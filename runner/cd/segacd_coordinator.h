/* Deterministic explicit-PC dual-68000 coordinator. No OS clock or game code. */
#ifndef SEGACD_COORDINATOR_H
#define SEGACD_COORDINATOR_H
#include "segacd.h"
#include "m68k_step.h"
#include "catalog_lookup.h"
#include "catalog_index.h"

typedef struct SegaCDCoordinator SegaCDCoordinator;
typedef struct {SegaCDCoordinator *owner;SegaCDCpu cpu;} SegaCDBusBinding;
#define SEGACD_CDD_TRACE_CAPACITY 64u
typedef struct {
    uint8_t packet[10];
    uint32_t instruction_pc;
    uint64_t device_clock,command_deadline;
    int32_t lba;
    uint8_t status;
} SegaCDCddSubmission;
typedef struct {
    uint8_t tx[10],pending[10],control,status,command_pending;
    int32_t lba,seek_target;
    uint64_t device_clock,command_deadline,accepted_total;
    size_t retained_count;
} SegaCDCddSnapshot;
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
    /* Optional independent master-clock ceiling (for example a running Z80).
     * It is a read-only timestamp callback and must not enter the coordinator. */
    uint64_t (*device_limit)(void *);
    const M68kStepModule *const *modules[2];
    size_t module_count[2];
    M68kStepResult (*floor_step)(M68kStepContext *);
    uint64_t native_steps[2],floor_steps[2];
    /* Accepted interrupt diagnostics. Count only exceptions actually entered,
     * not merely asserted/requested interrupt levels. */
    uint64_t interrupts[2][8];
    uint32_t last_interrupt_pc[2],last_interrupt_target[2];
    uint8_t last_interrupt_vector[2],last_interrupt_level[2];
    /* Elapsed time in sub-CPU ticks, distinct from retired CPU cycle counts.
     * Main conversion retains the rational remainder (NTSC master / 7). */
    uint64_t time[2],device_time,main_remainder;
    uint64_t waits;
    uint32_t reset_generation,wait_address;
    unsigned wait_limit,main_irq,last_cpu,wait_cpu;
    M68kStepResult last_result,wait_result;
    uint8_t busy[2],sub_initialized,sub_was_running;
    /* External Main byte operations pause the Main timeline while Sub owns
     * Word RAM. A host DMA stall in that interval has no safe CPU stack to
     * charge; report the unsupported stall instead of charging Main early. */
    uint8_t external_active,external_stall_seen;
    /* Read-only diagnostic history for accepted, non-poll CDD commands.
     * Kept here so the sealed SegaCD device ABI stays unchanged. */
    SegaCDCddSubmission cdd_submissions[SEGACD_CDD_TRACE_CAPACITY];
    uint64_t cdd_submission_total;
    /* Derived host lookup only. Live opcode/extension guards still execute. */
    SegaCDCatalogLookup catalog_lookup[2];
    SegaCDCatalogIndex *catalog_index[2];
    uint8_t catalog_index_enabled;
};

int segacd_coordinator_init(SegaCDCoordinator *,SegaCD *,const M68kStepBus *host_main,void *host_user);
/* Descriptors/arrays must remain immutable and alive during execution. */
int segacd_coordinator_modules(SegaCDCoordinator *,SegaCDCpu,
                              const M68kStepModule *const *,size_t);
/* Optional bounded derived lookup; fixed-cache execution remains the default
 * for generic callers. Disable before destroying an enabled coordinator.
 * Configure between completed operations; allocation failure retains fallback. */
int segacd_coordinator_catalog_index(SegaCDCoordinator *,int enabled);
/* Diagnostics may attach the portable execution floor. Native-only consumers
 * leave it NULL: every uncovered operation is a stop, never skipped code. */
int segacd_coordinator_floor(SegaCDCoordinator *,M68kStepResult (*)(M68kStepContext *));
void segacd_coordinator_main_irq(SegaCDCoordinator *,unsigned level);
void segacd_coordinator_host_clock(SegaCDCoordinator *,void (*advance)(void *,uint64_t),uint64_t (*next_event)(void *));
void segacd_coordinator_device_limit(SegaCDCoordinator *,uint64_t (*limit)(void *));
void segacd_coordinator_pcm_sink(SegaCDCoordinator *,Rf5c164Sink,void *user);
/* Emulation-thread 44.1 kHz CDDA frames, after the guest fader. Host rate
 * conversion/mixing belongs outside deterministic device state. */
void segacd_coordinator_cdda_sink(SegaCDCoordinator *,SegaCDCddaSample,void *user);
/* Host VDP DMA bus callback: Word RAM has a one-access output delay (Sega
 * Hardware p61); normal CPU reads have none. First-latch prefetch is not yet
 * bus-phase qualified. These calls must use the main CPU's execution stack. */
uint16_t segacd_coordinator_vdp_read16(SegaCDCoordinator *,uint32_t address);
/* Charge a host DMA freeze during a main operation. The operation's actual
 * retirement then advances elapsed time, including the other processor. */
void segacd_coordinator_main_stall(SegaCDCoordinator *,uint32_t main_cycles);
/* Configure only between completed CPU operations. Caller mounts the matching
 * TOC on devices->drive and keeps host handles alive until unmounted. */
void segacd_coordinator_media(SegaCDCoordinator *,SegaCDReadSector,void *media_user,
                             SegaCDCddaSector,void *cdda_user);
/* Run until both elapsed CPU timelines reach the absolute sub-clock deadline.
 * If a device_limit is installed, deadline must not exceed its current value;
 * otherwise returns NO_WAIT_HANDLER without advancing CPUs/devices. Returns
 * at an instruction boundary; STOP advances emulated device time. MISS/faults
 * are diagnostic stops, never skipped code. */
M68kStepResult segacd_coordinator_run_until(SegaCDCoordinator *,uint64_t deadline);
/* One scheduler boundary, useful for instruction/interrupt observations.
 * An idle boundary advances at most to the next timer event / 384 ticks. */
M68kStepResult segacd_coordinator_step(SegaCDCoordinator *);
/* External master byte access. A CD ownership WAIT runs only the actual Sub
 * CPU, then retries this byte once grant is available. No CPU may be busy at
 * entry; host callbacks must not execute CPUs. Host M68K_BUS_WAIT and a VDP
 * DMA stall during this operation return NO_WAIT_HANDLER. Successful earlier
 * external bus effects are retained. Read output changes only on success. */
M68kStepResult segacd_coordinator_external_main_read8(SegaCDCoordinator *,uint32_t,uint8_t *);
M68kStepResult segacd_coordinator_external_main_write8(SegaCDCoordinator *,uint32_t,uint8_t);
/* Snapshot current CDD packet/state and copy up to capacity retained accepted
 * commands into chronological order (oldest first among the copied entries).
 * Returns the number of entries copied; snapshot reports total and retained. */
size_t segacd_coordinator_cdd_trace(const SegaCDCoordinator *,SegaCDCddSnapshot *,
                                    SegaCDCddSubmission *,size_t capacity);
#endif
