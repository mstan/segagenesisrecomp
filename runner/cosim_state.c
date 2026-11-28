/* cosim_state.c — full architectural-state FNV-1a hasher for the Genesis
 * benchmark and differential co-simulation paths.
 *
 * Genesis RAM is tiny (64 KB work + 8 KB Z80 + 64 KB VRAM ~= 136 KB), so we
 * FULL-HASH every checkpoint rather than incremental page-hashing. This drops
 * an entire class of dirty-tracking bugs and makes Gate 4 (hash-vs-byte)
 * trivially sound. Every field is folded explicitly little-endian; struct
 * memory is never hashed raw (padding bytes are a false-divergence source).
 * Function pointers are excluded (host-only). See cosim.h / the proposal.
 */
#include "cosim.h"

#include "include/genesis_runtime.h"      /* g_cpu, g_ram, g_audio_cycle_counter */
#include "video/genesis_machine.h"        /* g_machine (vdp/bus/z80/master_cycle) */
#include "video/genesis_bus.h"            /* GenesisBus                          */
#include "video/genesis_vdp.h"            /* GVDP + size macros                  */
#include "external/superzazu/z80.h"       /* struct z80                          */

/* --- externs owned by other TUs ------------------------------------------ */
extern uint32_t g_68k_stamp_rebase;        /* glue.c: interrupt-handler stamp base */
extern int      glue_cosim_vint_latched(void);  /* glue.c: non-clearing latch read  */

/* Chip statics live in their owning TUs; each exposes a full-state hash so the
 * ymfm/sn76489 internals are hashed (NOT zeroed as frame_snapshots.c does). */
extern uint64_t ym2612_cosim_hash(void);   /* ym2612_ymfm.cpp */
extern uint64_t psg_cosim_hash(void);      /* sn76489.c       */
extern uint64_t audio_event_cosim_hash(void); /* event_queue.c */

/* --- gate-3 injection state ---------------------------------------------- */
static int32_t  s_inj_ram_off = -1;   /* offset into g_ram, or -1              */
static uint8_t  s_inj_ram_xor = 0;
static int      s_inj_reg      = -1;  /* 0..7 = D, 8..15 = A, or -1            */
static uint32_t s_inj_reg_xor  = 0;

void cosim_inject_ram(uint32_t addr, uint8_t xor_val) {
    s_inj_ram_off = (int32_t)(addr & (RAM_SIZE - 1u));
    s_inj_ram_xor = xor_val;
}
void cosim_inject_reg(int reg_index, uint32_t xor_val) {
    s_inj_reg = reg_index; s_inj_reg_xor = xor_val;
}
void cosim_state_apply_pending_injection(void) {
    if (s_inj_ram_off >= 0) {
        g_ram[s_inj_ram_off] ^= s_inj_ram_xor;
        s_inj_ram_off = -1;
    }
    if (s_inj_reg >= 0) {
        if (s_inj_reg < 8)       g_cpu.D[s_inj_reg]     ^= s_inj_reg_xor;
        else if (s_inj_reg < 16) g_cpu.A[s_inj_reg - 8] ^= s_inj_reg_xor;
        s_inj_reg = -1;
    }
}
void cosim_state_reset(void) {
    s_inj_ram_off = -1; s_inj_ram_xor = 0;
    s_inj_reg = -1; s_inj_reg_xor = 0;
}

/* --- per-subsystem hashers ----------------------------------------------- */

static uint64_t hash_cpu68k(void) {
    uint64_t h = cosim_fnv_init();
    for (int i = 0; i < 8; i++) h = cosim_fnv_u32(h, g_cpu.D[i]);
    for (int i = 0; i < 8; i++) h = cosim_fnv_u32(h, g_cpu.A[i]);  /* A7 = SSP */
    h = cosim_fnv_u16(h, g_cpu.SR);
    h = cosim_fnv_u32(h, g_cpu.USP);
    /* g_cpu.PC deliberately EXCLUDED: block-granular currency on the recomp
     * side vs per-instruction on the interp side. A real control-flow split
     * still surfaces as a D/A/RAM diff within one checkpoint. */
    return h;
}

