/*
 * psg.h — Texas Instruments SN76489 PSG (Programmable Sound Generator) interface.
 * 3 square-wave tone channels + 1 noise channel.
 * Accessed via $C00011 (write-only, byte writes).
 * Shared by the 68000 main CPU and the Z80 sound CPU on Genesis.
 */
#pragma once
#include <stdint.h>

void psg_init(int sample_rate);
void psg_write(uint8_t val);
/* Generate 'num_samples' mono int16 samples into buf */
void psg_generate(int16_t *buf, int num_samples);
void psg_reset(void);
