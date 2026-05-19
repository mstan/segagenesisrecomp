/*
 * verify.h — Dual-execution verification system.
 *
 * Two modes:
 *   Phase 0 (default): interpreter-vs-interpreter.  No native code runs.
 *     Tests that the snapshot/restore/shadow mechanism is self-consistent.
 *     Expect 0 errors.  Game runs on pure interpreter.
 *
 *   Phase 1 (F8 toggle): native-vs-interpreter.
 *     Native overrides run, then shadow interpreter re-runs from the same
 *     pre-call state.  Divergences indicate codegen bugs.
 *
 * During shadow execution, VDP/IO writes are suppressed and the hybrid
 * dispatch hook is bypassed so the interpreter runs sub-calls natively.
 */

#ifndef VERIFY_H
#define VERIFY_H

#include <stdint.h>
#include "clowncommon.h"
#include "clownmdemu.h"
#include "clown68000.h"
#include "bus-common.h"

/* Global flag: non-zero while the shadow interpreter is running.
 * Checked by hybrid.c to bypass native dispatch during shadow. */
extern int g_verify_shadow_active;

/* Initialise the verification system.  Must be called after glue_init(). */
void VerifyInit(ClownMDEmu *emu, CPUCallbackUserData *cpu_data);

/* Toggle between phase 0 (interp-vs-interp) and phase 1 (native-vs-interp).
 * Returns the new phase number (0 or 1). */
int  VerifyTogglePhase(void);
int  VerifyGetPhase(void);

/* Called by hybrid.c BEFORE dispatch.  Returns non-zero if the caller
 * should SKIP the native function (phase 0 mode — verify handles everything
 * including advancing the emulator state past the function). */
int  VerifyBeforeDispatch(cc_u32l func_addr, cc_u32l ret_addr);

/* Snapshot current state (called by hybrid.c before native dispatch in phase 1). */
void VerifyTakeSnapshot(void);

/* Called by hybrid.c AFTER native dispatch + RTS simulation (phase 1 only).
 * Runs shadow interpreter and compares. */
void VerifyAfterNative(cc_u32l func_addr, cc_u32l ret_addr);

#endif /* VERIFY_H */
