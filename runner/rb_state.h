#ifndef GENESIS_RB_STATE_H
#define GENESIS_RB_STATE_H
/*
 * rb_state.h -- the rollback state of one Genesis machine, as ONE section
 * table that drives both the in-memory snapshot and the partitioned digest.
 *
 * A snapshot taken between ticks (the game fiber suspended, the scheduler on
 * the main fiber) and loaded back makes the next genesis_sim_step() behave
 * exactly as it did the first time. Sections, in blob order:
 *
 *   exec    the suspended game fiber (registers + live stack) and the
 *           generated tail-frame head that points into it   [host addresses]
 *   cpu     g_cpu (68K registers)
 *   sched   glue.c scheduler state: cycle budget, IRQ debt, V-int latches,
 *           yield flags, busy-wait streaks, frame counters (glue_rb_sched_*)
 *   ram     68K work RAM (64 KB)
 *   machine VDP (VRAM/CRAM/VSRAM/regs/FSM), bus (Z80 RAM, SRAM, I/O, pads,
 *           YM timer B), superzazu Z80, the YM timer clock g_snd_frame and
 *           the owed VDP DMA stall; host pointers zeroed
 *   fm      ymfm YM2612 chip + wrapper sample clock/LPF + undrained samples
 *   psg     SN76489 + sample clock + undrained samples
 *   evq     the cycle-stamped audio event queue, compact (queued events only)
 *   input   the sealed GenesisSimInput and the tick counter
 *   game    GameSpec.rb_state_save/load (adapter statics), if provided
 *
 * DIGEST DOMAIN. Every section's digest is the FNV-1a of exactly the bytes
 * its snapshot writes, except exec, whose bytes are host addresses: exec is
 * digested address-free (yield site, running flag, resume PC, the guest-
 * visible tail-frame chain) and its raw stack is compared in-process by the
 * determinism probe (GENESIS_RB_PROBE) instead. Digesting only reads state:
 * it never goes through the emulated bus, so observing cannot perturb the
 * guest (the lesson of finding A, docs/NETPLAY.md).
 *
 * Partitions are finer than sections so a fork names what differs: the
 * machine section digests as vdp / bus / z80.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GenesisRbPart {
    GENESIS_RB_PART_EXEC = 0,
    GENESIS_RB_PART_CPU,
    GENESIS_RB_PART_SCHED,
    GENESIS_RB_PART_RAM,
    GENESIS_RB_PART_VDP,
    GENESIS_RB_PART_BUS,
    GENESIS_RB_PART_Z80,
    GENESIS_RB_PART_FM,
    GENESIS_RB_PART_PSG,
    GENESIS_RB_PART_EVQ,
    GENESIS_RB_PART_INPUT,
    GENESIS_RB_PART_GAME,
    GENESIS_RB_PART_COUNT
} GenesisRbPart;

typedef struct GenesisRbDigest {
    uint64_t master;                        /* fold of every partition */
    uint64_t part[GENESIS_RB_PART_COUNT];
} GenesisRbDigest;

const char *genesis_rb_part_name(int part);

/* Bytes a snapshot of the CURRENT state needs (the exec and evq sections
 * vary with the live stack depth and queue fill). */
size_t genesis_rb_bound(void);
/* Save into dst; returns bytes written, 0 if cap < bound or a section
 * failed. Must be called between ticks, from the scheduler fiber. */
size_t genesis_rb_save(void *dst, size_t cap);
/* Restore a blob produced by genesis_rb_save IN THIS PROCESS. Validates the
 * framing and every section length before changing anything; 1 on success.
 * On a section failure after validation (cannot happen for a blob this
 * process wrote) the machine is left partially restored and 0 returned. */
int genesis_rb_load(const void *src, size_t len);

/* Partitioned digest of the live state (reads only). */
void genesis_rb_digest(GenesisRbDigest *out);
/* 32-bit folds for the wire (recomp-net RNetRbDigestParts). */
uint32_t genesis_rb_fold32(uint64_t h);

/* Mutation self-test over the section table: for every section, a flipped
 * byte must change that section's partition(s) and no other, and loading
 * the original snapshot must restore the original digest exactly. Prints one
 * line per section; returns the number of failures. Offline only. */
int genesis_rb_selftest(void);

/* glue.c halves (not for general use). */
size_t   glue_rb_sched_save(void *dst, size_t cap);
int      glue_rb_sched_load(const void *src, size_t len);
size_t   glue_rb_exec_save(void *dst, size_t cap);
int      glue_rb_exec_load(const void *src, size_t len);
uint64_t glue_rb_exec_digest(uint64_t h);
int      glue_rb_fiber_stack_range(uintptr_t *lo, uintptr_t *top);

#ifdef __cplusplus
}
#endif

#endif /* GENESIS_RB_STATE_H */
