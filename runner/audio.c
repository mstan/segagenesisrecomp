#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Shared clock-domain bridge: persistent band-limited polyphase resampler + a
 * P-only normalized-fill controller, plus Phase-1 stall concealment (boot
 * pre-roll + pitch-preserving underrun loop). Replaces the old nearest-neighbor
 * ±0.5% servo (which crackled because every correction dropped/duplicated
 * samples) and the >8-frame hard-drop. See recomp_audio_drc.h. This is the only
 * TU that pulls in the implementation. */
#define RECOMP_AUDIO_DRC_IMPL
#include "recomp_audio_drc.h"

/* Volume divisors from the reference mixer (clownmdemu-frontend-common/mixer.h).
 * FM is fixed; PSG is a RUNTIME knob ([SND-TRACE] tuning, default 8 = original
 * behaviour) so the PSG/FM balance can be dialled in by ear without rebuilds —
 * the clownmdemu ÷8 was tuned for clownmdemu's PSG scale, not our clean-room
 * SN76489's. Set via --psg-vol-div N. Bake the chosen value back into a
 * #define before commit. */
#define FM_VOL_DIV  1
static int s_psg_vol_div = 8;
void audio_set_psg_vol_div(int d) { if (d >= 1 && d <= 64) s_psg_vol_div = d; }

/* Master output volume (launcher / settings.ini). 0..256 fixed-point gain
 * applied to the SPEAKER stream only — the WAV capture tap stays bit-exact. */
static int s_master_vol = 256;
void audio_set_master_volume(int pct)
{
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    s_master_vol = pct * 256 / 100;
}

static SDL_AudioDeviceID s_dev = 0;

/* DRC bridge state. Producer (rab_push) runs on the emulation/main thread inside
 * audio_flush under SDL_LockAudioDevice; consumer (rab_pull) runs on the SDL
 * audio callback thread. SDL_LockAudioDevice gives us the SPSC guarantee. */
static rab_bridge s_bridge;
static int        s_bridge_ready = 0;
static uint64_t   s_prev_underruns = 0;

/* SDL pull callback: the device asks for `len` bytes of stereo S16; the bridge
 * resamples from the internal mix rate to the device rate under DRC and emits
 * faded silence (or pitch-preserving conceal) on underrun. No guest code, no
 * locks, no allocation here. */
static void genesis_audio_callback(void *user, Uint8 *stream, int len)
{
    (void)user;
    if (!s_bridge_ready) { memset(stream, 0, (size_t)len); return; }
    int frames = len / (int)(2 * sizeof(int16_t)); /* stereo S16 */
    rab_pull(&s_bridge, (int16_t *)stream, frames);
}

/* Audio stats */
static AudioStats s_stats = {0};

/* =========================================================================
 * Delivery observability — always-on rings (PRINCIPLES.md #17).
 *
 * Depth ring: SDL queue depth at the entry of every realtime flush.
 * 4096 entries ≈ 68 s of history at 59.94 fps.
 * Event ring: drops (queue over cap) and underruns (queue found empty).
 * Probes query backward after a heard artifact; nothing is "armed".
 * ========================================================================= */
#define DEPTH_RING_SIZE 4096
static struct {
    uint32_t wall_frame;
    uint32_t queued_bytes;
    uint32_t wall_dt_us;    /* wall-clock time since the previous realtime
                               flush — a stalled main loop shows as dt >>
                               16700 us right where the queue collapses */
} s_depth_ring[DEPTH_RING_SIZE];
static uint32_t s_depth_head = 0;   /* next write slot */
static uint32_t s_depth_count = 0;  /* total entries ever written */
static uint64_t s_last_flush_pc = 0;

#define EVT_RING_SIZE 256
static AudioDeliveryEvent s_evt_ring[EVT_RING_SIZE];
static uint32_t s_evt_head = 0;
static uint32_t s_evt_count = 0;

/* Skip the first few flushes: the queue is trivially empty at startup. */
#define DELIVERY_WARMUP_FLUSHES 30

