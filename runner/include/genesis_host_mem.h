#ifndef GENESIS_HOST_MEM_H
#define GENESIS_HOST_MEM_H
/* genesis_host_mem.h — host-side access to guest memory (defined in glue.c).
 * Included by genesis_runtime.h (generated code, adapters) and glue.h (frame
 * loop, host services) so every host caller sees one declaration. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Host-side memory access ----
 * The m68k_read / m68k_write accessors ARE the emulated 68000's bus: every
 * call is a guest access and drives scheduler state (bus-watchdog count,
 * bus ring, same-address spin-yield streak, interleave budget check, Z80
 * sync-poll streak). Host code that merely inspects or edits guest memory
 * (frame loop bookkeeping, diagnostics, input scripts, TCP debug commands,
 * game-adapter features) must use these instead, so observing the machine
 * can never change how it executes.
 *   peek: side-effect free for every address (device ports return their
 *         current value without read side effects; see gbus_peek16).
 *   poke: RAM / SRAM / Z80 RAM are written directly; ROM is ignored; a poke
 *         to a device port performs that device write (the write IS the
 *         mutation) but still touches no 68K scheduler or diagnostic state. */
uint8_t  glue_peek8 (uint32_t addr);
uint16_t glue_peek16(uint32_t addr);
uint32_t glue_peek32(uint32_t addr);
void     glue_poke8 (uint32_t addr, uint8_t  val);
void     glue_poke16(uint32_t addr, uint16_t val);
void     glue_poke32(uint32_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_HOST_MEM_H */
