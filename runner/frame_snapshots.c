/*
 * frame_snapshots.c — implementations of the per-subsystem snapshot
 * accessors declared in frame_record.h. Each one reads live state from
 * the running clownmdemu instance (or g_cpu, for the recompiled 68K
 * register file) into the corresponding *Snap struct. Pure copies; no
 * side effects; safe to call once per frame from cmd_server.
 */

#include "frame_record.h"

#include <string.h>
#include <stdint.h>

#include "backend_decls.h"   /* own decls — native builds have no clownmdemu paths */
#include "genesis_runtime.h"


/* Own-backend (AGPL-free) state lives in g_machine, not clownmdemu. The
 * snapshot accessors below read it directly and ignore the (NULL) emu arg,
 * so cmd_server can populate the SAME FrameRecord fields the oracle does —
 * the cross-backend parity surface for divergence_diff. WRAM is g_ram (the
 * authoritative own-backend work RAM; see genesis_bus.c). FM/PSG internal
 * state is ymfm/sn76489-specific and NOT byte-comparable to clownmdemu's
 * FM_State/PSG_State, so those stay zeroed here — the FM/PSG register WRITE
 * STREAM (chip_ring) is the cross-backend audio comparable instead. */
#include "video/genesis_machine.h"
extern uint8_t g_ram[0x10000];

/* ---------------------------------------------------------------- 68K
 *
 * In native mode the recompiled C code drives g_cpu directly — that's
 * the authoritative 68K state.
 *
 * In oracle mode the clown68000 interpreter drives g_clownmdemu.m68k;
 * g_cpu is just a stale shadow because no recompiled instruction has
 * touched it. We MUST read from the interpreter struct so the snapshot
 * reflects what actually executed. Otherwise the cpu snapshot is all
 * zeros and every comparison reports a spurious divergence at frame 0.
 *
 * The two structs differ in field names but represent the same 68K. */

void m68k_snapshot(M68KRegSnap *out)
{
    memset(out, 0, sizeof(*out));

    /* Native build: g_cpu is live state. */
    for (int i = 0; i < 8; i++) out->D[i] = g_cpu.D[i];
    for (int i = 0; i < 8; i++) out->A[i] = g_cpu.A[i];
    out->USP = g_cpu.USP;
    out->PC  = g_cpu.PC;
    out->SR  = g_cpu.SR;

    out->flag_C = (uint8_t)((out->SR >> 0) & 1u);
    out->flag_V = (uint8_t)((out->SR >> 1) & 1u);
    out->flag_Z = (uint8_t)((out->SR >> 2) & 1u);
    out->flag_N = (uint8_t)((out->SR >> 3) & 1u);
    out->flag_X = (uint8_t)((out->SR >> 4) & 1u);
    out->flag_S = (uint8_t)((out->SR >> 13) & 1u);
    out->imask  = (uint8_t)((out->SR >> 8) & 7u);
}

/* ---------------------------------------------------------------- Z80 */

void z80_snapshot(Z80RegSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    (void)emu;
    /* Live Z80 regs are canonical in g_machine.z80 (the embedded superzazu
     * core); RAM + bus-control in the bus. */
    const z80 *z = &g_machine.z80;
    out->A = z->a;
    out->F = (uint8_t)((z->sf << 7) | (z->zf << 6) | (z->yf << 5) | (z->hf << 4) |
                       (z->xf << 3) | (z->pf << 2) | (z->nf << 1) | (z->cf << 0));
    out->B = z->b; out->C = z->c;
    out->D = z->d; out->E = z->e;
    out->H = z->h; out->L = z->l;
    out->Ap = z->a_; out->Fp = z->f_;
    out->Bp = z->b_; out->Cp = z->c_;
    out->Dp = z->d_; out->Ep = z->e_;
    out->Hp = z->h_; out->Lp = z->l_;
    out->IXH = (uint8_t)(z->ix >> 8); out->IXL = (uint8_t)(z->ix & 0xFF);
    out->IYH = (uint8_t)(z->iy >> 8); out->IYL = (uint8_t)(z->iy & 0xFF);
    out->I = z->i; out->R = z->r;
    out->SP = z->sp;
    out->PC = z->pc;
    out->iff_enabled  = (uint8_t)z->iff1;
    out->irq_pending  = (uint8_t)z->int_pending;
    memcpy(out->ram, g_machine.bus.z80_ram, sizeof(out->ram));
    out->bus_requested = (uint8_t)g_machine.bus.z80_busreq;
    /* clownmdemu's reset_held == "Z80 in reset"; ours tracks the inverse
     * (z80_reset_off = 1 means running), so invert for a comparable field. */
    out->reset_held    = (uint8_t)(!g_machine.bus.z80_reset_off);
    out->bank          = (uint16_t)g_machine.bus.z80_bank;
    return;
}