static void delivery_event(uint32_t wall_frame, uint32_t detail, uint8_t kind)
{
    /* detail: UNDERRUN -> wall-clock us since previous flush (stall length);
     *         DROP     -> queued bytes at the over-cap drop. */
    AudioDeliveryEvent *e = &s_evt_ring[s_evt_head];
    e->wall_frame   = wall_frame;
    e->flush_index  = s_stats.total_flushes;
    e->queued_bytes = detail;
    e->kind         = kind;
    s_evt_head = (s_evt_head + 1) % EVT_RING_SIZE;
    s_evt_count++;
    /* Rare-event print (not hot-path telemetry): first 20 verbatim, then
     * every 100th, so a persistent starvation can't flood the log. */
    if (s_evt_count <= 20 || (s_evt_count % 100) == 0)
        fprintf(stderr, "[AUDIO-DELIVERY] %s frame=%u flush=%u %s=%u (event %u)\n",
                kind == 0 ? "UNDERRUN" : "DROP",
                wall_frame, s_stats.total_flushes,
                kind == 0 ? "dt_us" : "queued", detail, s_evt_count);
}

/* WAV capture state */
static FILE *s_wav_file = NULL;
static uint32_t s_wav_data_bytes = 0;

/* Scratch output buffer — sized for worst-case PSG frames per video frame.
 * PSG rate ~223721 Hz / 50 Hz (PAL) ≈ 4475 frames.  Double for headroom. */
#define OUT_BUF_FRAMES 16384
static int16_t s_out[OUT_BUF_FRAMES * 2];

int audio_init(int psg_sample_rate)
{
    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    /* Open at a normal host rate and let the DRC bridge own the (large)
     * downsample from the internal mix rate. Previously the device was opened
     * at the raw PSG rate (~223721 Hz) and SDL did the resample internally,
     * with our nearest-neighbor servo riding on top — both contributed to the
     * crackle. The bridge does a band-limited polyphase 223721->host downsample
     * with a P-only ratio servo, so the device runs at a clean 48 kHz. */
    want.freq     = 48000;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = genesis_audio_callback;

    /* Allow SDL to renegotiate the rate (some backends won't give exactly
     * 48000); the bridge keys off got.freq so any rate works. */
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got,
                                SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (s_dev == 0) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    rab_config cfg;
    rab_config_defaults(&cfg);
    cfg.channels    = 2;
    cfg.source_rate = (double)psg_sample_rate; /* internal FM+PSG mix rate */
    cfg.host_rate   = (double)got.freq;         /* device rate (48 kHz) */
    /* Phase-1 stall concealment: a ~200 ms boot pre-roll hides the cold-start
     * hitch for free (the servo drains the excess down to target over time),
     * and stretch_enable (default 1) conceals brief producer stalls by a
     * pitch-preserving loop of recent audio instead of fading to silence. */
    cfg.preroll_ms     = 200.0;
    cfg.stretch_enable = 1;     /* conceal on (defaults to 1; pinned explicit) */
    if (rab_init(&s_bridge, &cfg) != 0) {
        fprintf(stderr, "audio: rab_init (DRC bridge) failed\n");
        SDL_CloseAudioDevice(s_dev); s_dev = 0;
        return -1;
    }
    s_bridge_ready   = 1;
    s_prev_underruns = 0;
    s_stats.min_queued_bytes = UINT32_MAX;  /* low-water: nothing sampled yet */

    /* No manual silence prime: the bridge holds output muted until its ring
     * reaches the pre-roll fill, then fades in (clean startup, no edge click). */
    SDL_PauseAudioDevice(s_dev, 0);
    fprintf(stderr, "[audio] DRC bridge: %d Hz internal -> %d Hz device "
            "(preroll %.0f ms, conceal %s)\n",
            psg_sample_rate, got.freq, cfg.preroll_ms,
            cfg.stretch_enable ? "on" : "off");
    return 0;
}

void audio_close(void)
{
    if (s_dev != 0) {
        SDL_LockAudioDevice(s_dev);
        s_bridge_ready = 0;
        SDL_UnlockAudioDevice(s_dev);
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
        rab_free(&s_bridge);
    }
}

