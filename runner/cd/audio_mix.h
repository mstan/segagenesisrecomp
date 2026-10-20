#ifndef SEGACD_AUDIO_MIX_H
#define SEGACD_AUDIO_MIX_H

#include <stddef.h>
#include <stdint.h>

/* Host-side clock-domain mixer. All input timestamps and output-window
 * positions use the Sega CD 12.5 MHz subclock. The audio_extra adapter runs
 * synchronously on the emulation thread from audio_flush; it never advances
 * an emulated device and never runs in the SDL pull callback. */
enum {
    SEGACD_AUDIO_MIX_CLOCK_HZ = 12500000u,
    SEGACD_AUDIO_MIX_QUEUE_CAPACITY = 4096u,
    SEGACD_AUDIO_MIX_MAX_WINDOW_FRAMES = 16384u
};

typedef enum {
    SEGACD_AUDIO_SOURCE_PCM = 0,
    SEGACD_AUDIO_SOURCE_CDDA = 1,
    SEGACD_AUDIO_SOURCE_COUNT = 2
} SegaCDAudioSource;

typedef enum {
    SEGACD_AUDIO_MIX_OK = 0,
    SEGACD_AUDIO_MIX_INVALID,
    SEGACD_AUDIO_MIX_NOT_ACTIVE,
    SEGACD_AUDIO_MIX_PAUSED,
    SEGACD_AUDIO_MIX_QUEUE_FULL,
    SEGACD_AUDIO_MIX_BAD_TIMESTAMP,
    SEGACD_AUDIO_MIX_NEED_INPUT,
    SEGACD_AUDIO_MIX_WINDOW_MISMATCH,
    SEGACD_AUDIO_MIX_ALREADY_BOUND
} SegaCDAudioMixResult;

enum {
    SEGACD_AUDIO_FAULT_OVERFLOW = 1u << 0,
    SEGACD_AUDIO_FAULT_TIMESTAMP = 1u << 1,
    SEGACD_AUDIO_FAULT_UNDERFLOW = 1u << 2,
    SEGACD_AUDIO_FAULT_WINDOW = 1u << 3
};

typedef struct {
    uint64_t subclock;
    int16_t left;
    int16_t right;
} SegaCDAudioFrame;

typedef struct {
    SegaCDAudioFrame frame[SEGACD_AUDIO_MIX_QUEUE_CAPACITY];
    uint64_t last_subclock;
    uint64_t accepted_frames;
    uint64_t rejected_frames;
    uint64_t discarded_frames;
    uint16_t head;
    uint16_t count;
    uint8_t active;
    uint8_t have_last_subclock;
} SegaCDAudioQueue;

typedef struct {
    uint64_t first_subclock;
    uint32_t first_phase;       /* fraction numerator / step_denominator */
    uint32_t step_numerator;    /* subclock ticks per output frame */
    uint32_t step_denominator;
    uint32_t rate_hz;           /* AudioExtraSource's nominal integer rate */
    size_t frames;
    uint8_t pending;
} SegaCDAudioWindow;

typedef struct {
    SegaCDAudioQueue source[SEGACD_AUDIO_SOURCE_COUNT];
    SegaCDAudioWindow window;
    uint32_t faults;
    uint64_t rendered_frames;
    uint64_t underflow_blocks;
    uint64_t overflow_events;
    uint64_t paused_rejections;
    uint64_t next_output_subclock;
    uint32_t next_output_phase;
    uint32_t next_output_denominator;
    uint32_t next_output_step_numerator;
    uint32_t next_output_rate_hz;
    uint8_t paused;
    uint8_t next_output_valid;
    SegaCDAudioMixResult last_result;
} SegaCDAudioMix;

typedef struct {
    uint32_t faults;
    uint64_t rendered_frames;
    uint64_t underflow_blocks;
    uint64_t overflow_events;
    uint64_t paused_rejections;
    uint64_t pcm_queued_frames;
    uint64_t cdda_queued_frames;
    uint64_t pcm_discarded_frames;
    uint64_t cdda_discarded_frames;
    SegaCDAudioMixResult last_result;
} SegaCDAudioMixStats;

