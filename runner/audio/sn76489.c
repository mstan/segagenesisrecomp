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
static int16_t s_raw_scratch[PSG_SCRATCH_SAMPLES];
static int16_t s_aud_scratch[PSG_SCRATCH_SAMPLES],s_aud_raw[PSG_SCRATCH_SAMPLES];
static struct {unsigned mask;int32_t previous,output;} s_aud={15,0,0};
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
    uint8_t  noise_ff;         /* ÷2 tone-divider flip-flop (clocks the LFSR) */
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

static int32_t sn_step_mix(SN76489 *p,int32_t *channels)
{
    int32_t mix = 0;

    for (int i = 0; i < 3; i++) {
        if (--p->tone_counter[i] <= 0) {
            p->tone_counter[i] = p->tone_period[i] ? p->tone_period[i] : 1;
            p->tone_out[i] ^= 1;
        }
        int32_t value=p->tone_out[i]?SN_VOL[p->vol[i]]:-SN_VOL[p->vol[i]];
        mix+=value;if(channels)channels[i]=value;
    }

    if (--p->noise_counter <= 0) {
        p->noise_counter = sn_noise_reload(p);
        /* The noise generator drives an internal /2 flip-flop (exactly like a
         * tone channel's square output); the LFSR is clocked only on that
         * flip-flop's rising edge, i.e. once per TWO counter periods. Without
         * this the LFSR ran twice as fast (noise an octave too high) — the
         * jump-SFX "boop". Matches BlastEm psg.c output_state[3] and clownmdemu
         * psg.c fake_output_bit. */
        p->noise_ff ^= 1;
        if (p->noise_ff) {
            uint16_t fb = (p->noise_ctrl & 0x04)            /* 1 = white */
                        ? ((p->lfsr ^ (p->lfsr >> 3)) & 1)  /* taps bit0 ^ bit3 */
                        : (p->lfsr & 1);                    /* periodic */
            p->lfsr = (uint16_t)((p->lfsr >> 1) | (fb << 15));
            p->noise_out = (uint8_t)(p->lfsr & 1);
        }
    }
    int32_t noise=p->noise_out?SN_VOL[p->vol[3]]:-SN_VOL[p->vol[3]];
    if(channels)channels[3]=noise;
    return mix+noise;
}
static int16_t sn_filter_sample(int32_t mix,int32_t *previous,int32_t *output)
{
    int64_t lp=((int64_t)(mix+*previous)*SN_LPF_SAMPLE_MAGIC+
        (int64_t)*output*SN_LPF_OUTPUT_MAGIC)/65536;
    *previous=mix;int32_t out=(int32_t)lp;
    if(out>32767)out=32767;if(out< -32768)out= -32768;
    *output=out;return (int16_t)out;
}
static int16_t sn_render_sample(SN76489 *p,int16_t *unfiltered)
{
    int32_t mix=sn_step_mix(p,NULL);*unfiltered=(int16_t)mix;
    return sn_filter_sample(mix,&p->lpf_prev,&p->lpf_y);
}
static int16_t sn_render_audition(SN76489 *p,int16_t *raw,int16_t *audition,int16_t *aud_raw)
{
    int32_t channels[4],mix=sn_step_mix(p,channels),selected=0;
    for(unsigned n=0;n<4;n++)if(s_aud.mask&(1u<<n))selected+=channels[n];
    *raw=(int16_t)mix;*aud_raw=(int16_t)selected;
    *audition=sn_filter_sample(selected,&s_aud.previous,&s_aud.output);
    return sn_filter_sample(mix,&p->lpf_prev,&p->lpf_y);
}

#ifndef GENESIS_PSG_SILENT_SPANS
#define GENESIS_PSG_SILENT_SPANS 1
#endif
#if GENESIS_PSG_SILENT_SPANS
/* No writes occur inside an advance interval. Once every channel is muted
 * and the filter has settled exactly to zero, samples are known, but chip
 * counters/phase/noise must still advance for a later unmute or snapshot. */
