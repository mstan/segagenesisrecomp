#ifndef GENESIS_PCM_HLE_H
#define GENESIS_PCM_HLE_H
#include <stddef.h>
#include <stdint.h>
typedef struct PcmHleClip {
    const uint8_t *samples;
    uint32_t frames,rate_numerator,rate_denominator,loop;
} PcmHleClip;
typedef struct PcmHleState {uint64_t phase;uint32_t clip;uint8_t active,paused,pan;} PcmHleState;
typedef struct PcmHle {const PcmHleClip *clips;unsigned count;PcmHleState state;} PcmHle;
void pcm_hle_init(PcmHle *p,const PcmHleClip *clips,unsigned count);
int pcm_hle_play(PcmHle *p,unsigned clip,unsigned pan);
void pcm_hle_pause(PcmHle *p,int paused);
void pcm_hle_stop(PcmHle *p);
int pcm_hle_mix(PcmHle *p,int16_t *stereo,size_t frames,unsigned rate);
int pcm_hle_state_valid(const PcmHle *p,const PcmHleState *state);
/* Deliver original unsigned DAC values at native master-clock offsets. This
 * passes through the regular chip/filter path without a guest sample loop. */
typedef void (*PcmHleDac)(void *user,uint32_t offset,uint8_t value);
int pcm_hle_events(PcmHle *p,uint32_t cycles,uint32_t master_rate,PcmHleDac emit,void *user);
#endif
