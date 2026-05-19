/*
 * event_queue.h — cycle-stamped audio-event queue.
 *
 * Single-producer (68K game fiber) / single-consumer (main fiber drain at
 * frame end) ring buffer. Both run on the same OS thread and the producer
 * yields before the consumer runs, so no locking is needed.
 *
 * Every 68K write to FM ($A04000-$A04006) or PSG ($C00011) pushes an
 * AudioEvent onto this queue with a cycle stamp from g_audio_cycle_counter.
 * audio_drain() walks the queue in order, feeding the YM2612 / PSG models
 * which advance sample-accurately between events.
 */
#ifndef AUDIO_EVENT_QUEUE_H
#define AUDIO_EVENT_QUEUE_H

#include <stdint.h>
#include <stddef.h>  /* size_t */

enum {
    AUDIO_PORT_FM1_ADDR = 0,   /* $A04000 write */
    AUDIO_PORT_FM1_DATA = 1,   /* $A04002 write */
    AUDIO_PORT_FM2_ADDR = 2,   /* $A04004 write */
    AUDIO_PORT_FM2_DATA = 3,   /* $A04006 write */
    AUDIO_PORT_PSG      = 4,   /* $C00011 write */
};

typedef struct AudioEvent {
    uint32_t cycle_stamp;   /* g_audio_cycle_counter at time of write */
    uint8_t  port;
    uint8_t  value;
} AudioEvent;

/* Push one event. Safe to call from anywhere inside the game fiber. */
void audio_event_push(uint32_t cycle_stamp, uint8_t port, uint8_t value);

/* Drain iterator. Returns 1 if *out was filled, 0 if queue empty.
 * Call repeatedly until it returns 0 to walk the frame's events in order. */
int  audio_event_pop(AudioEvent *out);

/* Reset the queue — called at end-of-wall-frame after drain completes, or
 * on lifecycle init. */
void audio_event_queue_reset(void);

/* Diagnostic: current fill level. */
size_t audio_event_queue_count(void);

#endif
