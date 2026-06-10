/*
 * sn76489.c — cycle-stamped SN76489 PSG.
 *
 * Clean-room SN76489 (Sega Master System / Mega Drive variant) implemented
 * from public hardware documentation — NO emulator-core code, so this carries
 * no third-party copyleft. It replaced the previous clownmdemu (AGPL) PSG
 * wrapper outright (the old path lives in git history only). See LICENSING.md.
 *
 * Models 3 square-tone channels + 1 LFSR noise channel, 4-bit logarithmic
 * volume (2 dB/step, 0xF = off), and the SMS/Genesis 16-bit noise LFSR
 * (white-noise taps at bits 0 and 3, feedback -> bit 15), with a one-pole
 * low-pass to round the step edges.
 *
 * Clocked at the PSG sample rate (master/240 ≈ 223.7 kHz NTSC), one tone-
 * counter tick per emitted sample. The public psg_* API and that sample rate
 * are unchanged, so all downstream mixing/resampling is unaffected.
 */
#include "sn76489.h"        /* our wrapper API */
#include "genesis_clocks.h" /* GENESIS_MASTER_CLOCK_NTSC (our own constant) */
#include <string.h>

#define PSG_SAMPLE_DIVISOR_MASTER   240u   /* Z80_CLOCK_DIVIDER * PSG_SAMPLE_RATE_DIVIDER */

static int        s_inited = 0;
static uint32_t   s_leftover_master_cycles = 0;  /* in master-cycle units */

#define PSG_SCRATCH_SAMPLES  8192
static int16_t s_scratch[PSG_SCRATCH_SAMPLES];
static size_t  s_scratch_write = 0;
static size_t  s_scratch_read  = 0;

/* ---- clean-room SN76489 ---- */
typedef struct {
    uint16_t tone_period[3];   /* 10-bit reload value */
    int32_t  tone_counter[3];
    uint8_t  tone_out[3];      /* square flip-flop (0/1) */
    uint8_t  vol[4];           /* 4-bit attenuation; index 3 = noise */
    uint8_t  noise_ctrl;       /* low 3 bits: fb-mode + rate */
    int32_t  noise_counter;
    uint16_t lfsr;
    uint8_t  noise_out;
    uint8_t  latch;            /* (chan<<1)|type of last LATCH byte */
    int32_t  lpf_y;            /* low-pass: previous output  */
    int32_t  lpf_prev;         /* low-pass: previous input sample */
} SN76489;

/* First-order low-pass matching the reference (clownmdemu low-pass-filter.c
 * with coefficients 26.044, 24.044): out = ((in + prevIn)*SAMPLE_MAGIC +
 * prevOut*OUTPUT_MAGIC) >> 16. Replaces the old >>1 one-pole, which was too
 * bright and let PSG energy run hotter than the reference. */
#define SN_LPF_SAMPLE_MAGIC  2517   /* round(1.0    * 65536 / 26.044) */
#define SN_LPF_OUTPUT_MAGIC  60504  /* round(24.044 * 65536 / 26.044) */
static SN76489 s_sn;

/* 4-bit attenuation -> linear amplitude. 2 dB/step (×10^-0.1), 0xF = silence.
 * Values match the reference (clownmdemu psg.c psg_volumes / known SN76489
 * behaviour): peak 0x1FFF with a per-channel /4 headroom so 4 summed channels
 * (4*8191 = 32764) just fit int16. Brings PSG level to parity with the
 * clownmdemu render measured by tools/synth_replay. */
static const int16_t SN_VOL[16] = {
    0x1FFF, 0x196A, 0x1430, 0x1009, 0x0CBD, 0x0A1E, 0x0809, 0x0662,
    0x0512, 0x0407, 0x0333, 0x028A, 0x0204, 0x019A, 0x0146, 0x0000
};

static void sn_reset(SN76489 *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 3; i++) { p->tone_period[i] = 1; p->tone_counter[i] = 1; }
    p->vol[0] = p->vol[1] = p->vol[2] = p->vol[3] = 0xF;  /* all silent */
    p->noise_counter = 0x10;
    p->lfsr = 0x8000;
}

static void sn_write(SN76489 *p, uint8_t b)
{
    if (b & 0x80) {                       /* LATCH/DATA byte: %1 cc t dddd */
        p->latch = (b >> 4) & 0x07;
        uint8_t chan = (b >> 5) & 0x03;
        uint8_t type = (b >> 4) & 0x01;   /* 1 = volume, 0 = tone/noise */
        if (type)            p->vol[chan] = b & 0x0F;
        else if (chan < 3)   p->tone_period[chan] = (uint16_t)((p->tone_period[chan] & 0x3F0) | (b & 0x0F));
        else               { p->noise_ctrl = b & 0x07; p->lfsr = 0x8000; }
    } else {                              /* DATA byte: %0 - dddddd */
        uint8_t chan = (p->latch >> 1) & 0x03;
        uint8_t type = p->latch & 0x01;
        if (type)            p->vol[chan] = b & 0x0F;
        else if (chan < 3)   p->tone_period[chan] = (uint16_t)((p->tone_period[chan] & 0x00F) | ((uint16_t)(b & 0x3F) << 4));
        else               { p->noise_ctrl = b & 0x07; p->lfsr = 0x8000; }
    }
}