void segacd_audio_mix_init(SegaCDAudioMix *mix);
void segacd_audio_mix_reset(SegaCDAudioMix *mix);
/* Activation resets that source's queue. Inactive sources contribute zero. */
SegaCDAudioMixResult segacd_audio_mix_set_active(SegaCDAudioMix *mix,
                                                  SegaCDAudioSource source,
                                                  int active);
/* Timestamped pushes are emulation-thread operations. Overflow/backwards or
 * implausibly distant timestamps are reported and set sticky faults; a failed
 * push does not overwrite/drop an older accepted sample. */
SegaCDAudioMixResult segacd_audio_mix_push(SegaCDAudioMix *mix,
                                           SegaCDAudioSource source,
                                           uint64_t subclock,
                                           int16_t left,int16_t right);
/* Pause is an explicit timeline discontinuity: it drops queued host audio,
 * records the count, and makes further pushes return PAUSED. Resume clears
 * the output window; sources must be activated again before pushing. */
void segacd_audio_mix_pause(SegaCDAudioMix *mix);
void segacd_audio_mix_resume(SegaCDAudioMix *mix);
void segacd_audio_mix_clear_faults(SegaCDAudioMix *mix,uint32_t mask);

/* Configure an exact rational output window. Position i is
 * first_subclock + first_phase/step_denominator +
 * i*step_numerator/step_denominator. This supports the NTSC PSG period
 * 40,000,000/715,909 subclock ticks (master/240) without per-frame drift.
 * rate_hz is the integer rate passed by AudioExtraSource (223721 for NTSC).
 */
SegaCDAudioMixResult segacd_audio_mix_set_window(SegaCDAudioMix *mix,
                                                  uint64_t first_subclock,
                                                  uint32_t first_phase,
                                                  uint32_t step_numerator,
                                                  uint32_t step_denominator,
                                                  uint32_t rate_hz,
                                                  size_t frames);
/* Read-only preflight for a pending window. It does not consume queues, alter
 * the output, or set sticky fault counters; render_window records failures. */
SegaCDAudioMixResult segacd_audio_mix_check_window(const SegaCDAudioMix *mix,
                                                    size_t frames,
                                                    uint32_t rate_hz);
/* Add linearly interpolated PCM/CDDA to existing PSG/FM mix. If either active
 * source lacks a bracketing sample anywhere in the window, returns NEED_INPUT,
 * sets a sticky underflow fault, and leaves the entire output buffer and both
 * source queues unchanged. Callers must inspect the result/faults; no samples
 * are fabricated or silently dropped. */
SegaCDAudioMixResult segacd_audio_mix_render_window(SegaCDAudioMix *mix,
                                                    int16_t *mix_stereo,
                                                    size_t frames,
                                                    uint32_t rate_hz);
/* After a successful render, return the exact rational position for the next
 * contiguous window. Reset/pause clears this anchor; the caller then starts a
 * new epoch with an explicit first position. */
int segacd_audio_mix_next_output_position(const SegaCDAudioMix *mix,
                                          uint64_t *subclock,
                                          uint32_t *phase,
                                          uint32_t *denominator);

/* audio_extra_source adapter. One active binding is permitted because the
 * legacy AudioExtraSource API has no user pointer. Bind/unbind on the emulation
 * thread and keep the mixer alive until unbound. Before audio_flush, configure
 * a window with the exact current PSG sample timeline. After flush, inspect
 * last_result/faults; the void adapter cannot return errors to audio.c. */
SegaCDAudioMixResult segacd_audio_mix_bind_audio_extra(SegaCDAudioMix *mix);
void segacd_audio_mix_unbind_audio_extra(SegaCDAudioMix *mix);
void segacd_audio_mix_audio_extra_source(int16_t *mix_stereo,size_t frames,double rate);
void segacd_audio_mix_get_stats(const SegaCDAudioMix *mix,SegaCDAudioMixStats *out);

#endif
