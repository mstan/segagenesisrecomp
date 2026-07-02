/* cosim_visible.c — guest-visible-surface hasher for pairing #2 (recomp own-
 * backend vs clownmdemu oracle). Compiled only under GENESIS_COSIM.
 *
 * Unlike pairing #1 (same-backend, bit-exact FULL state), pairing #2 compares
 * two INDEPENDENT implementations. Raw full-state equality is a known-doomed
 * test: timing-sensitive WRAM scratch (busy-wait counters, H/V-derived values)
 * diverges early even when the program is correct (project memory: RKA WRAM
 * forked at frame 76 with correct visuals). So this hashes the guest-VISIBLE
 * surface that MUST match if the recomp is faithful — normalized identically
 * for both backends by the existing frame_snapshots.c accessors:
 *   cpu68k : D/A/SR (PC excluded — a marker on the recompiled side)
 *   ram    : 64 KB WRAM (INCLUDED but expected to diverge on scratch; the
 *            coordinator compares it separately so it doesn't mask real bugs)
 *   z80    : Z80 register file        z80ram : 8 KB Z80 RAM
 *   vdp    : VDP semantic regs + VRAM/CRAM/VSRAM (the DISPLAY output)
 *   evq    : the FM/PSG write STREAM (values+order) — the audio comparable;
 *            NOT the chip internals (ymfm vs clown FM are different synths) and
 *            NOT stamps (timing model differs).
 * The fm/psg/timing/handshake sub-hash slots are unused (0) in visible mode.
 */
#include "cosim.h"
#include "frame_record.h"   /* M68KRegSnap / Z80RegSnap / VdpSnap (normalized)   */

/* frame_snapshots.c accessors (backend-branched inside). ClownMDEmu is opaque
 * on the own-backend build (passed NULL there). */
struct ClownMDEmu;
extern void m68k_snapshot(M68KRegSnap *out);
extern void z80_snapshot (Z80RegSnap *out, struct ClownMDEmu *emu);
extern void vdp_snapshot (VdpSnap *out,   struct ClownMDEmu *emu);
extern void wram_snapshot(uint8_t out[0x10000], struct ClownMDEmu *emu);

/* Cumulative FM/PSG write-stream hash (values+order, stamp-independent) + count,
 * from the shared audio_event_push history ring (event_queue.c). */
extern uint64_t audio_event_cosim_stream_hash(uint64_t *out_count);

#if OWN_BACKEND
#  define COSIM_EMU NULL
#else
extern struct ClownMDEmu g_clownmdemu;
#  define COSIM_EMU (&g_clownmdemu)
#endif

static uint64_t hash_cpu_visible(void) {
    M68KRegSnap s; m68k_snapshot(&s);
    uint64_t h = cosim_fnv_init();
    for (int i = 0; i < 8; i++) h = cosim_fnv_u32(h, s.D[i]);
    for (int i = 0; i < 8; i++) h = cosim_fnv_u32(h, s.A[i]);
    h = cosim_fnv_u16(h, s.SR);
    h = cosim_fnv_u32(h, s.USP);
    return h;   /* PC excluded (marker on the recompiled backend) */
}

static uint64_t hash_wram_visible(void) {
    static uint8_t wram[0x10000];
    wram_snapshot(wram, COSIM_EMU);
    return cosim_fnv_bytes(cosim_fnv_init(), wram, sizeof wram);
}

static uint64_t hash_z80_visible(Z80RegSnap *zs) {
    uint64_t h = cosim_fnv_init();
    h = cosim_fnv_u8(h, zs->A);  h = cosim_fnv_u8(h, zs->F);
    h = cosim_fnv_u8(h, zs->B);  h = cosim_fnv_u8(h, zs->C);
    h = cosim_fnv_u8(h, zs->D);  h = cosim_fnv_u8(h, zs->E);
    h = cosim_fnv_u8(h, zs->H);  h = cosim_fnv_u8(h, zs->L);
    h = cosim_fnv_u8(h, zs->Ap); h = cosim_fnv_u8(h, zs->Fp);
    h = cosim_fnv_u8(h, zs->Bp); h = cosim_fnv_u8(h, zs->Cp);
    h = cosim_fnv_u8(h, zs->Dp); h = cosim_fnv_u8(h, zs->Ep);
    h = cosim_fnv_u8(h, zs->Hp); h = cosim_fnv_u8(h, zs->Lp);
    h = cosim_fnv_u8(h, zs->IXH); h = cosim_fnv_u8(h, zs->IXL);
    h = cosim_fnv_u8(h, zs->IYH); h = cosim_fnv_u8(h, zs->IYL);
    h = cosim_fnv_u8(h, zs->I);  h = cosim_fnv_u8(h, zs->R);
    h = cosim_fnv_u16(h, zs->SP);
    h = cosim_fnv_u8(h, zs->iff_enabled);
    h = cosim_fnv_u8(h, zs->irq_pending);
    h = cosim_fnv_u8(h, zs->bus_requested);
    h = cosim_fnv_u8(h, zs->reset_held);
    h = cosim_fnv_u16(h, zs->bank);
    return h;   /* Z80 PC excluded (currency, like the 68K PC) */
}

