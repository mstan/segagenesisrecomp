/*
 * vdp_integration.h — hooks that splice the clean-room genesis_vdp into the
 * runner. Compiled only when PERMISSIVE_VDP is defined; all call sites in
 * glue.c / main.c are guarded by `#if PERMISSIVE_VDP` so the default
 * (clownmdemu) build is byte-for-byte unaffected.
 *
 * Phase 1 (this seam): run our VDP as a SHADOW — mirror the 68K's VDP-port
 * writes into it, and at each scanline-rendered callback substitute OUR
 * rendered line for display. clownmdemu still drives timing/interrupts, so this
 * isolates and validates the renderer with no risk to the working build. Phase
 * 2 replaces the scheduler/bus to drop clownmdemu's VDP from the binary.
 */
#ifndef VDP_INTEGRATION_H
#define VDP_INTEGRATION_H

#include <stdint.h>

void gvdp_integration_init(void);

/* Mirror a 16-bit 68K bus write; no-op unless `byte_addr` hits the VDP ports
 * ($C00000-$C00007). Called from glue.c's m68k_write paths. */
void gvdp_on_bus_write(uint32_t byte_addr, uint16_t value);

/* Render scanline `line` from our VDP into `row` (ARGB8888), up to `max_w`
 * pixels. Returns pixels written. Called from main.c's scanline callback. */
int gvdp_render_substitute(int line, uint32_t *row, int max_w);

#endif /* VDP_INTEGRATION_H */
