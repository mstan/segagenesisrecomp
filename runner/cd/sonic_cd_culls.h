/* Source-bound discovery of Sonic CD horizontal object culls. */
#ifndef SEGACD_SONIC_CD_CULLS_H
#define SEGACD_SONIC_CD_CULLS_H

#include "sonic_cd_view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SEGACD_CULL_CHECK_S1=0,
    SEGACD_CULL_TEST_BADNIK,
    SEGACD_CULL_FLOAT_BLOCK,
    SEGACD_CULL_MOVING_SPRING,
    SEGACD_CULL_SPRING,
    /* A complete short-form arithmetic window whose exact cleanup chain is
     * known, but whose per-zone object identity is deliberately unspecified. */
    SEGACD_CULL_SHORT_CLEANUP,
    SEGACD_CULL_FAMILY_COUNT
} SegaCDCullFamily;

/* Owned 70-file audit maximum: five fixed source families plus eight exact
 * short-cleanup windows in the R31/R32/R33 overlays. */
enum { SEGACD_CULL_MAX_SITES=13 };

typedef enum {
    SEGACD_CULL_CLEANUP_NONE=0,
    SEGACD_CULL_CLEANUP_DESPAWN_OBJECT,
    SEGACD_CULL_CLEANUP_DELETE_OBJECT,
    SEGACD_CULL_CLEANUP_PLATFORM_DESTROY
} SegaCDCullCleanupKind;

typedef struct {
    SegaCDCullFamily family;
    uint32_t function_pc; /* 0 when source does not identify a fixed family. */
    uint32_t subtract_pc; /* SUBI.W #$80,D1 in the source sequence. */
    uint32_t compare_pc;  /* CMPI.W #$280,D0 in the source sequence. */
    uint32_t branch_pc;   /* BHI opcode in the source cull window. */
    uint32_t cleanup_pc;  /* Proven helper target; 0 when not source-classified. */
    SegaCDCullCleanupKind cleanup_kind;
    uint8_t object_id;    /* 0 for the shared CheckObjDespawnS1 helper. */
    uint8_t object_x_offset;
    uint8_t object_register; /* D0 */
    uint8_t camera_register; /* D1 */
} SegaCDCullSite;

typedef struct {
    SegaCDCullSite sites[SEGACD_CULL_MAX_SITES];
    size_t site_count;
    /* Families omitted from this MMD's source ObjectIndex are reported here.
     * A nonzero unrelated handler at that object ID is also absent when it
     * fails the full family signature; it is never treated as a cull site. */
    uint32_t absent_families;
    /* R43C contains one exact short-form window whose JMP enters owned words
     * that are extension/data, not a proven cleanup routine. It stays native
     * original and is surfaced here rather than counted absent or qualified. */
    uint32_t unqualified_short_cleanup_pc;
    uint8_t unqualified_short_cleanup_count;
} SegaCDCullDiscovery;

/* Read-only discovery. Requires the unique source RunObjects/ObjectIndex
 * root, validates each object handler's indexed state table where applicable,
 * and requires a complete source-specific function signature. Returns 0 for
 * malformed images, ambiguous roots or partial/mismatched source handlers.
 * Object families legitimately not used by the selected stage are reported
 * in absent_families and do not fail discovery. */
int segacd_culls_discover(const SegaCDViewSnapshot *snapshot,
                          SegaCDCullDiscovery *out);

#ifdef __cplusplus
}
#endif
#endif