static uint64_t hash_vdp_visible(void) {
    static VdpSnap v;   /* 64KB+ — static to keep it off the stack */
    vdp_snapshot(&v, COSIM_EMU);
    uint64_t h = cosim_fnv_init();
    /* Semantic register-derived config (what the game programmed). */
    h = cosim_fnv_u32(h, v.plane_a_addr);      h = cosim_fnv_u32(h, v.plane_b_addr);
    h = cosim_fnv_u32(h, v.window_addr);       h = cosim_fnv_u32(h, v.sprite_table_addr);
    h = cosim_fnv_u32(h, v.hscroll_addr);
    h = cosim_fnv_u8(h, v.access_increment);
    h = cosim_fnv_u8(h, v.display_enabled);
    h = cosim_fnv_u8(h, v.v_int_enabled);      h = cosim_fnv_u8(h, v.h_int_enabled);
    h = cosim_fnv_u8(h, v.h40_enabled);        h = cosim_fnv_u8(h, v.v30_enabled);
    h = cosim_fnv_u8(h, v.shadow_highlight_enabled);
    h = cosim_fnv_u8(h, v.background_colour);  h = cosim_fnv_u8(h, v.h_int_interval);
    h = cosim_fnv_u8(h, v.plane_width_shift);  h = cosim_fnv_u8(h, v.plane_height_bitmask);
    h = cosim_fnv_u8(h, v.hscroll_mask);       h = cosim_fnv_u8(h, v.vscroll_mode);
    /* The DISPLAY output: VRAM + CRAM + VSRAM. */
    h = cosim_fnv_bytes(h, v.vram, sizeof v.vram);
    for (int i = 0; i < 64; i++) h = cosim_fnv_u16(h, v.cram[i]);
    for (int i = 0; i < 64; i++) h = cosim_fnv_u16(h, v.vsram[i]);
    return h;
    /* access_address / access_code / currently_in_vblank / dma_* deliberately
     * omitted — transient FSM/raster phase that differs by timing model. */
}

uint64_t cosim_state_hash_visible(CosimSubHashes *sub) {
    CosimSubHashes s;
    Z80RegSnap zs; z80_snapshot(&zs, COSIM_EMU);

    s.cpu68k    = hash_cpu_visible();
    s.ram       = hash_wram_visible();
    s.z80       = hash_z80_visible(&zs);
    s.z80ram    = cosim_fnv_bytes(cosim_fnv_init(), zs.ram, sizeof zs.ram);
    s.vdp       = hash_vdp_visible();
    uint64_t cnt = 0;
    s.evq       = audio_event_cosim_stream_hash(&cnt);   /* FM/PSG write stream */
    /* Not cross-comparable in pairing #2 (different implementations): */
    s.timing = 0; s.handshake = 0; s.fm = 0; s.psg = 0;
    if (sub) *sub = s;

    uint64_t h = cosim_fnv_init();
    h = cosim_fold(h, s.cpu68k); h = cosim_fold(h, s.ram);
    h = cosim_fold(h, s.z80);    h = cosim_fold(h, s.z80ram);
    h = cosim_fold(h, s.vdp);    h = cosim_fold(h, s.evq);
    return h;
}

/* Localizer: hash a named guest memory region in `nchunks` equal slices, using
 * the SAME normalized snapshot accessors as the visible hash (so it is directly
 * comparable across the own-backend and clownmdemu oracle). The coordinator
 * diffs the per-chunk hashes at the first-divergence checkpoint to localize WHICH
 * bytes of z80ram / WRAM / VRAM split — the field-diff step of the decision
 * procedure for the runner (cross-backend, where a full byte dump is not
 * comparable but a region diff is). Returns nchunks written, or -1 (unknown). */
int cosim_visible_region_chunks(const char *region, int nchunks, uint64_t *out)
{
    const uint8_t *buf = 0; size_t sz = 0;
    static uint8_t s_wram[0x10000];
    static VdpSnap s_v;
    Z80RegSnap zs;
    if (!region) return -1;
    if (!strcmp(region, "z80ram")) { z80_snapshot(&zs, COSIM_EMU); buf = zs.ram; sz = sizeof zs.ram; }
    else if (!strcmp(region, "wram")) { wram_snapshot(s_wram, COSIM_EMU); buf = s_wram; sz = sizeof s_wram; }
    else if (!strcmp(region, "vram")) { vdp_snapshot(&s_v, COSIM_EMU); buf = s_v.vram; sz = sizeof s_v.vram; }
    else return -1;
    if (nchunks < 1) nchunks = 1;
    size_t chunk = sz / (size_t)nchunks; if (chunk == 0) chunk = 1;
    for (int i = 0; i < nchunks; i++) {
        size_t off = (size_t)i * chunk;
        size_t len = (i == nchunks - 1) ? (sz - off) : chunk;
        out[i] = (off < sz) ? cosim_fnv_bytes(cosim_fnv_init(), buf + off, len) : cosim_fnv_init();
    }
    return nchunks;
}

#if !OWN_BACKEND
/* The injection/reset entry points that cosim.c calls live in cosim_state.c,
 * which the ORACLE build cannot compile (it reads own-backend globals). Gate-3
 * fault injection targets the own-backend A-side only, so stub them here for the
 * oracle B-side. */
void cosim_inject_ram(uint32_t addr, uint8_t xor_val) { (void)addr; (void)xor_val; }
void cosim_inject_reg(int reg_index, uint32_t xor_val) { (void)reg_index; (void)xor_val; }
void cosim_state_apply_pending_injection(void) {}
void cosim_state_reset(void) {}
#endif
