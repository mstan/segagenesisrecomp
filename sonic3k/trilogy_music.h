#pragma once
#include "trilogy_state.h"
void tr_audio_state(TrStateIO *io);
#include <stddef.h>
#include <stdint.h>

/* Private owner-ROM data, decoded at startup, never serialized into saves. */
enum { TR_MUSIC_CUES=7, TR_MUSIC_BANK_SIZE=0x8000 };
typedef struct TrMusic {
    uint8_t song[TR_MUSIC_CUES][TR_MUSIC_BANK_SIZE];
    unsigned song_bytes[TR_MUSIC_CUES];
    uint8_t drums[TR_MUSIC_BANK_SIZE];
    uint8_t envelope[13][128];
    unsigned envelopes,drum_count;
} TrMusic;
extern const uint8_t tr_music_ids[TR_MUSIC_CUES];
extern const uint8_t tr_music_envelope_ids[13];
int tr_music_decode(unsigned pack,const uint8_t *rom,size_t size,TrMusic *out,char *error,size_t error_size);
void tr_audio_reset(void);
int tr_audio_load(unsigned pack,const uint8_t *rom,size_t size);
void tr_audio_cue(unsigned pack,unsigned id);