static unsigned sn_counter_span(int32_t *counter,unsigned period,unsigned samples)
{
    unsigned first=*counter>0?(unsigned)*counter:1u;
    if(samples<first){*counter-=(int32_t)samples;return 0;}
    unsigned rest=samples-first;
    *counter=(int32_t)(period-rest%period);
    return 1u+rest/period;
}
static uint16_t sn_white_step(uint16_t bits)
{return (uint16_t)((bits>>1)|(((bits^(bits>>3))&1u)<<15));}
static uint16_t sn_linear_apply(const uint16_t columns[16],uint16_t bits)
{
    uint16_t out=0;
    for(unsigned i=0;bits;++i,bits>>=1)if(bits&1u)out^=columns[i];
    return out;
}
static uint16_t sn_white_span(uint16_t bits,unsigned steps)
{
    /* A scratch chunk has at most8192 ticks, hence4096 LFSR edges. This is
     * derived immutable math, not chip state; reset/restore needs no epoch. */
    static uint16_t powers[13][16];
    static int ready;
    if(steps<16u){while(steps--)bits=sn_white_step(bits);return bits;}
    if(!ready) {
        for(unsigned i=0;i<16;++i)powers[0][i]=sn_white_step((uint16_t)(1u<<i));
        for(unsigned p=1;p<13;++p)for(unsigned i=0;i<16;++i)
            powers[p][i]=sn_linear_apply(powers[p-1],powers[p-1][i]);
        ready=1;
    }
    for(unsigned p=0;steps;++p,steps>>=1)if(steps&1u)bits=sn_linear_apply(powers[p],bits);
    return bits;
}
static int sn_silent_span(SN76489 *p,int16_t *out,size_t samples)
{
    if(p->vol[0]!=15||p->vol[1]!=15||p->vol[2]!=15||p->vol[3]!=15||
       p->lpf_y||p->lpf_prev||p->noise_ff>1||p->noise_out>1||
       p->tone_out[0]>1||p->tone_out[1]>1||p->tone_out[2]>1)return 0;
    for(unsigned i=0;i<3;++i) {
        unsigned period=p->tone_period[i]?p->tone_period[i]:1u;
        p->tone_out[i]^=(uint8_t)(sn_counter_span(&p->tone_counter[i],period,(unsigned)samples)&1u);
    }
    unsigned edges=sn_counter_span(&p->noise_counter,(unsigned)sn_noise_reload(p),(unsigned)samples);
    unsigned shifts=(edges+(p->noise_ff?0u:1u))/2u;
    p->noise_ff^=(uint8_t)(edges&1u);
    if(shifts) {
        if(p->noise_ctrl&4u)p->lfsr=sn_white_span(p->lfsr,shifts);
        else {
            unsigned rotate=shifts&15u;
            if(rotate)p->lfsr=(uint16_t)((p->lfsr>>rotate)|((uint32_t)p->lfsr<<(16u-rotate)));
        }
        /* A noise-control write resets LFSR without changing noise_out.
         * Preserve that stale output until an actual rising clock edge. */
        p->noise_out=(uint8_t)(p->lfsr&1u);
    }
    memset(out,0,samples*sizeof *out);
    return 1;
}
#endif

void psg_init(void)
{
    sn_reset(&s_sn);
    s_scratch_write = s_scratch_read = 0;
    s_leftover_master_cycles = 0;s_aud.previous=s_aud.output=0;
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
        int silent=0;
#if GENESIS_PSG_SILENT_SPANS
        if((s_aud.mask==15 || (!s_aud.previous && !s_aud.output)) &&
           sn_silent_span(&s_sn,s_scratch+s_scratch_write,chunk))silent=1;
#endif
        if(silent) {
            memset(s_raw_scratch+s_scratch_write,0,chunk*sizeof *s_raw_scratch);
            memset(s_aud_scratch+s_scratch_write,0,chunk*sizeof *s_aud_scratch);
            memset(s_aud_raw+s_scratch_write,0,chunk*sizeof *s_aud_raw);
        } else if(s_aud.mask==15) {
            for(size_t i=0;i<chunk;i++)
                s_scratch[s_scratch_write+i]=sn_render_sample(&s_sn,s_raw_scratch+s_scratch_write+i);
            memcpy(s_aud_scratch+s_scratch_write,s_scratch+s_scratch_write,chunk*sizeof *s_scratch);
            memcpy(s_aud_raw+s_scratch_write,s_raw_scratch+s_scratch_write,chunk*sizeof *s_raw_scratch);
        } else for(size_t i=0;i<chunk;i++)
            s_scratch[s_scratch_write+i]=sn_render_audition(&s_sn,s_raw_scratch+s_scratch_write+i,
                s_aud_scratch+s_scratch_write+i,s_aud_raw+s_scratch_write+i);
        if(s_aud.mask==15){s_aud.previous=s_sn.lpf_prev;s_aud.output=s_sn.lpf_y;}
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
    return psg_render_pair(out,NULL,sample_count);
}