void audio_flush(uint32_t wall_frame, int realtime,
                 const int16_t *fm_buf,  size_t fm_frames,
                 const int16_t *psg_buf, size_t psg_frames)
{
    if (s_dev == 0 || psg_buf == NULL || psg_frames == 0)
        return;

    if (psg_frames > OUT_BUF_FRAMES)
        psg_frames = OUT_BUF_FRAMES;

    /* Delivery observability: the bridge ring fill (ms) at flush entry is the
     * analogue of the old SDL-queue depth, and the bridge's own underrun
     * counter is the authoritative record of the device starving (a splice the
     * generated WAV stream can never show). The queued_bytes field now carries
     * ring fill in milliseconds. */
    if (realtime) {
        uint64_t pc_now = SDL_GetPerformanceCounter();
        uint32_t dt_us = 0;
        if (s_last_flush_pc)
            dt_us = (uint32_t)((pc_now - s_last_flush_pc) * 1000000ull
                               / SDL_GetPerformanceFrequency());
        s_last_flush_pc = pc_now;
        rab_stats bst; rab_get_stats(&s_bridge, &bst);
        uint32_t fill_ms = (uint32_t)(rab_fill_ms(&s_bridge) + 0.5);
        s_depth_ring[s_depth_head].wall_frame   = wall_frame;
        s_depth_ring[s_depth_head].queued_bytes = fill_ms; /* ring fill, ms */
        s_depth_ring[s_depth_head].wall_dt_us   = dt_us;
        s_depth_head = (s_depth_head + 1) % DEPTH_RING_SIZE;
        s_depth_count++;
        if (s_stats.total_flushes >= DELIVERY_WARMUP_FLUSHES) {
            if (fill_ms < s_stats.min_queued_bytes)
                s_stats.min_queued_bytes = fill_ms;
            if (bst.underrun_events != s_prev_underruns) {
                s_stats.underrun_flushes++;
                delivery_event(wall_frame, dt_us, 0);
            }
        }
        s_prev_underruns = bst.underrun_events;
    }

    /* Reference mixer approach: iterate at PSG rate (no PSG resampling at
     * ratio 1), upsample FM to the output rate via nearest-neighbour.
     * Divisors: PSG/8, FM/1 — same as clownmdemu-frontend-common/mixer.h.
     * This is now a FIXED ratio-1 conversion, never rate-modulated: the DRC
     * bridge owns the band-limited downsample to the device rate and the
     * clock-domain servo, so this mix never drops/duplicates a sample. */
    #define MIX_INTO(out_n)                                                  \
        for (size_t i = 0; i < (out_n); i++) {                               \
            size_t si = (out_n) == psg_frames ? i                            \
                        : i * psg_frames / (out_n);                          \
            int32_t p = (int32_t)psg_buf[si] / s_psg_vol_div;                \
            int32_t l = p;                                                   \
            int32_t r = p;                                                   \
            if (fm_buf != NULL && fm_frames > 0) {                           \
                size_t fi = si * fm_frames / psg_frames;                     \
                l += (int32_t)fm_buf[fi * 2 + 0] / FM_VOL_DIV;               \
                r += (int32_t)fm_buf[fi * 2 + 1] / FM_VOL_DIV;               \
            }                                                                \
            if (l >  32767) l =  32767; else if (l < -32768) l = -32768;     \
            if (r >  32767) r =  32767; else if (r < -32768) r = -32768;     \
            s_out[i * 2 + 0] = (int16_t)l;                                   \
            s_out[i * 2 + 1] = (int16_t)r;                                   \
        }

    /* Mix one video frame at the internal PSG rate into s_out (PSG ratio 1; FM
     * upsampled to PSG rate). This is the single mix, a FIXED conversion, so it
     * no longer crackles. The DRC bridge owns the downsample and the servo. */
    MIX_INTO(psg_frames);

    /* WAV capture taps the GENERATED stream at ratio 1, before master volume
     * and before the bridge — WAV content stays bit-deterministic across runs
     * and pacing modes (the paired-capture diffing contract). The delivery
     * rings, not the WAV, are the record of what the speaker got. */
    if (s_wav_file) {
        uint32_t bytes = (uint32_t)(psg_frames * 2 * sizeof(int16_t));
        fwrite(s_out, 1, bytes, s_wav_file);
        s_wav_data_bytes += bytes;
    }

    /* Master volume — speaker output only (applied after the WAV tap, so paired
     * captures stay bit-deterministic regardless of this setting). */
    if (s_master_vol != 256) {
        for (size_t i = 0; i < psg_frames * 2; i++)
            s_out[i] = (int16_t)((int32_t)s_out[i] * s_master_vol / 256);
    }

    /* Hand the mixed frame to the DRC bridge. Realtime only: in turbo there is
     * no device playback, so pushing would just overflow the ring. The lock
     * gives the single-producer/single-consumer guarantee against the pull
     * callback. The bridge's P-only controller + faded/pitch-preserving conceal
     * replace the old ±0.5% nearest-neighbor servo and the >8-frame hard drop
     * (every one of which was a discard = a click/crackle). */
    if (realtime && s_bridge_ready) {
        SDL_LockAudioDevice(s_dev);
        rab_push(&s_bridge, s_out, (int)psg_frames);
        SDL_UnlockAudioDevice(s_dev);
    } else if (!realtime) {
        s_stats.turbo_dropped_flushes++;
    }

    /* Stats */
    s_stats.last_fm_frames  = fm_frames;
    s_stats.last_psg_frames = psg_frames;
    s_stats.total_fm_frames  += fm_frames;
    s_stats.total_psg_frames += psg_frames;
    s_stats.total_flushes++;
}

