/*
 * blastem_psg.c — minimal standalone SN76489 replay core, vendored from
 * BlastEm's psg.c (Copyright 2013 Michael Pavone, GPLv3). DEV-ONLY harness
 * reference. NEVER shipped / linked into runner or recompiler. See header for
 * the full licensing posture.
 *
 * Every line below that is not the emit shim is a verbatim copy of BlastEm
 * psg.c (psg_write + the body of psg_run + volume_table + PSG_VOL_DIV), so the
 * LFSR-noise math being adjudicated is byte-identical to upstream BlastEm.
 */
#include "blastem_psg.h"
#include <string.h>

void bpsg_init(bpsg_context *context)
{
    memset(context, 0, sizeof(*context));
    for (int i = 0; i < 4; i++) {
        context->volume[i] = 0xF;
    }
    context->pan = 0xFF;
}

/* ---- verbatim from BlastEm psg.c::psg_write (vgm/event_log removed) ---- */
void bpsg_write(bpsg_context *context, uint8_t value)
{
    if (value & 0x80) {
        context->latch = value & 0x70;
        uint8_t channel = value >> 5 & 0x3;
        if (value & 0x10) {
            context->volume[channel] = value & 0xF;
        } else {
            if (channel == 3) {
                switch(value & 0x3)
                {
                case 0:
                case 1:
                case 2:
                    context->counter_load[3] = 0x10 << (value & 0x3);
                    context->noise_use_tone = 0;
                    break;
                default:
                    context->counter_load[3] = context->counter_load[2];
                    context->noise_use_tone = 1;
                }
                context->noise_type = value & 0x4;
                context->lsfr = 0x8000;
            } else {
                context->counter_load[channel] = (context->counter_load[channel] & 0x3F0) | (value & 0xF);
                if (channel == 2 && context->noise_use_tone) {
                    context->counter_load[3] = context->counter_load[2];
                }
            }
        }
    } else {
        if (!(context->latch & 0x10)) {
            uint8_t channel = context->latch >> 5 & 0x3;
            if (channel != 3) {
                context->counter_load[channel] = (value << 4 & 0x3F0) | (context->counter_load[channel] & 0xF);
                if (channel == 2 && context->noise_use_tone) {
                    context->counter_load[3] = context->counter_load[2];
                }
            }
        }
    }
}

#define PSG_VOL_DIV 14

/* table shamelessly swiped from PSG doc from smspower.org (verbatim, BlastEm) */
static int16_t volume_table[16] = {
    32767/PSG_VOL_DIV, 26028/PSG_VOL_DIV, 20675/PSG_VOL_DIV, 16422/PSG_VOL_DIV, 13045/PSG_VOL_DIV, 10362/PSG_VOL_DIV,
    8231/PSG_VOL_DIV, 6568/PSG_VOL_DIV, 5193/PSG_VOL_DIV, 4125/PSG_VOL_DIV, 3277/PSG_VOL_DIV, 2603/PSG_VOL_DIV,
    2067/PSG_VOL_DIV, 1642/PSG_VOL_DIV, 1304/PSG_VOL_DIV, 0
};

/* ---- body of BlastEm psg.c::psg_run, run nsamples times, mono emit ---- */
void bpsg_run(bpsg_context *context, int16_t *out, size_t nsamples)
{
    for (size_t s = 0; s < nsamples; s++) {
        for (int i = 0; i < 4; i++) {
            if (context->counters[i]) {
                context->counters[i] -= 1;
            }
            if (!context->counters[i]) {
                context->counters[i] = context->counter_load[i];
                context->output_state[i] = !context->output_state[i];
                if (i == 3 && context->output_state[i]) {
                    context->noise_out = context->lsfr & 1;
                    context->lsfr = (context->lsfr >> 1) | (context->lsfr << 15);
                    if (context->noise_type) {
                        //white noise
                        if (context->lsfr & 0x4) {
                            context->lsfr ^= 0x8000;
                        }
                    }
                }
            }
        }

        int16_t left_accum = 0, right_accum = 0;
        uint8_t pan_left = 0x10, pan_right = 0x1;

        int16_t value;
        for (int i = 0; i < 3; i++) {
            if (context->output_state[i]) {
                value = volume_table[context->volume[i]];
                if (context->pan & pan_left) {
                    left_accum += value;
                }
                if (context->pan & pan_right) {
                    right_accum += value;
                }
            } else {
                value = 0;
            }
            pan_left <<= 1;
            pan_right <<= 1;
        }
        value = 0;
        if (context->noise_out) {
            value = volume_table[context->volume[3]];
            if (context->pan & pan_left) {
                left_accum += value;
            }
            if (context->pan & pan_right) {
                right_accum += value;
            }
        }

        (void)right_accum; /* Genesis pan=0xFF -> L==R; emit mono left */
        out[s] = left_accum;
    }
}
