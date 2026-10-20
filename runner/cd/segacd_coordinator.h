/* Deterministic explicit-PC dual-68000 coordinator. No OS clock or game code. */
#ifndef SEGACD_COORDINATOR_H
#define SEGACD_COORDINATOR_H
#include "segacd.h"
#include "m68k_step.h"

typedef struct SegaCDCoordinator SegaCDCoordinator;
typedef struct {SegaCDCoordinator *owner;SegaCDCpu cpu;} SegaCDBusBinding;
struct SegaCDCoordinator {
    SegaCD *devices;
    M68kStepContext cpu[2];
    SegaCDBusBinding binding[2];
    M68kStepBus host_main;
    void *host_user;
    SegaCDIO io;
    /* Host Genesis devices (VDP/Z80) advance on the same emulated timeline,
     * including while either CPU waits for memory. IRQs may be queued here;
     * CPU execution/module mutation must not reenter from these callbacks. */
    void (*host_advance)(void *,uint64_t);
    uint64_t (*host_next_event)(void *);
    const M68kStepModule *const *modules[2];
    size_t module_count[2];
    M68kStepResult (*floor_step)(M68kStepContext *);
    uint64_t native_steps[2],floor_steps[2];
    /* Elapsed time in sub-CPU ticks, distinct from retired CPU cycle counts.
     * Main conversion retains the rational remainder (NTSC master / 7). */
    uint64_t time[2],device_time,main_remainder;
    uint64_t waits;
    uint32_t reset_generation,wait_address;
    unsigned wait_limit,main_irq,last_cpu,wait_cpu;
    M68kStepResult last_result,wait_result;
    uint8_t busy[2],sub_initialized,sub_was_running;
};

int segacd_coordinator_init(SegaCDCoordinator *,SegaCD *,const M68kStepBus *host_main,void *host_user);
/* Descriptors/arrays must remain immutable and alive during execution. */
int segacd_coordinator_modules(SegaCDCoordinator *,SegaCDCpu,
                              const M68kStepModule *const *,size_t);
/* A full-system consumer must attach its qualified portable floor. NULL is
 * useful for AOT coverage diagnostics, where a missing operation is a stop. */
int segacd_coordinator_floor(SegaCDCoordinator *,M68kStepResult (*)(M68kStepContext *));
void segacd_coordinator_main_irq(SegaCDCoordinator *,unsigned level);
void segacd_coordinator_host_clock(SegaCDCoordinator *,void (*advance)(void *,uint64_t),uint64_t (*next_event)(void *));
void segacd_coordinator_pcm_sink(SegaCDCoordinator *,Rf5c164Sink,void *user);
/* Configure only between completed CPU operations. Caller mounts the matching
 * TOC on devices->drive and keeps host handles alive until unmounted. */
void segacd_coordinator_media(SegaCDCoordinator *,SegaCDReadSector,void *media_user,
                             SegaCDCddaSector,void *cdda_user);
/* Run until both elapsed CPU timelines reach the absolute sub-clock deadline.
 * Returns at an instruction boundary; STOP advances emulated device time.
 * MISS/faults are diagnostic stops, never skipped code. */
M68kStepResult segacd_coordinator_run_until(SegaCDCoordinator *,uint64_t deadline);
/* One scheduler boundary, useful for instruction/interrupt observations.
 * An idle boundary advances at most to the next timer event / 384 ticks. */
M68kStepResult segacd_coordinator_step(SegaCDCoordinator *);
#endif
