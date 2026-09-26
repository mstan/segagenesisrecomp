/* rb_state.c -- one section table for rollback snapshots and digests.
 * See rb_state.h for the contract and the digest domain. */
#include "rb_state.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cosim.h"                    /* cosim_fnv_* */
#include "game_spec.h"
#include "sim_step.h"
#include "include/genesis_runtime.h"  /* g_cpu, g_ram, M68KState */
#include "video/genesis_machine.h"
#include "audio/ym2612.h"
#include "audio/sn76489.h"
#include "audio/event_queue.h"

/* ---- drift guards -----------------------------------------------------------
 * These pin the layouts the sections serialize. A failing assert means a
 * field was added to (or removed from) a structure a rollback snapshot and
 * digest copy wholesale: check that the new field is simulation state that
 * belongs in the snapshot (it will be, by construction, for these raw
 * images) and that it holds no host pointer; if it does, scrub it the way
 * machine_rb_save does. Then update the size here. */
_Static_assert(sizeof(M68KState) == 76, "M68KState changed: review rb_state cpu section");
_Static_assert(sizeof(GenesisSimInput) == 16, "GenesisSimInput changed: review rb_state input section");
_Static_assert(sizeof(AudioEvent) == 8, "AudioEvent changed: review audio_event_rb_*");
_Static_assert(sizeof(g_ram) == 0x10000, "work RAM size changed");
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
/* 64-bit layouts (the host pointers inside are scrubbed by machine_rb_save). */
_Static_assert(sizeof(GVDP) == 65824, "GVDP changed: review machine_rb_save (pointers) and rb_state");
_Static_assert(sizeof(GenesisBus) == 24664, "GenesisBus changed: review machine_rb_save and rb_state");
_Static_assert(sizeof(GenesisMachine) == 90576, "GenesisMachine changed: review machine_rb_save and rb_state");
#endif

/* ---- sections ---------------------------------------------------------------- */

static size_t sec_cpu_save(void *dst, size_t cap)
{
    if (!dst) return sizeof g_cpu;
    if (cap < sizeof g_cpu) return 0;
    memcpy(dst, &g_cpu, sizeof g_cpu);
    return sizeof g_cpu;
}
static int sec_cpu_load(const void *src, size_t len)
{
    if (len != sizeof g_cpu) return 0;
    memcpy(&g_cpu, src, sizeof g_cpu);
    return 1;
}

static size_t sec_ram_save(void *dst, size_t cap)
{
    if (!dst) return sizeof g_ram;
    if (cap < sizeof g_ram) return 0;
    memcpy(dst, g_ram, sizeof g_ram);
    return sizeof g_ram;
}
static int sec_ram_load(const void *src, size_t len)
{
    if (len != sizeof g_ram) return 0;
    memcpy(g_ram, src, sizeof g_ram);
    return 1;
}

typedef struct RbInputImage {
    GenesisSimInput in;
    uint32_t        ticks;
} RbInputImage;

static size_t sec_input_save(void *dst, size_t cap)
{
    RbInputImage im;
    if (!dst) return sizeof im;
    if (cap < sizeof im) return 0;
    memset(&im, 0, sizeof im);
    im.in = *genesis_sim_input();
    im.ticks = genesis_sim_tick_count();
    memcpy(dst, &im, sizeof im);
    return sizeof im;
}
static int sec_input_load(const void *src, size_t len)
{
    RbInputImage im;
    if (len != sizeof im) return 0;
    memcpy(&im, src, sizeof im);
    genesis_sim_input_restore(&im.in);
    genesis_sim_set_tick_count(im.ticks);
    return 1;
}

static size_t sec_game_save(void *dst, size_t cap)
{
    if (!g_game_spec.rb_state_save) return 0;
    return g_game_spec.rb_state_save(dst, cap);
}
static int sec_game_load(const void *src, size_t len)
{
    if (!g_game_spec.rb_state_load) return len == 0;
    return g_game_spec.rb_state_load(src, len);
}

static size_t sec_fm_save(void *dst, size_t cap)  { return ym2612_rb_save(dst, cap); }
static int    sec_fm_load(const void *s, size_t n) { return ym2612_rb_load(s, n); }
static size_t sec_psg_save(void *dst, size_t cap) { return psg_rb_save(dst, cap); }
static int    sec_psg_load(const void *s, size_t n) { return psg_rb_load(s, n); }
static size_t sec_evq_save(void *dst, size_t cap) { return audio_event_rb_save(dst, cap); }
static int    sec_evq_load(const void *s, size_t n) { return audio_event_rb_load(s, n); }

