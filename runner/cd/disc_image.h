/* Bounded SGCDIMG1 raw-sector image reader. The FILE handle is host state and
 * stays outside SegaCD device/simulation state. */
#ifndef SEGACD_DISC_IMAGE_H
#define SEGACD_DISC_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include "cdd.h"

typedef struct SegaCDImage SegaCDImage;

/* Opens and validates the complete image, including its payload SHA-256.
 * On failure, returns NULL and writes a short error when capacity is nonzero. */
SegaCDImage *segacd_image_open(const char *path, char *error, size_t capacity);
/* Optional complete-image expectation from an import/cache manifest. This is
 * not a verification token: both actual full-image and payload identities are
 * checked on every open, in the same pass. Retains a protected Windows object
 * or private portable snapshot for all later reads; fails closed on ownership
 * failure. NULL expectation retains ordinary open semantics. */
SegaCDImage *segacd_image_open_expected(const char *path,const uint8_t expected[32],
                                       char *error,size_t capacity);
void segacd_image_close(SegaCDImage *image);
/* Optional host execution service. The reader remains a pure portable module
 * with no SDL/thread dependency when no service is installed. try_start must
 * never join/execute inline; busy()==0 publishes completed writes; destroy
 * joins before returning. Replacement/close quiesces the prior service. */
typedef struct {
    void *(*create)(void);
    int (*try_start)(void *,void (*)(void *),void *);
    int (*busy)(void *);
    void (*destroy)(void *);
} SegaCDImageAsyncHost;
void segacd_image_set_async_host(SegaCDImage *,const SegaCDImageAsyncHost *);
void segacd_image_prefetch_cancel(SegaCDImage *);
const SegaCDTOC *segacd_image_toc(const SegaCDImage *image);
/* Returns nonzero on success. On invalid LBA or I/O failure, raw is unchanged. */
int segacd_image_read(SegaCDImage *image, int32_t lba, uint8_t raw[2352]);
const uint8_t *segacd_image_sha256(const SegaCDImage *image);
/* Actual complete-image identity, available only after an expected open. */
const uint8_t *segacd_image_whole_sha256(const SegaCDImage *image);
/* Optional immutable host residency, verified/prefaulted during full validation.
 * NULL means the requested sector is not resident; ordinary reads still work.
 * Pointers remain valid until close. This never advances a drive/device clock. */
const uint8_t *segacd_image_resident_sector(const SegaCDImage *, int32_t lba);
size_t segacd_image_resident_bytes(const SegaCDImage *);
/* Host-only storage and stream observations, excluding the validation pass.
 * Adjacent raw reads preserve FILE read-ahead; resident reads do not seek. */
typedef struct {
    uint64_t raw_reads,resident_reads,host_seeks;
    size_t mapped_view_bytes,private_resident_bytes;
    uint64_t prefetch_jobs,prefetch_hits,prefetch_failures,prefetch_cancels;
    uint64_t sync_read_ns,max_sync_read_ns;
    size_t prefetch_bytes;
    size_t private_snapshot_bytes;
    int whole_identity_checked;
} SegaCDImageHostStats;
int segacd_image_host_stats(const SegaCDImage *,SegaCDImageHostStats *);

#endif
