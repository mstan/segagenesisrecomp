/*
 * psg.c — SN76489 PSG stub.
 *
 * The SN76489 provides 3 square-wave tone channels and 1 noise channel.
 * It is write-only and is accessed via a single byte write to $C00011.
 * Write format:
 *   First byte:  1 xxx dd dd  — latch/data (channel select + low 4 bits of freq or volume)
 *   Second byte: 0 xx dddddd  — data (high 6 bits of frequency)
 *
 * TODO: implement tone generation and audio output mixing.
 */
#include "psg.h"
#include <string.h>
#include <stdio.h>

static int s_sample_rate = 44100;

typedef struct {
    int     freq;     /* 10-bit frequency counter reload value */
    int     counter;  /* Current counter */
    int     output;   /* Current square wave output (+1 or -1) */
    uint8_t volume;   /* 4-bit attenuation (0=max, 15=silent) */
} PsgTone;

typedef struct {
    uint8_t  ctrl;    /* Noise control register */
    int      lfsr;    /* Linear feedback shift register */
    uint8_t  volume;
} PsgNoise;

static PsgTone  s_tone[3];
static PsgNoise s_noise;
static int      s_latch_ch  = 0;   /* Latched channel (0-3) */
static int      s_latch_type = 0;  /* 0=freq, 1=volume */

void psg_init(int sample_rate) {
    s_sample_rate = sample_rate;
    memset(s_tone,  0, sizeof(s_tone));
    memset(&s_noise, 0, sizeof(s_noise));
    s_noise.lfsr = 0x8000;
    for (int i = 0; i < 3; i++) s_tone[i].volume = 15;  /* silent */
    s_noise.volume = 15;
}

void psg_reset(void) {
    psg_init(s_sample_rate);
}

void psg_write(uint8_t val) {
    if (val & 0x80) {
        /* Latch/data byte */
        s_latch_ch   = (val >> 5) & 3;
        s_latch_type = (val >> 4) & 1;
        if (s_latch_ch < 3) {
            if (s_latch_type) {
                s_tone[s_latch_ch].volume = val & 0xF;
            } else {
                s_tone[s_latch_ch].freq = (s_tone[s_latch_ch].freq & 0x3F0) | (val & 0xF);
            }
        } else {
            if (s_latch_type) s_noise.volume = val & 0xF;
            else              s_noise.ctrl   = val & 0xF;
        }
    } else {
        /* Data byte */
        if (s_latch_ch < 3 && !s_latch_type) {
            s_tone[s_latch_ch].freq = (s_tone[s_latch_ch].freq & 0xF) | ((val & 0x3F) << 4);
        }
    }
}

void psg_generate(int16_t *buf, int num_samples) {
    /* TODO: generate square wave + noise samples */
    for (int i = 0; i < num_samples; i++)
        buf[i] = 0;
}