/* Default digest: the section's own bytes into its one partition. */
static void digest_bytes(int part, const uint8_t *b, size_t n, GenesisRbDigest *d)
{
    d->part[part] = cosim_fnv_bytes(d->part[part], b, n);
}

static void digest_exec(int part, const uint8_t *b, size_t n, GenesisRbDigest *d)
{
    (void)b; (void)n;
    d->part[part] = glue_rb_exec_digest(d->part[part]);
}

/* The machine image splits three ways so a fork names the chip. */
static void digest_machine(int part, const uint8_t *b, size_t n, GenesisRbDigest *d)
{
    (void)part;
    const size_t vo = offsetof(GenesisMachine, vdp), vn = sizeof(GVDP);
    const size_t bo = offsetof(GenesisMachine, bus), bn = sizeof(GenesisBus);
    const size_t zo = offsetof(GenesisMachine, z80);
    if (n < sizeof(GenesisMachine)) return;
    d->part[GENESIS_RB_PART_VDP] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_VDP], b + vo, vn);
    d->part[GENESIS_RB_PART_BUS] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_BUS], b + bo, bn);
    /* z80 core + the machine's cycle carry/master clock + the tail
     * (YM timer clock, DMA stall, recompiled Z80): everything after the bus. */
    d->part[GENESIS_RB_PART_Z80] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_Z80], b + zo, n - zo);
    /* Bytes before vdp (none today) belong to the VDP partition. */
    if (vo) d->part[GENESIS_RB_PART_VDP] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_VDP], b, vo);
    /* Padding between the members is part of the copied image: fold it too,
     * so "digest = snapshot bytes" holds exactly. */
    if (bo > vo + vn)
        d->part[GENESIS_RB_PART_VDP] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_VDP], b + vo + vn, bo - (vo + vn));
    if (zo > bo + bn)
        d->part[GENESIS_RB_PART_BUS] = cosim_fnv_bytes(d->part[GENESIS_RB_PART_BUS], b + bo + bn, zo - (bo + bn));
}

typedef struct RbSection {
    const char *name;
    int part;
    size_t (*save)(void *dst, size_t cap);         /* dst NULL = bytes needed */
    int    (*load)(const void *src, size_t len);
    void   (*digest)(int part, const uint8_t *b, size_t n, GenesisRbDigest *d);
} RbSection;

static const RbSection k_sections[] = {
    { "exec",    GENESIS_RB_PART_EXEC,  glue_rb_exec_save,  glue_rb_exec_load,  digest_exec    },
    { "cpu",     GENESIS_RB_PART_CPU,   sec_cpu_save,       sec_cpu_load,       digest_bytes   },
    { "sched",   GENESIS_RB_PART_SCHED, glue_rb_sched_save, glue_rb_sched_load, digest_bytes   },
    { "ram",     GENESIS_RB_PART_RAM,   sec_ram_save,       sec_ram_load,       digest_bytes   },
    { "machine", GENESIS_RB_PART_VDP,   machine_rb_save,    machine_rb_load,    digest_machine },
    { "fm",      GENESIS_RB_PART_FM,    sec_fm_save,        sec_fm_load,        digest_bytes   },
    { "psg",     GENESIS_RB_PART_PSG,   sec_psg_save,       sec_psg_load,       digest_bytes   },
    { "evq",     GENESIS_RB_PART_EVQ,   sec_evq_save,       sec_evq_load,       digest_bytes   },
    { "input",   GENESIS_RB_PART_INPUT, sec_input_save,     sec_input_load,     digest_bytes   },
    { "game",    GENESIS_RB_PART_GAME,  sec_game_save,      sec_game_load,      digest_bytes   },
};
#define RB_NSEC (sizeof k_sections / sizeof k_sections[0])

static const char *const k_part_names[GENESIS_RB_PART_COUNT] = {
    "exec", "cpu", "sched", "ram", "vdp", "bus", "z80", "fm", "psg", "evq", "input", "game"
};

const char *genesis_rb_part_name(int part)
{
    return (part >= 0 && part < GENESIS_RB_PART_COUNT) ? k_part_names[part] : "?";
}

