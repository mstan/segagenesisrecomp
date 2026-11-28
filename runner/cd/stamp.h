/* Mega-CD stamp/trace-vector graphics device. No renderer or host pointers.
 * Register/pixel rules: Sega Hardware Manual v1.0 pp35-54.
 * The functional line scheduler is provisional; bus contention is not yet
 * qualified. It must not be used as evidence of hardware cycle accuracy. */
#ifndef SEGACD_STAMP_H
#define SEGACD_STAMP_H
#include <stdint.h>
typedef struct {
    uint16_t reg[8], active[7];
    uint32_t vector, line, line_clocks, remaining_clocks;
    uint8_t busy;
    uint64_t operations, pixels;
} SegaCDStamp;
void segacd_stamp_reset(SegaCDStamp *);
uint16_t segacd_stamp_read(const SegaCDStamp *,unsigned reg);
void segacd_stamp_latch(SegaCDStamp *,unsigned reg,uint16_t value);
/* Caller checks 2M Word RAM ownership before starting. */
void segacd_stamp_start(SegaCDStamp *,uint16_t vector);
/* Returns one on completion. Word RAM is big-endian physical storage. */
int segacd_stamp_advance(SegaCDStamp *,uint8_t word[0x40000],unsigned priority,uint32_t clocks);
#endif
