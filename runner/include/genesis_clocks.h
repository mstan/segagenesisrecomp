/*
 * genesis_clocks.h — Genesis / Mega Drive clock rates (NTSC).
 *
 * These are hardware facts (the documented Model 1 NTSC master clock and its
 * derived divisors), defined here as our own constants so the audio/runtime
 * code needs no emulator headers. Values match real hardware.
 */
#ifndef GENESIS_CLOCKS_H
#define GENESIS_CLOCKS_H

#define GENESIS_MASTER_CLOCK_NTSC     53693175u   /* master crystal, Hz        */
/* PSG/Z80 sample rate = master / (Z80 divider 15 * PSG divider 16) = /240. */
#define GENESIS_PSG_SAMPLE_RATE_NTSC  (GENESIS_MASTER_CLOCK_NTSC / 240u)  /* ~223721 Hz */

#endif /* GENESIS_CLOCKS_H */