/* ---- blob framing ----------------------------------------------------------
 * "GRB1" u32 nsec, then per section: u32 len, len bytes. */
static const char k_magic[4] = { 'G', 'R', 'B', '1' };

size_t genesis_rb_bound(void)
{
    size_t n = sizeof k_magic + sizeof(uint32_t);
    for (size_t i = 0; i < RB_NSEC; i++)
        n += sizeof(uint32_t) + k_sections[i].save(NULL, 0);
    return n;
}

size_t genesis_rb_save(void *dst, size_t cap)
{
    uint8_t *o = (uint8_t *)dst, *end = o + cap;
    uint32_t nsec = (uint32_t)RB_NSEC;
    if (!dst || cap < sizeof k_magic + sizeof nsec) return 0;
    memcpy(o, k_magic, sizeof k_magic); o += sizeof k_magic;
    memcpy(o, &nsec, sizeof nsec);      o += sizeof nsec;
    for (size_t i = 0; i < RB_NSEC; i++) {
        size_t need = k_sections[i].save(NULL, 0);
        if ((size_t)(end - o) < sizeof(uint32_t) + need) return 0;
        size_t got = need ? k_sections[i].save(o + sizeof(uint32_t), need) : 0;
        if (need && got == 0) {
            fprintf(stderr, "[rb_state] save: section %s failed\n", k_sections[i].name);
            return 0;
        }
        uint32_t len = (uint32_t)got;
        memcpy(o, &len, sizeof len);
        o += sizeof len + got;
    }
    return (size_t)(o - (uint8_t *)dst);
}

/* Walk the framing; fills off/len per section. 1 when well-formed. */
static int rb_frame(const uint8_t *b, size_t len, size_t off[RB_NSEC], size_t sl[RB_NSEC])
{
    uint32_t nsec;
    size_t p = sizeof k_magic + sizeof nsec;
    if (!b || len < p || memcmp(b, k_magic, sizeof k_magic)) return 0;
    memcpy(&nsec, b + sizeof k_magic, sizeof nsec);
    if (nsec != RB_NSEC) return 0;
    for (size_t i = 0; i < RB_NSEC; i++) {
        uint32_t l;
        if (len - p < sizeof l) return 0;
        memcpy(&l, b + p, sizeof l);
        p += sizeof l;
        if (len - p < l) return 0;
        off[i] = p; sl[i] = l;
        p += l;
    }
    return p == len;
}

/* Negative control for the probe (never set in play): skip restoring one
 * named section, so a probe run proves it can see a missing carrier. */
static const char *rb_skip_section(void)
{
    static int read;
    static const char *v;
    if (!read) { read = 1; v = getenv("GENESIS_RB_NEGCTL_SKIP"); if (v && !*v) v = NULL;
                 if (v) fprintf(stderr, "[rb_state] NEGATIVE CONTROL: section '%s' is not restored\n", v); }
    return v;
}

int genesis_rb_load(const void *src, size_t len)
{
    size_t off[RB_NSEC], sl[RB_NSEC];
    const uint8_t *b = (const uint8_t *)src;
    const char *skip = rb_skip_section();
    if (!rb_frame(b, len, off, sl)) {
        fprintf(stderr, "[rb_state] load: malformed blob (%zu bytes)\n", len);
        return 0;
    }
    for (size_t i = 0; i < RB_NSEC; i++) {
        if (sl[i] == 0 && k_sections[i].save(NULL, 0) == 0) continue;   /* empty game */
        if (skip && !strcmp(skip, k_sections[i].name)) continue;
        if (!k_sections[i].load(b + off[i], sl[i])) {
            fprintf(stderr, "[rb_state] load: section %s rejected (%zu bytes)\n",
                    k_sections[i].name, sl[i]);
            return 0;
        }
    }
    return 1;
}

/* ---- digest ----------------------------------------------------------------- */

static uint8_t *s_dig_buf;
static size_t   s_dig_cap;

static void digest_blob(const uint8_t *b, const size_t off[RB_NSEC], const size_t sl[RB_NSEC],
                        GenesisRbDigest *d)
{
    for (int p = 0; p < GENESIS_RB_PART_COUNT; p++) d->part[p] = cosim_fnv_init();
    for (size_t i = 0; i < RB_NSEC; i++)
        k_sections[i].digest(k_sections[i].part, b + off[i], sl[i], d);
    d->master = cosim_fnv_init();
    for (int p = 0; p < GENESIS_RB_PART_COUNT; p++) d->master = cosim_fold(d->master, d->part[p]);
}

