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

#endif