static uint64_t hash_timing(void) {
    uint64_t h = cosim_fnv_init();
    h = cosim_fnv_u32(h, g_audio_cycle_counter);
    h = cosim_fnv_u32(h, g_68k_stamp_rebase);
    h = cosim_fnv_u32(h, (uint32_t)glue_cosim_vint_latched());
    h = cosim_fnv_u32(h, g_machine.master_cycle);
    h = cosim_fnv_u32(h, g_machine.z80_cycle_debt);
    return h;
}

static uint64_t hash_ram(void) {
    return cosim_fnv_bytes(cosim_fnv_init(), g_ram, RAM_SIZE);
}

static uint64_t hash_z80(void) {
    const z80 *z = &g_machine.z80;
    uint64_t h = cosim_fnv_init();
    /* funcptrs (read_byte/write_byte/port_in/port_out) + userdata EXCLUDED */
    h = cosim_fnv_u64(h, (uint64_t)z->cyc);   /* t-state counter (guest quantity) */
    h = cosim_fnv_u16(h, z->pc);
    h = cosim_fnv_u16(h, z->sp);
    h = cosim_fnv_u16(h, z->ix);
    h = cosim_fnv_u16(h, z->iy);
    h = cosim_fnv_u16(h, z->mem_ptr);
    h = cosim_fnv_u8(h, z->a); h = cosim_fnv_u8(h, z->b); h = cosim_fnv_u8(h, z->c);
    h = cosim_fnv_u8(h, z->d); h = cosim_fnv_u8(h, z->e); h = cosim_fnv_u8(h, z->h);
    h = cosim_fnv_u8(h, z->l);
    h = cosim_fnv_u8(h, z->a_); h = cosim_fnv_u8(h, z->b_); h = cosim_fnv_u8(h, z->c_);
    h = cosim_fnv_u8(h, z->d_); h = cosim_fnv_u8(h, z->e_); h = cosim_fnv_u8(h, z->h_);
    h = cosim_fnv_u8(h, z->l_); h = cosim_fnv_u8(h, z->f_);
    h = cosim_fnv_u8(h, z->i); h = cosim_fnv_u8(h, z->r);
    /* flag bitfields packed into one byte, deterministic order */
    uint8_t flags = (uint8_t)((z->sf<<7)|(z->zf<<6)|(z->yf<<5)|(z->hf<<4)|
                              (z->xf<<3)|(z->pf<<2)|(z->nf<<1)|(z->cf));
    h = cosim_fnv_u8(h, flags);
    h = cosim_fnv_u8(h, z->iff_delay);
    h = cosim_fnv_u8(h, z->interrupt_mode);
    h = cosim_fnv_u8(h, z->int_data);
    uint8_t st = (uint8_t)((z->iff1<<4)|(z->iff2<<3)|(z->halted<<2)|
                           (z->int_pending<<1)|(z->nmi_pending));
    h = cosim_fnv_u8(h, st);
    return h;
}

static uint64_t hash_z80ram(void) {
    return cosim_fnv_bytes(cosim_fnv_init(), g_machine.bus.z80_ram, sizeof g_machine.bus.z80_ram);
}

