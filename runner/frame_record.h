/*
 * frame_record.h — per-frame ring-buffer snapshot for the debug server.
 *
 * Captured by cmd_server_record_frame() once per frame. Read back over
 * TCP via get_frame / frame_range / first_failure (see TCP.md).
 *
 * Sized at ~140 KB per frame; ring at FRAME_HISTORY_CAP frames. Memory:
 * FRAME_HISTORY_CAP * sizeof(FrameRecord). At 600 frames that's ~84 MB,
 * a 10-second history at 60 fps. Bump if you need a longer window AND
 * have the RAM. Splitting into a small-state ring (CPU/regs only, big)
 * and a big-state ring (RAM/VRAM, smaller) is the next step if memory
 * becomes a constraint — see snesrecomp/runner/src/debug_server.c for
 * the precedent comment.
 */
#pragma once

#include <stdint.h>

/* ---------------------------------------------------------------- 68K */

typedef struct {
    uint32_t D[8];
    uint32_t A[8];
    uint32_t USP;
    uint32_t PC;            /* recompiled native: marker only (last_func) */
    uint16_t SR;
    /* Exploded SR for convenient diff/inspection */
    uint8_t  flag_C, flag_V, flag_Z, flag_N, flag_X, flag_S;
    uint8_t  imask;         /* bits 8-10 of SR */
    uint8_t  _pad;
} M68KRegSnap;

/* ---------------------------------------------------------------- Z80 */

typedef struct {
    /* Main register file */
    uint8_t  A, F, B, C, D, E, H, L;
    /* Alternate register file */
    uint8_t  Ap, Fp, Bp, Cp, Dp, Ep, Hp, Lp;
    /* Index registers split into halves to match clownz80 */
    uint8_t  IXH, IXL, IYH, IYL;
    /* Misc */
    uint8_t  I, R;
    uint16_t SP;
    uint16_t PC;
    uint8_t  iff_enabled;
    uint8_t  irq_pending;
    /* Z80 RAM ($A00000-$A01FFF mirror) */
    uint8_t  ram[0x2000];
    /* Bus state */
    uint8_t  bus_requested;
    uint8_t  reset_held;
    uint16_t bank;          /* current bank pointer for $8000 window */
} Z80RegSnap;

/* ---------------------------------------------------------------- VDP */

typedef struct {
    /* Discrete semantic regs (clownmdemu doesn't keep a flat reg file) */
    uint32_t plane_a_addr;
    uint32_t plane_b_addr;
    uint32_t window_addr;
    uint32_t sprite_table_addr;
    uint32_t hscroll_addr;
    uint32_t access_address;    /* current VRAM/CRAM/VSRAM write address */
    uint16_t access_code;
    uint8_t  access_increment;
    uint8_t  display_enabled;
    uint8_t  v_int_enabled, h_int_enabled;
    uint8_t  h40_enabled, v30_enabled;
    uint8_t  shadow_highlight_enabled;
    uint8_t  background_colour;
    uint8_t  h_int_interval;
    uint8_t  plane_width_shift;
    uint8_t  plane_height_bitmask;
    uint8_t  hscroll_mask;
    uint8_t  vscroll_mode;
    uint8_t  currently_in_vblank;
    uint8_t  dma_enabled;
    uint8_t  dma_mode;
    uint16_t dma_length;
    uint32_t dma_source;
    /* Video RAM (full byte-addressable image) */
    uint8_t  vram[0x10000];     /* 64 KB */
    uint16_t cram[64];          /* palette */
    uint16_t vsram[64];         /* vertical scroll */
} VdpSnap;

/* ---------------------------------------------------------------- FM */

typedef struct {
    /* The full FM_State is small (sub-1 KB) so we copy bytes verbatim and
     * decode at query time. Layout matches clownmdemu's FM_State exactly;
     * see fm.h. cmd_server.c reads the active state via memcpy. */
    uint8_t  raw[1024];
    uint16_t raw_len;           /* actual bytes written into raw */
} FmSnap;

