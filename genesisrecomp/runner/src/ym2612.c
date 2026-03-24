/*
 * ym2612.c — YM2612 FM synthesis stub.
 *
 * The YM2612 provides 6 FM channels, each with 4 operators (OPN2 variant).
 * It is accessed at $A04000/$A04001 (port 0) and $A04002/$A04003 (port 1).
 * DAC mode on channel 6 allows 8-bit PCM playback.
 *
 * A full implementation requires:
 *   - Envelope generators (ADSR) per operator
 *   - Phase/frequency accumulators
 *   - Feedback and algorithm routing
 *   - LFO (vibrato/tremolo)
 *   - DAC latch for channel 6
 *
 * TODO: integrate a YM2612 core (e.g. Nuked-OPN2 or similar).
 * This file is a stub that silently accepts all register writes.
 */
#include "ym2612.h"
#include <string.h>
#include <stdio.h>

static uint8_t s_regs[2][256];   /* Two register banks */
static uint8_t s_addr[2];        /* Currently selected register per port */

void ym2612_init(int sample_rate) {
    (void)sample_rate;
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_addr, 0, sizeof(s_addr));
}

void ym2612_reset(void) {
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_addr, 0, sizeof(s_addr));
}

uint8_t ym2612_read_status(void) {
    /* Bit 7 = busy, bit 6 = timer B overflow, bit 5 = timer A overflow */
    return 0x00;
}

void ym2612_write(int port, uint8_t reg, uint8_t val) {
    if (port < 0 || port > 1) return;
    s_regs[port][reg] = val;
    /* TODO: decode register writes and update FM synthesis state */
}

void ym2612_generate(int16_t *buf, int num_samples) {
    /* TODO: generate audio samples from FM state */
    for (int i = 0; i < num_samples * 2; i++)
        buf[i] = 0;
}
