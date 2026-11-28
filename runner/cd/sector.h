/* Raw CD-ROM sector checks used by the Sega CD host media layer. */
#ifndef SEGACD_SECTOR_H
#define SEGACD_SECTOR_H

#include <stdint.h>

typedef enum SegaCDSectorResult {
    SEGACD_SECTOR_MODE1_OK = 0,
    SEGACD_SECTOR_BAD_SYNC,
    SEGACD_SECTOR_UNSUPPORTED_MODE,
    SEGACD_SECTOR_BAD_EDC
} SegaCDSectorResult;

/* Checks sync, MODE1, and the stored EDC only. It does not validate the header
 * address against an LBA, inspect ECC, correct data, or repair bad sectors. */
SegaCDSectorResult segacd_sector_validate(const uint8_t raw[2352]);

#endif
