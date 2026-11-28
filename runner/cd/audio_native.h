#ifndef SEGACD_AUDIO_NATIVE_H
#define SEGACD_AUDIO_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#include "segacd.h"

typedef struct SegaCDNativeAudio SegaCDNativeAudio;

typedef enum {
    SEGACD_NATIVE_AUDIO_OK = 0,
    SEGACD_NATIVE_AUDIO_INVALID,
    SEGACD_NATIVE_AUDIO_NO_MEMORY,
    SEGACD_NATIVE_AUDIO_EVENT_OVERFLOW,
    SEGACD_NATIVE_AUDIO_BACKWARDS_WRITE,
    SEGACD_NATIVE_AUDIO_BAD_PORT,
    SEGACD_NATIVE_AUDIO_RENDER_OVERFLOW,
    SEGACD_NATIVE_AUDIO_MIX_UNDERFLOW,
    SEGACD_NATIVE_AUDIO_MIX_ERROR,
    SEGACD_NATIVE_AUDIO_HOST_AUDIO_ERROR
} SegaCDNativeAudioResult;

typedef struct {
    uint64_t queued_events;
    uint64_t applied_events;
    uint64_t deferred_events;
    uint64_t rendered_psg_frames;
    uint64_t rendered_fm_frames;
    uint64_t flushed_frames;
    uint64_t event_overflows;
    uint64_t backwards_writes;
    uint64_t mix_underflows;
    uint64_t output_frames;
    uint32_t faults;
    SegaCDNativeAudioResult last_result;
} SegaCDNativeAudioStats;

enum {
    SEGACD_NATIVE_AUDIO_FAULT_OVERFLOW = 1u << 0,
    SEGACD_NATIVE_AUDIO_FAULT_BACKWARDS = 1u << 1,
    SEGACD_NATIVE_AUDIO_FAULT_BAD_PORT = 1u << 2,
    SEGACD_NATIVE_AUDIO_FAULT_RENDER = 1u << 3,
    SEGACD_NATIVE_AUDIO_FAULT_MIX = 1u << 4
};

/* Create the single host audio pipeline. initialize_host_audio opens the SDL
 * device through audio_init(); tests and offline users may pass zero. The
 * native chip clocks still advance independently of the device callback. */
SegaCDNativeAudio *segacd_native_audio_create(int initialize_host_audio);
void segacd_native_audio_destroy(SegaCDNativeAudio *audio);

/* Observer suitable for SegaCDMachine.audio_write. It queues absolute
 * 12.5 MHz Sub-clock stamps; writes are sorted stably at the next boundary.
 * Stamps older than the already-rendered master-clock frontier are rejected. */
void segacd_native_audio_write(void *user,uint64_t subclock,uint8_t port,uint8_t value);
SegaCDNativeAudioResult segacd_native_audio_write_checked(SegaCDNativeAudio *,
                                                           uint64_t subclock,
                                                           uint8_t port,uint8_t value);

/* Sink wrappers for SegaCDIO PCM/CDDA callbacks. Samples keep their exact
 * absolute Sub-clock stamps and are consumed by the clock-domain mixer. */
void segacd_native_audio_pcm_sample(void *user,uint64_t subclock,
                                    int16_t left,int16_t right);
void segacd_native_audio_cdda_sample(void *user,uint64_t subclock,
                                     int16_t left,int16_t right);
/* Attach just the two sample outputs, preserving all media/drive callbacks in
 * the caller's SegaCDIO. Set the machine's audio_write/audio_user separately. */
void segacd_native_audio_attach_io(SegaCDNativeAudio *,SegaCDIO *);
/* The real SegaCDIO emits timestamped silence while stopped, so ordinary
 * transport changes should not toggle this. Use only for an absent stream. */
SegaCDNativeAudioResult segacd_native_audio_set_cdda_active(SegaCDNativeAudio *,int active);

/* Call after completed simulation frame N with its absolute master-clock end
 * (normally N*896040 when frames are numbered from 1 at time zero). The call
 * advances/renders frame N and flushes the previously held frame N-1, giving
 * both sample streams one frame of interpolation lookahead. Do not subtract
 * a frame from the supplied boundary. */
SegaCDNativeAudioResult segacd_native_audio_finish_frame(SegaCDNativeAudio *,
                                                         uint64_t end_master_clock,
                                                         uint32_t wall_frame,
                                                         int realtime);

/* Flush a final pending frame explicitly. Without another simulated frame of
 * source lookahead this can report MIX_UNDERFLOW; it never fabricates samples. */
SegaCDNativeAudioResult segacd_native_audio_flush_pending(SegaCDNativeAudio *,
                                                         int realtime);
void segacd_native_audio_get_stats(const SegaCDNativeAudio *,SegaCDNativeAudioStats *);

#endif
