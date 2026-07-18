/*
 * z80_recomp.c - statically-recompiled Genesis Z80 coprocessor adapter.
 *
 * Generated code comes from SmsRecomp --flat-step and operates on the same
 * packed-flag Z80State / z80_ops semantics from z80-recomp-core.
 * This adapter supplies the Genesis bus, explicit interrupt acceptance, and a
 * loud interpreter fallback for a PC outside the generated driver image.
 */
#include "z80_recomp.h"

#include "game_spec.h"
#include "video/genesis_machine.h"
#include "external/superzazu/z80.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Z80State g_z80;

static uint64_t s_fallback_steps;
static uint8_t s_fallback_seen[0x10000 / 8];
static int s_log_misses = -1;

static uint8_t pack_flags(const z80 *z)
{
    return (uint8_t)((z->sf << 7) | (z->zf << 6) | (z->yf << 5) |
                     (z->hf << 4) | (z->xf << 3) | (z->pf << 2) |
                     (z->nf << 1) | z->cf);
}

static void unpack_flags(z80 *z, uint8_t f)
{
    z->sf=(f>>7)&1; z->zf=(f>>6)&1; z->yf=(f>>5)&1; z->hf=(f>>4)&1;
    z->xf=(f>>3)&1; z->pf=(f>>2)&1; z->nf=(f>>1)&1; z->cf=f&1;
}

void z80_recomp_mirror_to_interpreter(z80 *z)
{
    z->cyc=(unsigned long)g_z80.cyc;
    z->pc=g_z80.pc; z->sp=g_z80.sp; z->ix=g_z80.ix; z->iy=g_z80.iy;
    z->mem_ptr=g_z80.wz;
    z->a=g_z80.a; z->b=g_z80.b; z->c=g_z80.c; z->d=g_z80.d;
    z->e=g_z80.e; z->h=g_z80.h; z->l=g_z80.l;
    z->a_=g_z80.a_; z->f_=g_z80.f_; z->b_=g_z80.b_; z->c_=g_z80.c_;
    z->d_=g_z80.d_; z->e_=g_z80.e_; z->h_=g_z80.h_; z->l_=g_z80.l_;
    z->i=g_z80.i; z->r=g_z80.r; unpack_flags(z,g_z80.f);
    z->iff1=g_z80.iff1; z->iff2=g_z80.iff2;
    z->interrupt_mode=g_z80.im; z->halted=g_z80.halted;
    z->iff_delay=g_z80.ei_block;
}

void z80_recomp_restore_from_interpreter(const z80 *z)
{
    g_z80.cyc=z->cyc;
    g_z80.pc=z->pc; g_z80.sp=z->sp; g_z80.ix=z->ix; g_z80.iy=z->iy;
    g_z80.wz=z->mem_ptr;
    g_z80.a=z->a; g_z80.f=pack_flags(z); g_z80.b=z->b; g_z80.c=z->c;
    g_z80.d=z->d; g_z80.e=z->e; g_z80.h=z->h; g_z80.l=z->l;
    g_z80.a_=z->a_; g_z80.f_=z->f_; g_z80.b_=z->b_; g_z80.c_=z->c_;
    g_z80.d_=z->d_; g_z80.e_=z->e_; g_z80.h_=z->h_; g_z80.l_=z->l_;
    g_z80.i=z->i; g_z80.r=z->r; g_z80.iff1=z->iff1; g_z80.iff2=z->iff2;
    g_z80.im=z->interrupt_mode; g_z80.halted=z->halted;
    g_z80.ei_block=z->iff_delay;
}

uint8_t sms_read8(uint16_t addr)
{
    return gbus_z80_read(&g_machine.bus, addr);
}

void sms_write8(uint16_t addr, uint8_t value)
{
    gbus_z80_write(&g_machine.bus, addr, value);
}

uint8_t sms_io_in(uint8_t port)
{
    return gbus_z80_read(&g_machine.bus,
                         (uint16_t)((g_z80.b << 8) | port));
}

void sms_io_out(uint8_t port, uint8_t value)
{
    gbus_z80_write(&g_machine.bus,
                   (uint16_t)((g_z80.b << 8) | port), value);
}

/* Flat generated code has no bank mapper; these satisfy the shared runtime
 * interface and remain useful if a generated diagnostic calls them. */
int sms_slot_bank(uint16_t addr) { (void)addr; return 0; }
void sms_sync(void) {}
void sms_halt(void) { g_z80.halted = true; }
void sms_diff_abort(void) {}
void sms_diff_enter(uint16_t addr) { (void)addr; }