static uint64_t hash_handshake(void) {
    const GenesisBus *b = &g_machine.bus;
    uint64_t h = cosim_fnv_init();
    h = cosim_fnv_u32(h, b->z80_bank);
    h = cosim_fnv_u32(h, (uint32_t)b->bank_shift);
    h = cosim_fnv_u8(h, b->z80_busreq);
    h = cosim_fnv_u8(h, b->z80_reset_off);
    h = cosim_fnv_u8(h, b->z80_reset_pending);
    for (int i = 0; i < 2; i++) h = cosim_fnv_u16(h, b->pad[i]);
    for (int i = 0; i < 2; i++) h = cosim_fnv_u8(h, b->pad_type[i]);
    for (int i = 0; i < 2; i++) h = cosim_fnv_u8(h, b->pad_th_count[i]);
    for (int i = 0; i < 2; i++) h = cosim_fnv_u8(h, b->pad_th_prev[i]);
    for (int i = 0; i < 3; i++) h = cosim_fnv_u8(h, b->io_data[i]);
    for (int i = 0; i < 3; i++) h = cosim_fnv_u8(h, b->io_ctrl[i]);
    h = cosim_fnv_u8(h, b->version);
    /* SRAM overlay geometry always; contents only for the declared span
     * (Sonic 1 has none: sram_size == 0). */
    h = cosim_fnv_u32(h, b->sram_base);
    h = cosim_fnv_u32(h, b->sram_end);
    h = cosim_fnv_u32(h, b->sram_size);
    h = cosim_fnv_u8(h, b->sram_present);
    h = cosim_fnv_u8(h, b->sram_enabled);
    if (b->sram_size && b->sram_size <= sizeof b->sram)
        h = cosim_fnv_bytes(h, b->sram, b->sram_size);
    return h;
}

static uint64_t hash_vdp(void) {
    const GVDP *v = &g_machine.vdp;
    uint64_t h = cosim_fnv_init();
    h = cosim_fnv_bytes(h, v->vram, sizeof v->vram);
    for (int i = 0; i < GVDP_CRAM_ENTRIES;  i++) h = cosim_fnv_u16(h, v->cram[i]);
    for (int i = 0; i < GVDP_VSRAM_ENTRIES; i++) h = cosim_fnv_u16(h, v->vsram[i]);
    h = cosim_fnv_bytes(h, v->reg, sizeof v->reg);
    h = cosim_fnv_u8(h, v->control_pending);
    h = cosim_fnv_u8(h, v->code);
    h = cosim_fnv_u16(h, v->address);
    h = cosim_fnv_u8(h, v->address_hi);
    h = cosim_fnv_u8(h, v->in_vblank);
    h = cosim_fnv_u8(h, v->in_hblank);
    h = cosim_fnv_u8(h, v->dma_active);
    h = cosim_fnv_u8(h, v->sprite_overflow);
    h = cosim_fnv_u8(h, v->sprite_collision);
    h = cosim_fnv_u8(h, v->vint_pending);
    h = cosim_fnv_u16(h, v->hint_counter);
    h = cosim_fnv_u16(h, v->scanline);
    h = cosim_fnv_u8(h, v->dma_fill_pending);
    /* funcptrs (bus_read/colour_updated) + their user ptrs EXCLUDED */
    return h;
}

/* --- top-level ----------------------------------------------------------- */
uint64_t cosim_state_hash(CosimSubHashes *sub) {
    CosimSubHashes s;
    s.cpu68k    = hash_cpu68k();
    s.timing    = hash_timing();
    s.ram       = hash_ram();
    s.z80       = hash_z80();
    s.z80ram    = hash_z80ram();
    s.handshake = hash_handshake();
    s.vdp       = hash_vdp();
    s.fm        = ym2612_cosim_hash();
    s.psg       = psg_cosim_hash();
    s.evq       = audio_event_cosim_hash();
    if (sub) *sub = s;

    /* Combined full-state hash = fold of every sub-hash in struct order. */
    uint64_t h = cosim_fnv_init();
    h = cosim_fold(h, s.cpu68k);
    h = cosim_fold(h, s.timing);
    h = cosim_fold(h, s.ram);
    h = cosim_fold(h, s.z80);
    h = cosim_fold(h, s.z80ram);
    h = cosim_fold(h, s.handshake);
    h = cosim_fold(h, s.vdp);
    h = cosim_fold(h, s.fm);
    h = cosim_fold(h, s.psg);
    h = cosim_fold(h, s.evq);
    return h;
}