void genesis_rb_digest(GenesisRbDigest *out)
{
    size_t off[RB_NSEC], sl[RB_NSEC];
    size_t need = genesis_rb_bound();
    memset(out, 0, sizeof *out);
    if (s_dig_cap < need) {
        uint8_t *nb = (uint8_t *)realloc(s_dig_buf, need);
        if (!nb) return;
        s_dig_buf = nb; s_dig_cap = need;
    }
    size_t n = genesis_rb_save(s_dig_buf, s_dig_cap);
    if (!n || !rb_frame(s_dig_buf, n, off, sl)) return;
    digest_blob(s_dig_buf, off, sl, out);
}

uint32_t genesis_rb_fold32(uint64_t h)
{
    return (uint32_t)(h ^ (h >> 32));
}

/* ---- mutation self-test ------------------------------------------------------ */

/* The byte to flip inside a section: one that its loader accepts and that is
 * state (not framing inside the section). */
static size_t mutate_offset(size_t sec, size_t len)
{
    const char *n = k_sections[sec].name;
    if (!strcmp(n, "evq")) return len > 8 ? 4 : (size_t)-1;          /* first event's stamp */
    if (!strcmp(n, "fm"))  return len > 16 ? 8 : (size_t)-1;          /* inside the ymfm blob */
    if (!strcmp(n, "psg")) return 0;
    if (!strcmp(n, "exec")) return (size_t)-1;                        /* host addresses */
    if (!strcmp(n, "machine")) return offsetof(GenesisMachine, vdp) + 0x100;  /* VRAM byte */
    return len / 2;
}

int genesis_rb_selftest(void)
{
    int fails = 0;
    size_t cap = genesis_rb_bound();
    uint8_t *orig = (uint8_t *)malloc(cap), *mut = (uint8_t *)malloc(cap);
    size_t off[RB_NSEC], sl[RB_NSEC];
    GenesisRbDigest d0, d1, d2;
    size_t n = orig ? genesis_rb_save(orig, cap) : 0;
    if (!n || !mut || !rb_frame(orig, n, off, sl)) {
        fprintf(stderr, "[rb_selftest] FAIL: could not snapshot\n");
        free(orig); free(mut);
        return 1;
    }
    genesis_rb_digest(&d0);
    for (size_t i = 0; i < RB_NSEC; i++) {
        size_t at = sl[i] ? mutate_offset(i, sl[i]) : (size_t)-1;
        if (at == (size_t)-1 || at >= sl[i]) {
            fprintf(stderr, "[rb_selftest] %-8s len=%-7zu skipped (%s)\n", k_sections[i].name, sl[i],
                    sl[i] ? "no mutable byte: host addresses or empty" : "empty");
            continue;
        }
        memcpy(mut, orig, n);
        mut[off[i] + at] ^= 0x5A;
        int ok_load = genesis_rb_load(mut, n);
        genesis_rb_digest(&d1);
        int changed = 0, leaked = 0;
        for (int p = 0; p < GENESIS_RB_PART_COUNT; p++) {
            if (d1.part[p] == d0.part[p]) continue;
            int own = p == k_sections[i].part ||
                      (i == 4 && (p == GENESIS_RB_PART_VDP || p == GENESIS_RB_PART_BUS ||
                                  p == GENESIS_RB_PART_Z80));
            if (own) changed = 1; else leaked = 1;
        }
        int restored = genesis_rb_load(orig, n);
        genesis_rb_digest(&d2);
        int back = restored && d2.master == d0.master &&
                   !memcmp(d2.part, d0.part, sizeof d0.part);
        int ok = ok_load && changed && !leaked && back && d1.master != d0.master;
        if (!ok) fails++;
        fprintf(stderr, "[rb_selftest] %-8s len=%-7zu flip@%-6zu load=%d changed=%d leaked=%d "
                "restored=%d %s\n", k_sections[i].name, sl[i], at, ok_load, changed, leaked,
                back, ok ? "OK" : "FAIL");
    }
    free(orig); free(mut);
    fprintf(stderr, "[rb_selftest] %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}