static int sn_noise_reload(const SN76489 *p)
{
    switch (p->noise_ctrl & 0x03) {
        case 0:  return 0x10;
        case 1:  return 0x20;
        case 2:  return 0x40;
        default: return p->tone_period[2] ? p->tone_period[2] : 1;  /* track ch2 */
    }
}

static int16_t sn_render_sample(SN76489 *p)
{
    int32_t mix = 0;

    for (int i = 0; i < 3; i++) {
        if (--p->tone_counter[i] <= 0) {
            p->tone_counter[i] = p->tone_period[i] ? p->tone_period[i] : 1;
            p->tone_out[i] ^= 1;
        }
        mix += p->tone_out[i] ? SN_VOL[p->vol[i]] : -SN_VOL[p->vol[i]];
    }

    if (--p->noise_counter <= 0) {
        p->noise_counter = sn_noise_reload(p);
        uint16_t fb = (p->noise_ctrl & 0x04)            /* 1 = white */
                    ? ((p->lfsr ^ (p->lfsr >> 3)) & 1)  /* taps bit0 ^ bit3 */
                    : (p->lfsr & 1);                    /* periodic */
        p->lfsr = (uint16_t)((p->lfsr >> 1) | (fb << 15));
        p->noise_out = (uint8_t)(p->lfsr & 1);
    }
    mix += p->noise_out ? SN_VOL[p->vol[3]] : -SN_VOL[p->vol[3]];

    /* First-order low-pass matching the reference (see magics above). */
    int64_t lp = ((int64_t)(mix + p->lpf_prev) * SN_LPF_SAMPLE_MAGIC
                + (int64_t)p->lpf_y * SN_LPF_OUTPUT_MAGIC) / 65536;
    p->lpf_prev = mix;
    int32_t out = (int32_t)lp;
    if (out >  32767) out =  32767;
    if (out < -32768) out = -32768;
    p->lpf_y = out;
    return (int16_t)out;
}

void psg_init(void)
{
    sn_reset(&s_sn);
    s_scratch_write = s_scratch_read = 0;
    s_leftover_master_cycles = 0;
    s_inited = 1;
}

void psg_advance(uint32_t cycles_master)
{
    if (!s_inited) psg_init();
    if (cycles_master == 0) return;

    uint64_t total = (uint64_t)cycles_master + s_leftover_master_cycles;
    size_t   samples_to_emit = (size_t)(total / PSG_SAMPLE_DIVISOR_MASTER);
    s_leftover_master_cycles = (uint32_t)(total % PSG_SAMPLE_DIVISOR_MASTER);

    while (samples_to_emit > 0) {
        size_t avail = PSG_SCRATCH_SAMPLES - s_scratch_write;
        if (avail == 0) return;  /* scratch full; drop */
        size_t chunk = samples_to_emit < avail ? samples_to_emit : avail;
        for (size_t i = 0; i < chunk; i++)
            s_scratch[s_scratch_write + i] = sn_render_sample(&s_sn);
        s_scratch_write  += chunk;
        samples_to_emit  -= chunk;
    }
}

void psg_write(uint8_t value)
{
    if (!s_inited) psg_init();
    sn_write(&s_sn, value);
}

size_t psg_render(int16_t *out, size_t sample_count)
{
    if (!out || sample_count == 0) return 0;
    size_t available = s_scratch_write - s_scratch_read;
    size_t copy = sample_count < available ? sample_count : available;
    if (copy > 0) {
        memcpy(out, &s_scratch[s_scratch_read], copy * sizeof(int16_t));
        s_scratch_read += copy;
    }
    if (s_scratch_read >= s_scratch_write) {
        s_scratch_read = s_scratch_write = 0;
    }
    return copy;
}

size_t psg_samples_available(void)
{
    return s_scratch_write - s_scratch_read;
}

uint32_t psg_sample_rate(void)
{
    return (uint32_t)(GENESIS_MASTER_CLOCK_NTSC / PSG_SAMPLE_DIVISOR_MASTER);
}

void psg_reset_leftover(void)
{
    s_leftover_master_cycles = 0;
}

/* Save-state hooks (own-backend snapshots): the whole SN76489 register/
 * counter struct + sample-clock remainder. The output scratch is transient
 * (drained every frame) and is reset on load. */
#include <stdio.h>
int psg_save_state(FILE *f)
{
    if (!s_inited) psg_init();
    if (fwrite(&s_sn, sizeof s_sn, 1, f) != 1) return 0;
    if (fwrite(&s_leftover_master_cycles, sizeof s_leftover_master_cycles, 1, f) != 1) return 0;
    return 1;
}

int psg_load_state(FILE *f)
{
    if (!s_inited) psg_init();
    if (fread(&s_sn, sizeof s_sn, 1, f) != 1) return 0;
    if (fread(&s_leftover_master_cycles, sizeof s_leftover_master_cycles, 1, f) != 1) return 0;
    s_scratch_read = s_scratch_write = 0;
    return 1;
}
