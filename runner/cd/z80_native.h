/* Instruction-sized generated Z80 execution on the Sega CD shared clock.
 * Link instead of runner/z80_recomp.c: both supply the sms_runtime ABI.
 * No decoder/interpreter is linked by this adapter. */
#ifndef SEGACD_Z80_NATIVE_H
#define SEGACD_Z80_NATIVE_H
#include "segacd_coordinator.h"
#include "sms_runtime.h"

typedef enum {
    SEGACD_Z80_OK, SEGACD_Z80_MISS, SEGACD_Z80_BUS_ERROR,
    SEGACD_Z80_REENTERED, SEGACD_Z80_BAD_MODULE
} SegaCDZ80Result;

/* Local callbacks implement $0000..$7fff (RAM, YM, serial bank, PSG).
 * Banked $8000..$ffff always uses the coordinator Main byte bus, including
 * ownership WAIT. Callbacks run on the emulation thread and must not enter
 * another Z80 operation. subclock is absolute, not a frame-relative stamp. */
typedef struct {
    uint8_t (*read)(void *,uint16_t,uint64_t);
    void (*write)(void *,uint16_t,uint8_t,uint64_t);
    uint16_t (*bank)(void *);
} SegaCDZ80Bus;

typedef struct {
    Z80State state;
    uint64_t time,remainder,retired,wait_ticks;
    SegaCDZ80Result result;
    M68kStepResult bus_result;
    uint16_t miss_pc;
    uint8_t irq_pending,in_step;
    /* Host bindings: exclude from persistent/simulation state snapshots. */
    SegaCDCoordinator *coordinator;
    SegaCDZ80Bus bus;
    void *user;
    void (*step)(void);
} SegaCDZ80;

int segacd_z80_init(SegaCDZ80 *,SegaCDCoordinator *,const SegaCDZ80Bus *,
                   void *user,void (*generated_step)(void));
/* Reset CPU control latches at the current shared boundary. RAM belongs to
 * the host bus and is not cleared. A fatal result requires a system restart. */
void segacd_z80_reset(SegaCDZ80 *);
void segacd_z80_irq(SegaCDZ80 *);
/* A BUSREQ/reset-held interval consumes elapsed time without executing or
 * changing registers. Call only while the Z80 is externally held. */
int segacd_z80_hold_until(SegaCDZ80 *,uint64_t subclock);
SegaCDZ80Result segacd_z80_step(SegaCDZ80 *);
/* Current callback's clock (including preceding waits), otherwise state.time. */
uint64_t segacd_z80_bus_time(const SegaCDZ80 *);
#endif
