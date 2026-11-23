#ifndef GENESIS_SMPS_HLE_SYNTH_H
#define GENESIS_SMPS_HLE_SYNTH_H
#include "smps_hle.h"
typedef struct SmpsHleChipOutput {
    int (*fm)(void *user,unsigned port,unsigned reg,unsigned value);
    int (*psg)(void *user,unsigned value);
    int (*pcm)(void *user,unsigned note,unsigned pan);
    int (*request)(void *user,unsigned id);
    void *user;
} SmpsHleChipOutput;
typedef struct SmpsHleSynthTrack {
    uint32_t voice;
    uint16_t base_frequency,last_frequency;
    int16_t modulation;
    uint8_t volume,pan,active,note,psg_envelope,psg_index,last_volume,noise;
    uint8_t modulation_control,modulation_index,modulation_multiplier;
    uint8_t raw[4],raw_delay,raw_speed,raw_steps,raw_z80;
    int8_t detune,raw_delta;
} SmpsHleSynthTrack;
typedef struct SmpsHleSynthState {
    SmpsHleSynthTrack track[SMPS_HLE_TRACKS],previous[SMPS_HLE_TRACKS];
    uint8_t communication,interrupted;
} SmpsHleSynthState;
typedef struct SmpsHleSynth {
    const uint8_t *data;size_t bytes;
    const uint32_t *psg_envelopes,*modulation_envelopes;
    unsigned psg_count,modulation_count;
    SmpsHleChipOutput output;
    SmpsHleSynthState state; /* Include with sequencer/chips in current states. */
} SmpsHleSynth;
void smps_hle_synth_init(SmpsHleSynth *s,const uint8_t *data,size_t bytes,SmpsHleChipOutput output);
/* Suitable as SmpsHleEmit. Missing PCM/request consumers fail explicitly. */
int smps_hle_synth_event(void *user,const SmpsHleEvent *event);
/* Reinstall a suspended music channel after its effect releases ownership. */
int smps_hle_synth_resume(SmpsHleSynth *s,unsigned channel);
#endif