size_t psg_render_pair(int16_t *filtered,int16_t *unfiltered,size_t sample_count)
{
    if ((!filtered && !unfiltered) || sample_count == 0) return 0;
    size_t available = s_scratch_write - s_scratch_read;
    size_t copy = sample_count < available ? sample_count : available;
    if (copy > 0) {
        if(filtered)memcpy(filtered, &s_scratch[s_scratch_read], copy * sizeof(int16_t));
        if(unfiltered)memcpy(unfiltered, &s_raw_scratch[s_scratch_read], copy * sizeof(int16_t));
        s_scratch_read += copy;
    }
    if (s_scratch_read >= s_scratch_write) {
        s_scratch_read = s_scratch_write = 0;
    }
    return copy;
}

void psg_set_audition_channels(unsigned mask){s_aud.mask=mask&15u;}
size_t psg_peek_audition(int16_t *filtered,int16_t *raw,size_t count)
{
    size_t available=s_scratch_write-s_scratch_read;if(count>available)count=available;
    if(filtered)memcpy(filtered,s_aud_scratch+s_scratch_read,count*sizeof *filtered);
    if(raw)memcpy(raw,s_aud_raw+s_scratch_read,count*sizeof *raw);return count;
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
    if(fwrite(&s_aud,sizeof s_aud,1,f)!=1)return 0;
    return 1;
}

#include "cosim.h"
/* Full PSG state hashed for benchmark regression and differential co-sim
 * (field-by-field to dodge struct padding). Includes the noise LFSR + counters
 * + sample-clock leftover — the surface frame_snapshots.c zeroes. */
uint64_t psg_cosim_hash(void)
{
    if (!s_inited) psg_init();
    uint64_t h = cosim_fnv_init();
    for (int i = 0; i < 3; i++) h = cosim_fnv_u16(h, s_sn.tone_period[i]);
    for (int i = 0; i < 3; i++) h = cosim_fnv_u32(h, (uint32_t)s_sn.tone_counter[i]);
    for (int i = 0; i < 3; i++) h = cosim_fnv_u8(h, s_sn.tone_out[i]);
    for (int i = 0; i < 4; i++) h = cosim_fnv_u8(h, s_sn.vol[i]);
    h = cosim_fnv_u8(h, s_sn.noise_ctrl);
    h = cosim_fnv_u32(h, (uint32_t)s_sn.noise_counter);
    h = cosim_fnv_u16(h, s_sn.lfsr);
    h = cosim_fnv_u8(h, s_sn.noise_ff);
    h = cosim_fnv_u8(h, s_sn.noise_out);
    h = cosim_fnv_u8(h, s_sn.latch);
    h = cosim_fnv_u32(h, (uint32_t)s_sn.lpf_y);
    h = cosim_fnv_u32(h, (uint32_t)s_sn.lpf_prev);
    h = cosim_fnv_u32(h, s_leftover_master_cycles);
    return h;
}

#ifdef GENESIS_COSIM
#include <stdio.h>
/* Human-readable PSG field dump for co-sim drill (which field diverges?). */
int psg_cosim_dump(char *buf, int cap) {
    if (!s_inited) psg_init();
    return snprintf(buf, (size_t)cap,
        "tone_p %u %u %u tone_c %d %d %d tone_o %u %u %u "
        "vol %u %u %u %u noise_ctrl %u noise_c %d lfsr %u noise_ff %u noise_o %u "
        "latch %u leftover %u",
        s_sn.tone_period[0], s_sn.tone_period[1], s_sn.tone_period[2],
        s_sn.tone_counter[0], s_sn.tone_counter[1], s_sn.tone_counter[2],
        s_sn.tone_out[0], s_sn.tone_out[1], s_sn.tone_out[2],
        s_sn.vol[0], s_sn.vol[1], s_sn.vol[2], s_sn.vol[3],
        s_sn.noise_ctrl, s_sn.noise_counter, s_sn.lfsr, s_sn.noise_ff,
        s_sn.noise_out, s_sn.latch, s_leftover_master_cycles);
}
#endif /* GENESIS_COSIM */

