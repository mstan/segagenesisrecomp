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
void segacd_image_close(SegaCDImage *image);
const SegaCDTOC *segacd_image_toc(const SegaCDImage *image);
/* Returns nonzero on success. On invalid LBA or I/O failure, raw is unchanged. */
int segacd_image_read(SegaCDImage *image, int32_t lba, uint8_t raw[2352]);
const uint8_t *segacd_image_sha256(const SegaCDImage *image);
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
} SegaCDImageHostStats;
int segacd_image_host_stats(const SegaCDImage *,SegaCDImageHostStats *);

#endif