/* ---------------------------------------------------------------- PSG */

typedef struct {
    /* Same approach as FM: byte-copy of PSG_State. */
    uint8_t  raw[256];
    uint16_t raw_len;
} PsgSnap;

/* ---------------------------------------------------------------- pace */

/* Per-wall-frame cycle-pacing telemetry. Captured from glue.c's
 * counters at end_of_wall_frame and included in every FrameRecord so
 * divergence_diff can spot recomp-vs-oracle pacing skew (slow music,
 * starved audio, missed VBlanks) without parsing stderr. Replaces the
 * legacy `[FPACE]` printf line. All fields are per-wall-frame deltas
 * unless noted. */
typedef struct {
    uint64_t insns_delta;        /* recompiled-C 68K instructions retired
                                  * (oracle: always 0 — interpreter
                                  * does not bump g_native_insn_count). */
    uint64_t bus_delta;          /* bus accesses through glue's read/write
                                  * wrappers; native-side only. */
    uint32_t audio_cyc;          /* 68K-equivalent cycles charged against
                                  * audio synthesis this wall frame. */
    uint8_t  vblanks_fired;      /* 1 if the VBla handler ran this wall
                                  * frame, 0 if forcing was skipped. */
    uint8_t  _pad[3];
} PaceSnap;

/* ---------------------------------------------------------------- diff */

#define MAX_FRAME_DIFFS 32
typedef struct {
    uint32_t addr;
    uint32_t expected;
    uint32_t actual;
    uint8_t  size;              /* 1, 2 or 4 bytes */
    uint8_t  field;             /* FrameDiffField enum */
} FrameDiffEntry;

enum FrameDiffField {
    DIFF_FIELD_M68K   = 1,
    DIFF_FIELD_Z80    = 2,
    DIFF_FIELD_VDP    = 3,
    DIFF_FIELD_FM     = 4,
    DIFF_FIELD_PSG    = 5,
    DIFF_FIELD_WRAM   = 6,
    DIFF_FIELD_VRAM   = 7,
    DIFF_FIELD_CRAM   = 8,
    DIFF_FIELD_VSRAM  = 9,
    DIFF_FIELD_Z80RAM = 10,
};

/* ---------------------------------------------------------------- record */

typedef struct {
    uint32_t       frame;

    M68KRegSnap    m68k;
    Z80RegSnap     z80;
    VdpSnap        vdp;
    FmSnap         fm;
    PsgSnap        psg;

    /* 68K work RAM ($FF0000-$FFFFFF) */
    uint8_t        wram[0x10000];

    /* Game-specific tail filled by game_fill_frame_record() */
    uint8_t        game_data[256];
    char           last_func[64];

    /* Verify-mode (filled by verify_mode.c when oracle comparison runs) */
    int            verify_pass;     /* -1 not run, 0 diverge, 1 match */
    int            diff_count;
    FrameDiffEntry diffs[MAX_FRAME_DIFFS];
} FrameRecord;

#ifndef FRAME_HISTORY_CAP
#  define FRAME_HISTORY_CAP 600     /* ~10 s at 60 fps; ~84 MB resident */
#endif

/* ----------------------------------------------------------------
 * Snapshot accessors (implemented in cmd_server.c). These read live
 * subsystem state into the corresponding snap struct. They never
 * allocate and they never block.
 * ---------------------------------------------------------------- */
struct ClownMDEmu;          /* fwd */
struct M68KState;           /* fwd; defined in clownmdemu_globals.h */

void m68k_snapshot(M68KRegSnap *out);
void z80_snapshot(Z80RegSnap *out, struct ClownMDEmu *emu);
void vdp_snapshot(VdpSnap *out, struct ClownMDEmu *emu);
void fm_snapshot (FmSnap  *out, struct ClownMDEmu *emu);
void psg_snapshot(PsgSnap *out, struct ClownMDEmu *emu);
void wram_snapshot(uint8_t out[0x10000], struct ClownMDEmu *emu);
