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

#include "clownmdemu.h"
#include "genesis_runtime.h"

extern ClownMDEmu g_clownmdemu;

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

#if ENABLE_RECOMPILED_CODE && !defined(SONIC_ORACLE_BUILD)
    /* Native build: g_cpu is live state. */
    for (int i = 0; i < 8; i++) out->D[i] = g_cpu.D[i];
    for (int i = 0; i < 8; i++) out->A[i] = g_cpu.A[i];
    out->USP = g_cpu.USP;
    out->PC  = g_cpu.PC;
    out->SR  = g_cpu.SR;
#else
    /* Oracle build: read from the interpreter's own state. A7 follows
     * the supervisor/user stack pointer based on the S flag. */
    const Clown68000_State *cs = &g_clownmdemu.m68k;
    for (int i = 0; i < 8; i++) out->D[i] = (uint32_t)cs->data_registers[i];
    for (int i = 0; i < 7; i++) out->A[i] = (uint32_t)cs->address_registers[i];
    out->A[7] = ((uint16_t)cs->status_register & (1u << 13))
                ? (uint32_t)cs->supervisor_stack_pointer
                : (uint32_t)cs->user_stack_pointer;
    out->USP = (uint32_t)cs->user_stack_pointer;
    out->PC  = (uint32_t)cs->program_counter;
    out->SR  = (uint16_t)cs->status_register;
#endif

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
    const ClownZ80_State *z = &emu->z80;
    out->A = z->a; out->F = z->f;
    out->B = z->b; out->C = z->c;
    out->D = z->d; out->E = z->e;
    out->H = z->h; out->L = z->l;
    out->Ap = z->a_; out->Fp = z->f_;
    out->Bp = z->b_; out->Cp = z->c_;
    out->Dp = z->d_; out->Ep = z->e_;
    out->Hp = z->h_; out->Lp = z->l_;
    out->IXH = z->ixh; out->IXL = z->ixl;
    out->IYH = z->iyh; out->IYL = z->iyl;
    out->I = z->i; out->R = z->r;
    out->SP = (uint16_t)z->stack_pointer;
    out->PC = (uint16_t)z->program_counter;
    out->iff_enabled  = (uint8_t)z->interrupts_enabled;
    out->irq_pending  = (uint8_t)z->interrupt_pending;

    /* Z80 RAM lives in the per-iteration state struct. */
    memcpy(out->ram, emu->state.z80.ram, sizeof(out->ram));
    out->bus_requested = (uint8_t)emu->state.z80.bus_requested;
    out->reset_held    = (uint8_t)emu->state.z80.reset_held;
    out->bank          = (uint16_t)emu->state.z80.bank;
}

/* ---------------------------------------------------------------- VDP */

void vdp_snapshot(VdpSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    const VDP_State *v = &emu->vdp.state;

    out->plane_a_addr        = (uint32_t)v->plane_a_address;
    out->plane_b_addr        = (uint32_t)v->plane_b_address;
    out->window_addr         = (uint32_t)v->window_address;
    out->sprite_table_addr   = (uint32_t)v->sprite_table_address;
    out->hscroll_addr        = (uint32_t)v->hscroll_address;
    out->access_address      = (uint32_t)v->access.address_register;
    out->access_code         = (uint16_t)v->access.code_register;
    out->access_increment    = (uint8_t)v->access.increment;

    out->display_enabled        = (uint8_t)v->display_enabled;
    out->v_int_enabled          = (uint8_t)v->v_int_enabled;
    out->h_int_enabled          = (uint8_t)v->h_int_enabled;
    out->h40_enabled            = (uint8_t)v->h40_enabled;
    out->v30_enabled            = (uint8_t)v->v30_enabled;
    out->shadow_highlight_enabled = (uint8_t)v->shadow_highlight_enabled;
    out->background_colour      = (uint8_t)v->background_colour;
    out->h_int_interval         = (uint8_t)v->h_int_interval;
    out->plane_width_shift      = (uint8_t)v->plane_width_shift;
    out->plane_height_bitmask   = (uint8_t)v->plane_height_bitmask;
    out->hscroll_mask           = (uint8_t)v->hscroll_mask;
    out->vscroll_mode           = (uint8_t)v->vscroll_mode;
    out->currently_in_vblank    = (uint8_t)v->currently_in_vblank;
    out->dma_enabled            = (uint8_t)v->dma.enabled;
    out->dma_mode               = (uint8_t)v->dma.mode;
    out->dma_length             = (uint16_t)v->dma.length;
    out->dma_source             = ((uint32_t)v->dma.source_address_high << 16)
                                | (uint32_t)v->dma.source_address_low;

    /* VRAM / CRAM / VSRAM — bulk byte copies. */
    memcpy(out->vram, v->vram, sizeof(out->vram));
    /* CRAM: copy first 64 entries from the (PALETTE_LINE_LENGTH ×
     * TOTAL_PALETTE_LINES) array — Genesis exposes 64 palette slots. */
    for (int i = 0; i < 64 && i < (int)(sizeof(v->cram) / sizeof(v->cram[0])); i++)
        out->cram[i] = (uint16_t)v->cram[i];
    /* VSRAM: copy up to 64 words. */
    for (int i = 0; i < 64; i++) out->vsram[i] = (uint16_t)v->vsram[i];
}

/* ---------------------------------------------------------------- FM */

void fm_snapshot(FmSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    /* Byte-copy the entire FM_State. Layout is opaque to us; differs
     * across clownmdemu versions. The TCP layer surfaces specific
     * fields by parsing this blob with knowledge of the struct. */
    size_t len = sizeof(emu->fm.state);
    if (len > sizeof(out->raw)) len = sizeof(out->raw);
    memcpy(out->raw, &emu->fm.state, len);
    out->raw_len = (uint16_t)len;
}

/* ---------------------------------------------------------------- PSG */

void psg_snapshot(PsgSnap *out, ClownMDEmu *emu)
{
    memset(out, 0, sizeof(*out));
    size_t len = sizeof(emu->psg.state);
    if (len > sizeof(out->raw)) len = sizeof(out->raw);
    memcpy(out->raw, &emu->psg.state, len);
    out->raw_len = (uint16_t)len;
}

/* ---------------------------------------------------------------- WRAM */

void wram_snapshot(uint8_t out[0x10000], ClownMDEmu *emu)
{
    /* Convert clownmdemu's word-addressed RAM (0x8000 entries) to a
     * flat byte image. Big-endian byte order matches what the 68K sees. */
    const cc_u16l *ram = emu->state.m68k.ram;
    for (int i = 0; i < 0x8000; i++) {
        uint16_t w = (uint16_t)ram[i];
        out[i * 2 + 0] = (uint8_t)(w >> 8);
        out[i * 2 + 1] = (uint8_t)(w & 0xFF);
    }
}
