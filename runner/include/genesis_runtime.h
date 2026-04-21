/*
 * genesis_runtime.h — Genesis/Mega Drive runtime interface.
 * Shared between runner/ and generated/ code.
 * Generated code includes this; runner implements it.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ---- CPU State ---- */
typedef struct {
    uint32_t D[8];   /* D0-D7 data registers */
    uint32_t A[8];   /* A0-A6 address registers, A7 = SSP (supervisor stack) */
    uint16_t SR;     /* Status register: T,S,I2,I1,I0,X,N,Z,V,C */
    uint32_t PC;     /* Program counter (for dynamic dispatch / debug) */
    uint32_t USP;    /* User Stack Pointer (shadow register, separate from A7) */
} M68KState;

extern M68KState g_cpu;
extern uint8_t   g_rom[0x400000];   /* 4MB ROM map */
extern uint8_t   g_ram[0x010000];   /* 64KB main RAM ($FF0000-$FFFFFF) */

/* ---- Memory Map ---- */
#define ROM_BASE    0x000000u
#define ROM_SIZE    0x400000u
#define RAM_BASE    0xFF0000u
#define RAM_SIZE    0x010000u
#define VDP_BASE    0xC00000u
#define Z80_BASE    0xA00000u
#define IO_BASE     0xA10000u

/* SR flag bits */
#define SR_C   (1u << 0)   /* Carry */
#define SR_V   (1u << 1)   /* Overflow */
#define SR_Z   (1u << 2)   /* Zero */
#define SR_N   (1u << 3)   /* Negative */
#define SR_X   (1u << 4)   /* Extend */
#define SR_I0  (1u << 8)   /* Interrupt mask bit 0 */
#define SR_I1  (1u << 9)   /* Interrupt mask bit 1 */
#define SR_I2  (1u << 10)  /* Interrupt mask bit 2 */
#define SR_S   (1u << 13)  /* Supervisor mode */
#define SR_T   (1u << 15)  /* Trace */

/* ---- Memory Interface ---- */
uint8_t  m68k_read8 (uint32_t addr);
uint16_t m68k_read16(uint32_t addr);
uint32_t m68k_read32(uint32_t addr);
void     m68k_write8 (uint32_t addr, uint8_t  val);
void     m68k_write16(uint32_t addr, uint16_t val);
void     m68k_write32(uint32_t addr, uint32_t val);

/* ---- Dispatch ---- */
/* Called for JMP (An) and indexed jump tables — dispatch to the correct recompiled function */
void call_by_address(uint32_t addr);

/* Logging for dispatch misses */
void genesis_log_dispatch_miss(uint32_t addr);

/* JMP table interpreter fallback — runs interpreter from target_pc until RTS */
void hybrid_jmp_interpret(uint32_t target_pc);

/* call_by_address interpreter fallback — runs interpreter when dispatch misses */
void hybrid_call_interpret(uint32_t target_pc);

/* ---- Fiber yield (Step 2) ---- */
void glue_yield_for_vblank(void);

/* ---- VDP Interface ---- */
void vdp_write_data(uint16_t val);
void vdp_write_ctrl(uint16_t val);
uint16_t vdp_read_data(void);
uint16_t vdp_read_status(void);
void vdp_render_frame(uint32_t *framebuf);   /* 320x224 or 256x224 ARGB8888 */

/* ---- Runtime Init ---- */
void runtime_init(void);

/* ---- Frame sync ---- */
/* Called by the runner at 60Hz. Signals the game thread to service VBlank+HBlank
 * on its own stack (eliminating g_ram races), then blocks until complete. */
void runtime_request_vblank(void);

/* ---- Debug: function call counter (temporary) ---- */
extern int g_dbg_b64_count;   /* incremented at start of func_000B64 */
extern int g_dbg_b5e_count;   /* incremented at start of func_000B5E */
extern int g_dbg_b88_count;   /* incremented at start of func_000B88 */

/* ---- RTE propagation ---- */
/* Set to 1 by RTE; checked after every call_by_address / JSR to propagate
 * the exception-return back up the C call chain.  Cleared by the VBlank
 * service routine after the handler chain completes. */
/* g_rte_pending is accessed via a pointer so the runner can redirect it
 * to a dummy variable during VBlank service, suppressing RTE propagation
 * inside the interrupt handler chain.  The handler's MOVEM + RTE stack
 * management is handled by force-restoring A7 in glue_service_vblank(). */
extern int *g_rte_pending_ptr;
#define g_rte_pending (*g_rte_pending_ptr)

/* ---- Early return (addq.l #4,sp + rts pattern) ---- */
/* 68K code uses "addq.l #4,sp" to discard a return address, then "rts" to
 * return to the caller's caller (skipping the rest of the current routine).
 * In C, "return" always goes to the immediate caller.  g_early_return counts
 * how many extra return levels to propagate.  At RTS, if > 0, it decrements
 * and sets g_rte_pending so the caller's post-JSR check triggers a return. */
extern int g_early_return;

/* ---- Frame counter ---- */
extern uint64_t g_frame_count;

/* ---- Instruction-count telemetry (Stage C) ----
 * Ticked by generated C once per decoded 68K instruction; defined in
 * runner/glue.c. 0 on oracle (generated functions don't execute). */
extern uint64_t g_native_insn_count;

/* ---- Contextual recompiler: cycle-accurate VBlank ---- */
/* Accumulated 68K cycles since frame start. When this reaches the VBlank
 * threshold (~109312 = scanline 224 × 488 cycles), glue_check_vblank()
 * fires the VBlank handler between instructions. */
extern uint32_t g_cycle_accumulator;
extern uint32_t g_vblank_threshold;
void glue_check_vblank(void);

/* ---- Controller ---- */
/* 3-button pad bitmask: bit5=C, bit4=B, bit3=A,
 *   bit2=Start, bit1=Up, bit0=Down (bit6=Left? depends on read mode)
 * Full 6-button pad uses an extended protocol — stub for now. */
extern uint8_t g_controller1_buttons;
extern uint8_t g_controller2_buttons;

/* ---- Dispatch miss monitor ---- */
extern uint32_t g_miss_count_any;
extern uint32_t g_miss_last_addr;
extern uint64_t g_miss_last_frame;
#define MAX_MISS_UNIQUE 64
extern uint32_t g_miss_unique_addrs[MAX_MISS_UNIQUE];
extern int      g_miss_unique_count;

/* ---- Logger ---- */
void log_on_change(const char *label, uint32_t value);
