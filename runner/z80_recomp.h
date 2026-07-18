/* z80_recomp.h - Genesis adapter for the shared z80-recomp-core semantics. */
#pragma once

#include <stdint.h>
#include "sms_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Z80State g_z80;

void z80_recomp_init(void);
void z80_recomp_reset(void);
uint32_t z80_recomp_step_one(void);
void z80_recomp_assert_irq(void);
uint16_t z80_recomp_pc(void);
int z80_recomp_irq_pending(void);
int z80_recomp_iff1(void);
uint64_t z80_recomp_fallback_steps(void);
uint32_t z80_recomp_fallback_unique_pcs(void);

/* Keep the legacy SuperZazu-shaped state synchronized for existing debug,
 * frame-record, and co-sim consumers while the AOT experiment is opt-in. */
struct z80;
void z80_recomp_mirror_to_interpreter(struct z80 *out);
void z80_recomp_restore_from_interpreter(const struct z80 *in);

#ifdef __cplusplus
}
#endif
