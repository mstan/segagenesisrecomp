#ifndef GENESIS_RB_PROBE_H
#define GENESIS_RB_PROBE_H
/* rb_probe.h -- offline rollback determinism probe (see rb_probe.c). */
#include "sim_step.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The hooks a REPLAYED tick runs with (no render, simulation-affecting
 * pre-raster work only, no observation). Set once by the host. */
void rb_probe_set_resim_hooks(const GenesisSimHooks *hooks);
int  rb_probe_armed(void);
/* Around every live tick, on the scheduler fiber. */
void rb_probe_pre_tick(void);
void rb_probe_post_tick(void);
/* A successful external file load starts another timeline. An unfinished
 * sealed-input window cannot compare across that operation; report it as
 * discarded rather than as a passed or divergent replay. */
void rb_probe_external_state_loaded(void);
void rb_probe_summary(void);

#ifdef __cplusplus
}
#endif

#endif
