#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

/*
 * Output sample rate is the PSG rate (~223721 Hz NTSC).
 * The reference mixer (clownmdemu-frontend-common/mixer.h) uses PSG as the
 * base rate so PSG never needs resampling.  FM is upsampled to match.
 */
int  audio_init(int psg_sample_rate);
void audio_close(void);

/*
 * Mix one video frame and push to the SDL ring buffer.
 *
 *   wall_frame — runner wall-frame number (delivery-ring stamp)
 *   realtime   — nonzero when the pacer is running at NTSC rate (not turbo);
 *                delivery anomalies (drops/underruns) are only meaningful —
 *                and only counted — in realtime mode
 *   fm_buf    — stereo int16 at FM rate  (~53267 Hz)
 *   fm_frames — number of FM stereo frames
 *   psg_buf   — mono   int16 at PSG rate (~223721 Hz)  ← output rate base
 *   psg_frames— number of PSG mono frames
 *
 * FM is upsampled to PSG rate; PSG is expanded mono→stereo.
 * Volume divisors match the reference: PSG÷8, FM÷1.
 */
void audio_flush(uint32_t wall_frame, int realtime,
                 const int16_t *fm_buf,  size_t fm_frames,
                 const int16_t *psg_buf, size_t psg_frames);

/*
 * WAV capture: write mixed stereo output to a .wav file.
 * Call audio_wav_start("out.wav") to begin, audio_wav_stop() to finalize.
 * While active, audio_flush() writes to both SDL and the WAV file.
 */
int  audio_wav_start(const char *path);
void audio_wav_stop(void);
int  audio_wav_active(void);

/* Audio stats for debug queries */
typedef struct {
    size_t last_fm_frames;
    size_t last_psg_frames;
    size_t total_fm_frames;
    size_t total_psg_frames;
    uint32_t total_flushes;
    uint32_t dropped_flushes;       /* realtime: SDL queue over hard cap */
    uint32_t turbo_dropped_flushes; /* turbo: expected, not an anomaly */
    uint32_t underrun_flushes;      /* realtime: queue found EMPTY at flush —
                                       the device starved (played silence)
                                       at some point since the last flush */
    uint32_t min_queued_bytes;      /* realtime low-water mark after warmup
                                       (UINT32_MAX until first sample) */
} AudioStats;
void audio_get_stats(AudioStats *out);
uint32_t audio_queued_bytes(void);

/*
 * Always-on delivery observability (PRINCIPLES.md #17 — ring, not arm).
 *
 * The WAV capture and the [BOOP] detector both tap the GENERATED stream;
 * neither can see what the speaker actually played. A queue-cap drop or
 * an underrun splices the output waveform at the device while leaving
 * the WAV gapless — completely invisible to every generated-stream probe.
 * These rings record the delivery side of every realtime flush so a probe
 * can correlate a heard artifact with a drop/underrun after the fact.
 */
typedef struct {
    uint32_t wall_frame;
    uint32_t flush_index;
    uint32_t queued_bytes;  /* UNDERRUN: wall-clock us since the previous
                               flush (the stall length that starved the
                               queue). DROP: queue depth at the drop. */
    uint8_t  kind;          /* 0 = underrun (queue empty), 1 = drop (over cap) */
} AudioDeliveryEvent;

/* Copy the most recent events (oldest first); returns count copied. */
size_t audio_get_delivery_events(AudioDeliveryEvent *out, size_t max);

/* Dump the per-flush depth ring + event ring to a text file.
 * Returns 0 on success. Ring spans the last ~68 s of realtime flushes. */
int audio_delivery_dump(const char *path);

#endif /* AUDIO_H */
