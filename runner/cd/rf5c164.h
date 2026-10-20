/* Portable Ricoh RF5C164. Chip byte addresses, independent of CPU bus lanes.
 * Sega Mega-CD PCM manual, sections 4.1-4.4 and 4.8-4.9. */
#ifndef RF5C164_H
#define RF5C164_H
#include <stdint.h>
typedef struct {
    uint8_t env,pan,fdl,fdh,lsl,lsh,start,on;
    uint32_t address; /* 16-bit wave address + 11 fractional bits */
} Rf5c164Channel;
typedef struct {
    Rf5c164Channel channel[8];
    uint8_t wave[65536];
    uint8_t selected,bank,enabled,disabled;
    uint16_t phase;
    int16_t left,right;
    uint64_t samples;
} Rf5c164;
typedef void (*Rf5c164Sink)(void *,int16_t left,int16_t right);
typedef void (*Rf5c164TimedSink)(void *,uint64_t subclock,int16_t left,int16_t right);
void rf5c164_init(Rf5c164 *);
/* Chip reset preserves external wave RAM. */
void rf5c164_reset(Rf5c164 *);
uint8_t rf5c164_read(const Rf5c164 *,unsigned chip_address);
void rf5c164_write(Rf5c164 *,unsigned chip_address,uint8_t);
void rf5c164_sample(Rf5c164 *,int16_t *left,int16_t *right);
/* One output sample per 384 input clocks, including silent samples. */
void rf5c164_advance(Rf5c164 *,uint32_t clocks,Rf5c164Sink,void *);
/* Optional exact-timestamp variant. start_subclock is the absolute 12.5 MHz
 * source time at the beginning of this advance interval. Samples occur when
 * the chip's 384-clock phase reaches a boundary; legacy sink behavior/state is
 * unchanged when callers continue to use rf5c164_advance. */
void rf5c164_advance_timed(Rf5c164 *,uint64_t start_subclock,uint32_t clocks,Rf5c164TimedSink,void *);
#endif