uint64_t g_sync_deadline = UINT64_MAX;
uint16_t g_enter_ring[SMS_ENTER_RING_SIZE];
uint32_t g_enter_pos;
uint16_t g_dbg_pc;
uint64_t g_frame_ic;
int g_diff_freeze;
uint64_t g_diff_icount;
int g_diff_active;

void sms_dispatch_miss(uint16_t addr)
{
    z80 *fallback = &g_machine.z80;
    z80_recomp_mirror_to_interpreter(fallback);
    /* z80_step() also services the interpreter's interrupt latches. Interrupt
     * acceptance belongs to accept_interrupts() after this generated/fallback
     * instruction boundary, so suppress it for the one-instruction capsule
     * without losing a pending level asserted by the Genesis scheduler. */
    int int_pending = fallback->int_pending;
    int nmi_pending = fallback->nmi_pending;
    fallback->int_pending = 0;
    fallback->nmi_pending = 0;
    z80_step(fallback);
    z80_recomp_restore_from_interpreter(fallback);
    fallback->int_pending = int_pending;
    fallback->nmi_pending = nmi_pending;
    s_fallback_steps++;
    if (s_log_misses < 0)
        s_log_misses = getenv("GENESIS_Z80_AOT_LOG_MISSES") ? 1 : 0;
    unsigned byte = addr >> 3;
    uint8_t bit = (uint8_t)(1u << (addr & 7));
    if (s_log_misses && !(s_fallback_seen[byte] & bit)) {
        s_fallback_seen[byte] |= bit;
        fprintf(stderr, "[Z80-AOT] dispatch miss at $%04X; interpreter fallback (total=%llu)\n",
                addr, (unsigned long long)s_fallback_steps);
    }
}

static void push16(uint16_t value)
{
    g_z80.sp = (uint16_t)(g_z80.sp - 2);
    sms_write16(g_z80.sp, value);
}

static void accept_interrupts(void)
{
    /* EI blocks the accept slot immediately following EI. Generated EI sets
     * iff1/iff2 and this one-boundary latch; clear it after declining once. */
    if (g_z80.ei_block) { g_z80.ei_block = 0; return; }
    if (!g_machine.z80.int_pending || !g_z80.iff1) return;

    g_machine.z80.int_pending = 0;
    g_z80.halted = false;
    g_z80.iff1 = g_z80.iff2 = false;
    g_z80.r = (uint8_t)((g_z80.r & 0x80) | ((g_z80.r + 1) & 0x7F));
    switch (g_z80.im) {
    case 0:
        /* Genesis drivers use IM1. IM0's data bus is normally $FF (RST 38),
         * so model that hardware value explicitly rather than decode at run. */
        g_z80.cyc += 13; push16(g_z80.pc); g_z80.pc = 0x0038; break;
    case 1:
        g_z80.cyc += 13; push16(g_z80.pc); g_z80.pc = 0x0038; break;
    case 2: {
        uint16_t vector=(uint16_t)((g_z80.i << 8) | 0xFFu);
        uint16_t target=sms_read16(vector);
        g_z80.cyc += 19; push16(g_z80.pc); g_z80.pc=target; break;
    }
    }
}

void z80_recomp_init(void)
{
    memset(&g_z80,0,sizeof(g_z80));
    g_z80.a=0xFF; g_z80.f=0xFF; g_z80.sp=0xFFFF;
    z80_recomp_reset();
    s_fallback_steps=0;
    memset(s_fallback_seen, 0, sizeof(s_fallback_seen));
}

void z80_recomp_reset(void)
{
    g_z80.pc=0; g_z80.iff1=g_z80.iff2=false; g_z80.halted=false;
    g_z80.im=0; g_z80.ei_block=0;
    g_machine.z80.int_pending=0; g_machine.z80.nmi_pending=0;
}

uint32_t z80_recomp_step_one(void)
{
    uint64_t before=g_z80.cyc;
    if (g_z80.halted) g_z80.cyc += 4;
    else if (g_game_spec.z80_step) g_game_spec.z80_step();
    else sms_dispatch_miss(g_z80.pc);
    accept_interrupts();
    z80_recomp_mirror_to_interpreter(&g_machine.z80);
    return (uint32_t)(g_z80.cyc-before);
}

void z80_recomp_assert_irq(void) { g_machine.z80.int_pending=1; }
uint16_t z80_recomp_pc(void) { return g_z80.pc; }
int z80_recomp_irq_pending(void) { return g_machine.z80.int_pending ? 1:0; }
int z80_recomp_iff1(void) { return g_z80.iff1 ? 1:0; }
