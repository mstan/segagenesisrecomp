/*
 * ym2612.c — cycle-stamped YM2612 wrapper over clownmdemu's FM library.
 *
 * This branch reuses clownmdemu's FM state machine (fm.c, known correct for
 * SMPS audio — the oracle build's sound is already validated against it)
 * as a pure library. We drive it with the cycle-stamped events queued by
 * 68K bus writes, calling FM_Update between writes so envelope/LFO state
 * advances continuously across the ~20k-cycle VBla music-handler span.
 *
 * A follow-up commit can lift fm.c into runner/audio/ for production
 * builds that drop clownmdemu entirely; the API here (ym2612_advance /
 * write / render) is designed to stay stable across that transition.
 */
#include "ym2612.h"
#include "fm.h"
#include <string.h>

static FM s_fm;
static int s_inited = 0;

/* Render buffer that FM_Update's callback fills. FM_OutputSamples emits
 * stereo samples at clownmdemu's FM sample rate (master / 144 = 372870 Hz
 * pre-divide; FM_Update internally emits at divider intervals).
 *
 * For the mixer: we hold samples generated during ym2612_advance in this
 * buffer and then ym2612_render copies them out. Simpler path would be
 * to have advance write directly to caller buffer, but FM_Update's
 * callback signature doesn't carry a user pointer to a destination —
 * we'd need static state anyway. */
#define FM_SCRATCH_STEREO_SAMPLES  8192
static int16_t s_scratch[FM_SCRATCH_STEREO_SAMPLES * 2];
static size_t  s_scratch_write = 0;  /* next free stereo-sample index */
static size_t  s_scratch_read  = 0;  /* next unread stereo-sample index */

/* FM_Update's callback: emit `total_frames` stereo samples into the
 * scratch ring starting at s_scratch_write. */
static void fm_emit_callback(const void *user_data, cc_u32f total_frames)
{
    (void)user_data;
    size_t remaining = total_frames;
    while (remaining > 0) {
        size_t avail = FM_SCRATCH_STEREO_SAMPLES - s_scratch_write;
        if (avail == 0) {
            /* Scratch full — consumer didn't drain fast enough. Drop
             * these samples. Shouldn't happen at 3728 samples/frame
             * and 8192-sample buffer. */
            return;
        }
        size_t chunk = remaining < avail ? remaining : avail;
        /* FM_OutputSamples accumulates (+=) into the buffer, so zero
         * the region first. */
        memset(&s_scratch[s_scratch_write * 2], 0, chunk * 2 * sizeof(int16_t));
        FM_OutputSamples(&s_fm, &s_scratch[s_scratch_write * 2], (cc_u32f)chunk);
        s_scratch_write += chunk;
        remaining       -= chunk;
    }
}

void ym2612_init(void)
{
    FM_Initialise(&s_fm);
    /* Ladder effect on (authentic) by default. The flag is INVERTED:
     * `ladder_effect_disabled = cc_false` means ladder is enabled. */
    s_fm.configuration.ladder_effect_disabled = cc_false;
    s_fm.configuration.dac_channel_disabled   = cc_false;
    for (int i = 0; i < FM_TOTAL_CHANNELS; i++)
        s_fm.configuration.fm_channels_disabled[i] = cc_false;
    s_scratch_write = s_scratch_read = 0;
    s_inited = 1;
}

static uint32_t s_fm_leftover_master = 0;

void ym2612_advance(uint32_t cycles_master)
{
    if (!s_inited) ym2612_init();
    if (cycles_master == 0) return;
    /* Argument is MASTER cycles. FM_Update takes 68K cycles
     * (master / M68K_CLOCK_DIVIDER = master / 7). Carry the remainder
     * so fractional 68K cycles accumulate across calls and we don't
     * lose precision the way the old /7-per-event approach did. */
    uint64_t total_master = (uint64_t)cycles_master + s_fm_leftover_master;
    uint32_t cycles_68k   = (uint32_t)(total_master / 7u);
    s_fm_leftover_master  = (uint32_t)(total_master % 7u);
    if (cycles_68k > 0)
        FM_Update(&s_fm, (cc_u32f)cycles_68k, fm_emit_callback, NULL);
}

void ym2612_write(uint8_t port, uint8_t value)
{
    if (!s_inited) ym2612_init();
    /* port 0/2 = FM address register (part 1 / part 2);
     * port 1/3 = FM data register. */
    switch (port) {
        case 0: FM_DoAddress(&s_fm, 0, value); break;
        case 1: FM_DoData(&s_fm, value);       break;
        case 2: FM_DoAddress(&s_fm, 1, value); break;
        case 3: FM_DoData(&s_fm, value);       break;
        default: break;  /* not an FM port */
    }
}

size_t ym2612_render(int16_t *out, size_t sample_count)
{
    if (!out || sample_count == 0) return 0;
    size_t available = s_scratch_write - s_scratch_read;
    size_t copy = sample_count < available ? sample_count : available;
    if (copy > 0) {
        memcpy(out, &s_scratch[s_scratch_read * 2], copy * 2 * sizeof(int16_t));
        s_scratch_read += copy;
    }
    /* Rewind both pointers when fully drained. Simpler than wraparound. */
    if (s_scratch_read >= s_scratch_write) {
        s_scratch_read = s_scratch_write = 0;
    }
    return copy;
}

size_t ym2612_samples_available(void)
{
    return s_scratch_write - s_scratch_read;
}

uint32_t ym2612_sample_rate(void)
{
    /* FM_Update takes cycles_to_do in 68K cycles and emits one stereo
     * sample every FM_SAMPLE_RATE_DIVIDER=144 cycles. 68K clock is
     * master/7 = 7670453 Hz NTSC → FM sample rate 7670453 / 144 = 53267 Hz.
     * Downstream audio.c's FM-to-PSG resampler (audio.c:63-79) handles
     * the upsample to PSG's 223721 Hz. */
    return 53267;
}
