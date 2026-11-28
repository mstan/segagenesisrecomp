/* Guarded native Sonic CD horizontal-view bounds.
 *
 * This opt-in step module changes only source-identified bounds. It does not
 * replace guest simulation, object creation/deletion, the native SAT queue,
 * or any display mode. Width 320 creates an empty module.
 */
#ifndef SEGACD_SONIC_CD_VIEW_H
#define SEGACD_SONIC_CD_VIEW_H

#include "m68k_step.h"
#include "sonic_cd_limits.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SEGACD_VIEW_MAX_ENTRIES = 41 };

typedef struct {
    const uint8_t *word_ram;
    size_t word_ram_size;
    uint32_t word_ram_base;
    const uint8_t *main_ram;
    size_t main_ram_size;
    uint32_t main_ram_base;
} SegaCDViewSnapshot;

/* Called at the original DrawObjects entry, after its normal LEA has written
 * A2 and before guest queue processing begins. The callback may inspect CPU
 * registers and its separately-owned read-only machine RAM, but must not
 * mutate state, execute a CPU, or retain the transient context pointer. */
typedef void (*SegaCDViewQueueCapture)(void *user,
                                      const M68kStepContext *cpu);
/* Called immediately before the original spriteCount.w store, after the guest
 * has completely built sprites.w. It may inspect D5 and the native SAT buffer
 * through user-owned read-only machine RAM. The guest store still executes. */
typedef void (*SegaCDViewSatCapture)(void *user,
                                    const M68kStepContext *cpu);

typedef struct {
    SegaCDViewQueueCapture queue_capture;
    void *queue_capture_user;
    SegaCDViewSatCapture sat_capture;
    void *sat_capture_user;
} SegaCDViewObservers;

typedef struct {
    M68kStepModule module;
    M68kStepEntry entries[SEGACD_VIEW_MAX_ENTRIES];
    /* Nearby original code is checked again on every intercepted operation.
     * A disc load can replace Word RAM within a frame, before the host gets
     * another opportunity to rebuild its module list. */
    const uint8_t *guard_image;
    size_t guard_image_size;
    uint32_t guard_offset[SEGACD_VIEW_MAX_ENTRIES];
    uint8_t guard_bytes[SEGACD_VIEW_MAX_ENTRIES][32];
    uint8_t guard_length[SEGACD_VIEW_MAX_ENTRIES];
    uint32_t spawn_anchor_offset;
    uint32_t object_layout_offset;
    uint32_t first_object_offset;
    uint16_t refresh_chunk;
    uint16_t refresh_underflow_result;
    uint8_t refresh_phase;
    uint8_t refresh_underflow_clamped;
    SegaCDViewQueueCapture queue_capture;
    void *queue_capture_user;
    SegaCDViewSatCapture sat_capture;
    void *sat_capture_user;
    uint32_t image_base;
    uint32_t image_size;
    uint16_t requested_width;
    uint16_t effective_width;
    uint16_t pixel_margin;
    uint16_t chunk_margin;
    uint8_t configured;
    uint8_t installed;
} SegaCDView;

void segacd_view_init(SegaCDView *view);

/* Rebuild while detached from every coordinator/catalog array. Configure is
 * intended between completed frames. The Word RAM storage must remain alive
 * while installed; its current bytes provide the per-operation source guard.
 * Main RAM is read only during configuration. Width must be 320 through
 * SEGACD_SONIC_CD_MAX_VIEW_WIDTH so all signed-word spawn bounds fit. Pixel margin is
 * floor((width-320)/2); the spawn/despawn margin is rounded up to 128-pixel
 * source chunks. Odd width differences round down to the largest symmetric
 * even-width view. Any missing or
 * ambiguous source anchor/site clears the module and returns 0. */
int segacd_view_configure(SegaCDView *view,
                          const SegaCDViewSnapshot *snapshot,
                          unsigned requested_width,
                          SegaCDViewQueueCapture capture,
                          void *capture_user);

/* Extended observer form. Either callback may be NULL; each observer entry
 * preserves the original instruction's architectural effects and cycles. */
int segacd_view_configure_observed(SegaCDView *view,
                                   const SegaCDViewSnapshot *snapshot,
                                   unsigned requested_width,
                                   const SegaCDViewObservers *observers);

/* Caller inserts this descriptor before original catalogs only after a
 * successful non-320 configuration. The descriptor and entries are owned by
 * SegaCDView and must remain alive until detached. */
const M68kStepModule *segacd_view_module(const SegaCDView *view);

/* Call after the coordinator has detached/installed the descriptor. This
 * enforces one active view module per process and prevents hidden mutation
 * while the CPU can execute it. Pass installed=0 before configure; pass 1
 * only after the coordinator's between-operation module setter has installed
 * this descriptor. */
int segacd_view_mark_installed(SegaCDView *view, int installed);

/* Request one backward then one forward call through the original native
 * SpawnObjects paths. The host calls this only when the rounded 128-pixel
 * spawn margin grows (including first mid-level widescreen enable). Request
 * is accepted only while detached, for the currently configured live image,
 * after the original spawner has completed initialization. It does not set
 * object flags or spawn objects itself. */
int segacd_view_request_spawn_refresh(SegaCDView *view,
                                      const SegaCDViewSnapshot *snapshot);
/* Nonzero while the two source-native refresh passes are pending. */
int segacd_view_spawn_refresh_pending(const SegaCDView *view);

#ifdef __cplusplus
}
#endif
#endif
