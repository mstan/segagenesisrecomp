/*
 * ym2612.h — Yamaha YM2612 FM synthesis chip interface.
 * 6 FM channels, 4-operator, OPN2 variant.
 * Accessed via $A04000/$A04001 (port 0) and $A04002/$A04003 (port 1).
 */
#pragma once
#include <stdint.h>

/* YM2612 register banks: two banks of 256 registers */
#define YM2612_REG_COUNT 512

void    ym2612_init(int sample_rate);
void    ym2612_write(int port, uint8_t reg, uint8_t val);
uint8_t ym2612_read_status(void);
/* Generate 'num_samples' stereo int16 pairs into buf */
void    ym2612_generate(int16_t *buf, int num_samples);
void    ym2612_reset(void);