/* ---------------------------------------------------------------- VDP */

void vdp_snapshot(VdpSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    (void)emu;
    /* Decode our GVDP register file into the SAME semantic fields the oracle
     * (clownmdemu) exposes, using the documented Genesis register layout, so
     * divergence_diff compares apples-to-apples. VRAM/VSRAM are raw byte/word
     * images (directly comparable). CRAM encoding is normalised below. */
    const GVDP *v = &g_machine.vdp;
    const uint8_t *r = v->reg;

    out->plane_a_addr      = (uint32_t)(r[2] & 0x38) << 10;
    out->plane_b_addr      = (uint32_t)(r[4] & 0x07) << 13;
    out->window_addr       = (uint32_t)(r[3] & 0x3E) << 10;
    out->sprite_table_addr = (uint32_t)(r[5] & 0x7F) << 9;
    out->hscroll_addr      = (uint32_t)(r[13] & 0x3F) << 10;
    out->access_address    = (uint32_t)v->address;
    out->access_code       = (uint16_t)v->code;
    out->access_increment  = (uint8_t)r[15];

    out->display_enabled        = (uint8_t)((r[1] >> 6) & 1);
    out->v_int_enabled          = (uint8_t)((r[1] >> 5) & 1);
    out->h_int_enabled          = (uint8_t)((r[0] >> 4) & 1);
    out->h40_enabled            = (uint8_t)((r[12] & 0x81) ? 1 : 0);
    out->v30_enabled            = (uint8_t)((r[1] >> 3) & 1);
    out->shadow_highlight_enabled = (uint8_t)((r[12] >> 3) & 1);
    out->background_colour      = (uint8_t)(r[7] & 0x3F);
    out->h_int_interval         = (uint8_t)r[10];
    /* plane size (reg[16]): width = bits1-0, height = bits5-4 (cell counts
     * 32/64/128). Expose the same shift/bitmask the oracle snapshot reports. */
    {
        static const uint8_t wshift[4] = { 5, 6, 6, 7 };   /* 32/64/(inv)/128 */
        static const uint16_t hmask[4] = { 31, 63, 63, 127 };
        out->plane_width_shift    = wshift[r[16] & 3];
        out->plane_height_bitmask = (uint8_t)hmask[(r[16] >> 4) & 3];
    }
    out->hscroll_mask           = (uint8_t)(r[11] & 3);
    out->vscroll_mode           = (uint8_t)((r[11] >> 2) & 1);
    out->currently_in_vblank    = (uint8_t)v->in_vblank;
    out->dma_enabled            = (uint8_t)((r[1] >> 4) & 1);
    out->dma_mode               = (uint8_t)((r[23] >> 6) & 3);
    out->dma_length             = (uint16_t)(r[19] | (r[20] << 8));
    out->dma_source             = ((uint32_t)(r[23] & 0x7F) << 16)
                                | ((uint32_t)r[22] << 8) | (uint32_t)r[21];

    memcpy(out->vram, v->vram, sizeof(out->vram));
    /* CRAM: GVDP stores 9-bit BGR (0x0BGR, 3 bits each). clownmdemu's snapshot
     * reports the raw value too; both are the 9-bit BGR the 68K wrote, so a
     * direct copy is comparable. */
    for (int i = 0; i < 64 && i < GVDP_CRAM_ENTRIES; i++)
        out->cram[i] = (uint16_t)v->cram[i];
    for (int i = 0; i < 64 && i < GVDP_VSRAM_ENTRIES; i++)
        out->vsram[i] = (uint16_t)v->vsram[i];
    return;
}

/* ---------------------------------------------------------------- FM */

void fm_snapshot(FmSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    /* ymfm internals are not byte-comparable to clownmdemu's FM_State;
     * the FM/PSG register WRITE STREAM (chip_ring) is the cross-backend
     * audio comparable instead. */
    (void)emu;
}

/* ---------------------------------------------------------------- PSG */

void psg_snapshot(PsgSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    (void)emu;   /* see fm_snapshot */
}

/* ---------------------------------------------------------------- WRAM */

void wram_snapshot(uint8_t out[0x10000], ClownMDEmu *emu)
{
    (void)emu;
    /* Own backend: g_ram IS the authoritative byte-addressed work RAM. */
    memcpy(out, g_ram, 0x10000);
    return;
}
