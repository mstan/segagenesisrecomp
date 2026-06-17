#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

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
    want.freq     = psg_sample_rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;   /* push model via SDL_QueueAudio */

    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (s_dev == 0) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }
    s_stats.min_queued_bytes = UINT32_MAX;  /* low-water: nothing sampled yet */
    /* Prime the queue with the servo's target depth of silence so playback
     * starts with a cushion instead of riding the empty mark until the
     * +ratio servo crawls it up (4 frames ≈ 67 ms; matches
     * TARGET_DEPTH_FRAMES in audio_flush). */
    {
        static const int16_t silence[8192] = {0};
        uint32_t prime = (uint32_t)((uint64_t)psg_sample_rate * 4u * 2 * 2 / 60)
                         & ~3u;  /* stereo-s16 frame aligned */
        while (prime > 0) {
            uint32_t chunk = prime < sizeof(silence) ? prime : sizeof(silence);
            SDL_QueueAudio(s_dev, silence, chunk);
            prime -= chunk;
        }
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return 0;
}

void audio_close(void)
{
    if (s_dev != 0) {
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
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

    /* Delivery observability: sample queue depth at flush entry. An empty
     * queue here means the device consumed everything and played silence
     * for some span since the last flush — an audible splice the generated
     * stream (WAV, [BOOP] detector) can never show. */
    if (realtime) {
        uint32_t entry_queued = SDL_GetQueuedAudioSize(s_dev);
        uint64_t pc_now = SDL_GetPerformanceCounter();
        uint32_t dt_us = 0;
        if (s_last_flush_pc)
            dt_us = (uint32_t)((pc_now - s_last_flush_pc) * 1000000ull
                               / SDL_GetPerformanceFrequency());
        s_last_flush_pc = pc_now;
        s_depth_ring[s_depth_head].wall_frame   = wall_frame;
        s_depth_ring[s_depth_head].queued_bytes = entry_queued;
        s_depth_ring[s_depth_head].wall_dt_us   = dt_us;
        s_depth_head = (s_depth_head + 1) % DEPTH_RING_SIZE;
        s_depth_count++;
        if (s_stats.total_flushes >= DELIVERY_WARMUP_FLUSHES) {
            if (entry_queued < s_stats.min_queued_bytes)
                s_stats.min_queued_bytes = entry_queued;
            if (entry_queued == 0) {
                s_stats.underrun_flushes++;
                delivery_event(wall_frame, dt_us, 0);
            }
        }
    }

    /* Reference mixer approach: iterate at PSG rate (no PSG resampling at
     * ratio 1), upsample FM to the output rate via nearest-neighbour.
     * Divisors: PSG/8, FM/1 — same as clownmdemu-frontend-common/mixer.h.
     * out_n != psg_frames applies the drift servo's nearest-neighbour
     * micro-resample (≤ ±0.5%) on top. */
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

    /* WAV capture taps the GENERATED stream at ratio 1, before the drift
     * servo touches anything — WAV content stays bit-deterministic across
     * runs and pacing modes (the paired-capture diffing contract). The
     * delivery rings, not the WAV, are the record of what the speaker got. */
    if (s_wav_file) {
        MIX_INTO(psg_frames);
        uint32_t bytes = (uint32_t)(psg_frames * 2 * sizeof(int16_t));
        fwrite(s_out, 1, bytes, s_wav_file);
        s_wav_data_bytes += bytes;
    }

    /* Drift servo (dynamic rate control): the pacer runs on the CPU wall
     * clock, the device drains on the sound card's clock — two oscillators
     * that never agree exactly. Without feedback the queue slowly wanders
     * until it either runs empty (underrun: device plays a silence gap)
     * or hits the hard cap (drop: a whole frame spliced out) — both
     * audible, both invisible in the WAV; the measured cause of the
     * "occasional boop". Nudge this flush's output length by at most
     * ±0.5% (inaudible) toward holding TARGET_DEPTH_FRAMES of cushion;
     * steady state converges to production == consumption.
     *
     * Target depth: measured interactive main-loop stalls reach ~40 ms
     * (2.5 frames — [AUDIO-DELIVERY] dt_us evidence), which a 2-frame
     * cushion cannot absorb. 4 frames (~67 ms latency, in line with
     * common emulator defaults) rides through the measured stall class.
     * Known residual: the Sega-scream handler stalls the host ~300 ms
     * (atomic giant-handler execution) — no reasonable cushion covers
     * that; fixing it needs handler slicing in the scheduler. */
    #define TARGET_DEPTH_FRAMES 4
    #define SERVO_MAX_ADJ 0.005
    Uint32 frame_bytes = (Uint32)(psg_frames * 2 * sizeof(int16_t));
    size_t out_n = psg_frames;
    if (realtime) {
        Uint32 queued_now = SDL_GetQueuedAudioSize(s_dev);
        double target = (double)(TARGET_DEPTH_FRAMES * frame_bytes);
        double dev = (target - (double)queued_now) / target;
        if (dev >  1.0) dev =  1.0;
        if (dev < -1.0) dev = -1.0;
        out_n = (size_t)((double)psg_frames * (1.0 + SERVO_MAX_ADJ * dev) + 0.5);
        if (out_n > OUT_BUF_FRAMES) out_n = OUT_BUF_FRAMES;
        if (out_n < 1) out_n = 1;
    }
    MIX_INTO(out_n);

    /* Master volume — speaker output only (the WAV tap above is untouched, so
     * paired captures stay bit-deterministic regardless of this setting). */
    if (s_master_vol != 256) {
        for (size_t i = 0; i < out_n * 2; i++)
            s_out[i] = (int16_t)((int32_t)s_out[i] * s_master_vol / 256);
    }

    /* Hard cap on queue depth — safety net only now that the servo holds
     * depth near target. If the queue is somehow above ~8 frames worth,
     * drop this flush rather than let lag accumulate.
     * In realtime this drop IS an audible splice — record it in the
     * delivery rings. In turbo it fires every frame by design (production
     * outruns the device several-fold) and is not an anomaly. */
    Uint32 out_bytes = (Uint32)(out_n * 2 * sizeof(int16_t));
    Uint32 queued = SDL_GetQueuedAudioSize(s_dev);
    Uint32 limit = frame_bytes * 8;
    if (queued > limit) {
        if (realtime) {
            s_stats.dropped_flushes++;
            delivery_event(wall_frame, queued, 1);
        } else {
            s_stats.turbo_dropped_flushes++;
        }
    } else {
        SDL_QueueAudio(s_dev, s_out, out_bytes);
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