size_t psg_rb_save(void *dst, size_t cap)
{
    if (!s_inited) psg_init();
    uint32_t pend = (uint32_t)(s_scratch_write - s_scratch_read);
    size_t need = sizeof s_sn + sizeof s_leftover_master_cycles + sizeof s_aud + sizeof pend +
                  (size_t)pend * 4u * sizeof(int16_t);
    if (!dst) return need;
    if (cap < need) return 0;
    uint8_t *o = (uint8_t *)dst;
    memcpy(o, &s_sn, sizeof s_sn); o += sizeof s_sn;
    memcpy(o, &s_leftover_master_cycles, sizeof s_leftover_master_cycles);
    o += sizeof s_leftover_master_cycles;
    memcpy(o,&s_aud,sizeof s_aud);o+=sizeof s_aud;
    memcpy(o, &pend, sizeof pend); o += sizeof pend;
    memcpy(o, &s_scratch[s_scratch_read], (size_t)pend * sizeof(int16_t));
    o += (size_t)pend * sizeof(int16_t);
    memcpy(o, &s_raw_scratch[s_scratch_read], (size_t)pend * sizeof(int16_t));o+=(size_t)pend*sizeof(int16_t);
    memcpy(o,s_aud_scratch+s_scratch_read,(size_t)pend*sizeof(int16_t));o+=(size_t)pend*sizeof(int16_t);
    memcpy(o,s_aud_raw+s_scratch_read,(size_t)pend*sizeof(int16_t));return need;
}

int psg_rb_load(const void *src, size_t len)
{
    if (!s_inited) psg_init();
    const uint8_t *i = (const uint8_t *)src;
    uint32_t pend = 0;
    size_t fixed = sizeof s_sn + sizeof s_leftover_master_cycles + sizeof s_aud + sizeof pend;
    if (!src || len < fixed) return 0;
    memcpy(&pend,i+fixed-sizeof pend,sizeof pend);
    unsigned mask=0;memcpy(&mask,i+sizeof s_sn+sizeof s_leftover_master_cycles,sizeof mask);if(mask>15)return 0;
    if (pend > PSG_SCRATCH_SAMPLES || len != fixed + (size_t)pend * 4u * sizeof(int16_t)) return 0;
    memcpy(&s_sn, i, sizeof s_sn); i += sizeof s_sn;
    memcpy(&s_leftover_master_cycles, i, sizeof s_leftover_master_cycles);
    i+=sizeof s_leftover_master_cycles;memcpy(&s_aud,i,sizeof s_aud);i+=sizeof s_aud+sizeof pend;
    memcpy(s_scratch, i, (size_t)pend * sizeof(int16_t));
    i += (size_t)pend * sizeof(int16_t);
    memcpy(s_raw_scratch,i,(size_t)pend*sizeof(int16_t));i+=(size_t)pend*sizeof(int16_t);
    memcpy(s_aud_scratch,i,(size_t)pend*sizeof(int16_t));i+=(size_t)pend*sizeof(int16_t);
    memcpy(s_aud_raw,i,(size_t)pend*sizeof(int16_t));
    s_scratch_read = 0;
    s_scratch_write = pend;
    return 1;
}

int psg_load_state(FILE *f)
{
    if(!s_inited)psg_init();SN76489 state;uint32_t leftover;
    unsigned mask;int32_t previous,output;
    if(fread(&state,sizeof state,1,f)!=1 || fread(&leftover,sizeof leftover,1,f)!=1 ||
       leftover>=PSG_SAMPLE_DIVISOR_MASTER || fread(&mask,sizeof mask,1,f)!=1 || mask>15 ||
       fread(&previous,sizeof previous,1,f)!=1 || fread(&output,sizeof output,1,f)!=1)return 0;
    s_sn=state;s_leftover_master_cycles=leftover;s_aud.mask=mask;
    s_aud.previous=previous;s_aud.output=output;s_scratch_read=s_scratch_write=0;return 1;
}
