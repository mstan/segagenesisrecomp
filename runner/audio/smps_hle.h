#ifndef GENESIS_SMPS_HLE_H
#define GENESIS_SMPS_HLE_H
#include <stddef.h>
#include <stdint.h>

/* Host-owned, normalized sequence data. Nothing is mapped into the cartridge
 * address space and no uploaded CPU program is executed. The title binds its
 * verified original image, sample/voice data and native sound request IDs. */
enum { SMPS_HLE_TRACKS=10, SMPS_HLE_STACK=16, SMPS_HLE_LOOPS=16 };
enum { SMPS_HLE_INTERRUPT=1, SMPS_HLE_ONE_PASS=2 };
typedef enum SmpsHleEventKind {
    SMPS_HLE_NOTE, SMPS_HLE_REST, SMPS_HLE_VOICE, SMPS_HLE_VOLUME,
    SMPS_HLE_PAN, SMPS_HLE_NOISE, SMPS_HLE_PCM, SMPS_HLE_FM_REGISTER,
    SMPS_HLE_ENVELOPE, SMPS_HLE_MODULATION, SMPS_HLE_SOUND_REQUEST,
    SMPS_HLE_COMMUNICATION, SMPS_HLE_TICK,
    SMPS_HLE_BACKUP, SMPS_HLE_BEGIN, SMPS_HLE_RESTORE, SMPS_HLE_BEGIN_TRACK
} SmpsHleEventKind;
typedef struct SmpsHleEvent {
    SmpsHleEventKind kind;
    unsigned channel; /* DAC=0, FM1..6=1..6, PSG1..3=7..9 */
    uint32_t value;
    int argument;
} SmpsHleEvent;
/* Return zero for unsupported output. Failure is explicit; the caller must
 * report it instead of falling back to a guest driver. */
typedef int (*SmpsHleEmit)(void *user,const SmpsHleEvent *event);
typedef struct SmpsHleTrack {
    uint32_t cursor,voice_base,returns[SMPS_HLE_STACK],effect;
    uint16_t remaining,duration,gate,gate_remaining;
    uint8_t loops[SMPS_HLE_LOOPS];
    uint8_t active,depth,divider,voice,pan,volume,note,tie,envelope,modulation_control,continuous;
    int8_t transpose,detune;
} SmpsHleTrack;
typedef struct SmpsHleClock {
    SmpsHleTrack track[SMPS_HLE_TRACKS];
    uint8_t tempo,accumulator,paused;
} SmpsHleClock;
typedef struct SmpsHleState {
    SmpsHleClock music,previous;
    uint32_t song,previous_song;
    uint8_t interrupted,failed,restore_on_loop,lead_channel;
} SmpsHleState;
typedef struct SmpsHle {
    const uint8_t *data;
    size_t bytes;
    SmpsHleEmit emit;
    void *user;
    SmpsHleState state; /* pointer-free current-build snapshot payload */
    char error[160];
} SmpsHle;
void smps_hle_init(SmpsHle *s,const uint8_t *data,size_t bytes,SmpsHleEmit emit,void *user);
/* INTERRUPT preserves the previous song clock. ONE_PASS additionally restores
 * it at the lead channel's first top-level loop, for full-song event choices. */
int smps_hle_music(SmpsHle *s,uint32_t offset,int flags);
/* Effects replace only their listed physical tracks; other effect tracks and
 * the separate music clock continue. Header channels use YM/PSG numbering. */
int smps_hle_effect(SmpsHle *s,uint32_t offset,int continuous);
int smps_hle_frame(SmpsHle *s);
void smps_hle_pause(SmpsHle *s,int paused);
int smps_hle_stop(SmpsHle *s);
int smps_hle_state_valid(const SmpsHle *s,const SmpsHleState *state);
#endif