/* =========================================================================
 * WAV capture
 * ========================================================================= */

static void wav_write_header(FILE *f, uint32_t sample_rate, uint32_t data_bytes)
{
    uint32_t total = 36 + data_bytes;
    uint16_t channels = 2;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);

    fwrite("RIFF", 1, 4, f);
    fwrite(&total, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t pcm = 1;
    fwrite(&pcm, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

int audio_wav_start(const char *path)
{
    if (s_wav_file) audio_wav_stop();
    s_wav_file = fopen(path, "wb");
    if (!s_wav_file) return -1;
    s_wav_data_bytes = 0;
    /* Write placeholder header — will be updated on stop */
    wav_write_header(s_wav_file, 223721, 0);
    fprintf(stderr, "[audio] WAV capture started: %s\n", path);
    return 0;
}

void audio_wav_stop(void)
{
    if (!s_wav_file) return;
    /* Rewrite header with correct data size */
    fseek(s_wav_file, 0, SEEK_SET);
    wav_write_header(s_wav_file, 223721, s_wav_data_bytes);
    fclose(s_wav_file);
    s_wav_file = NULL;
    fprintf(stderr, "[audio] WAV capture stopped: %u bytes\n", s_wav_data_bytes);
}

int audio_wav_active(void) { return s_wav_file != NULL; }

void audio_get_stats(AudioStats *out) { *out = s_stats; }
uint32_t audio_queued_bytes(void) { return s_dev ? SDL_GetQueuedAudioSize(s_dev) : 0; }

size_t audio_get_delivery_events(AudioDeliveryEvent *out, size_t max)
{
    uint32_t avail = s_evt_count < EVT_RING_SIZE ? s_evt_count : EVT_RING_SIZE;
    size_t n = avail < max ? avail : max;
    /* Oldest-first: start n entries back from the head. */
    uint32_t start = (s_evt_head + EVT_RING_SIZE - (uint32_t)n) % EVT_RING_SIZE;
    for (size_t i = 0; i < n; i++)
        out[i] = s_evt_ring[(start + i) % EVT_RING_SIZE];
    return n;
}

int audio_delivery_dump(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# audio delivery rings — queue depth at flush entry + anomaly events\n");
    fprintf(f, "# stats: flushes=%u dropped=%u turbo_dropped=%u underruns=%u min_queued=%u\n",
            s_stats.total_flushes, s_stats.dropped_flushes,
            s_stats.turbo_dropped_flushes, s_stats.underrun_flushes,
            s_stats.min_queued_bytes);
    fprintf(f, "# events (oldest first, ring holds last %u of %u total):\n",
            (unsigned)(s_evt_count < EVT_RING_SIZE ? s_evt_count : EVT_RING_SIZE),
            s_evt_count);
    {
        AudioDeliveryEvent evs[EVT_RING_SIZE];
        size_t n = audio_get_delivery_events(evs, EVT_RING_SIZE);
        for (size_t i = 0; i < n; i++)
            fprintf(f, "EVENT %s frame=%u flush=%u %s=%u\n",
                    evs[i].kind == 0 ? "UNDERRUN" : "DROP",
                    evs[i].wall_frame, evs[i].flush_index,
                    evs[i].kind == 0 ? "dt_us" : "queued",
                    evs[i].queued_bytes);
    }
    fprintf(f, "# depth ring (oldest first, last %u of %u realtime flushes):\n",
            (unsigned)(s_depth_count < DEPTH_RING_SIZE ? s_depth_count : DEPTH_RING_SIZE),
            s_depth_count);
    {
        uint32_t avail = s_depth_count < DEPTH_RING_SIZE ? s_depth_count : DEPTH_RING_SIZE;
        uint32_t start = (s_depth_head + DEPTH_RING_SIZE - avail) % DEPTH_RING_SIZE;
        for (uint32_t i = 0; i < avail; i++) {
            uint32_t idx = (start + i) % DEPTH_RING_SIZE;
            fprintf(f, "DEPTH frame=%u queued=%u dt_us=%u\n",
                    s_depth_ring[idx].wall_frame, s_depth_ring[idx].queued_bytes,
                    s_depth_ring[idx].wall_dt_us);
        }
    }
    fclose(f);
    return 0;
}
