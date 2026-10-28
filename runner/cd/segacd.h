/* Sega CD devices, independent of a game, renderer, CPU implementation or OS.
 * Original implementation from Sega's Hardware Manual v1.0 (1991-10-14).
 * Addresses and register behavior are documented in docs/SEGA_CD_BACKEND.md.
 * The caller supplies its own BIOS. No BIOS/disc bytes belong in this library.
 */
#ifndef SEGACD_H
#define SEGACD_H
#include <stddef.h>
#include <stdint.h>
#include "rf5c164.h"
#include "cdd.h"
#include "cdda.h"
#include "cdc.h"
#include "stamp.h"
#include "dma.h"

typedef enum {
    SEGACD_MAIN = 0, SEGACD_SUB = 1
} SegaCDCpu;
typedef enum {
    SEGACD_UNMAPPED = 0, SEGACD_OK = 1,
    /* The scheduler must run the other processor until ownership changes.
     * A denied access never reads/writes the other processor's memory. */
    SEGACD_WAIT = 2, SEGACD_ADDRESS_ERROR = 3
} SegaCDResult;

/* Host media handles and sinks stay outside deterministic device state. The
 * raw audio callback is a timed CD-DA sector event, not a host resampler. */
typedef int (*SegaCDReadSector)(void *,int32_t lba,uint8_t raw[2352]);
typedef void (*SegaCDCddaSector)(void *,uint64_t sub_clock,int32_t lba,const uint8_t raw[2352]);
typedef void (*SegaCDPcmTimedSample)(void *,uint64_t sub_clock,int16_t left,int16_t right);
typedef struct {
    Rf5c164Sink pcm_sink;
    void *pcm_user;
    SegaCDReadSector read_sector;
    void *media_user;
    SegaCDCddaSector cdda_sector;
    void *cdda_user;
    SegaCDCddaSample cdda_sample;
    void *cdda_sample_user;
    /* Appended for positional-initializer compatibility. When present, this
     * exact subclock callback takes precedence over the legacy untimed PCM
     * sink; sample events use device cycles and the RF5C164 384-clock phase. */
    SegaCDPcmTimedSample pcm_timed_sink;
    void *pcm_timed_user;
} SegaCDIO;

enum {
    SEGACD_BIOS_BYTES = 0x20000, SEGACD_PRG_BYTES = 0x80000,
    SEGACD_WORD_BYTES = 0x40000, SEGACD_BACKUP_BYTES = 0x2000,
    SEGACD_SUB_CLOCK = 12500000, SEGACD_TIMER_DIVISOR = 384
};

/* Device state contains no host pointers. The CPU coordinator owns CPU state
 * and media handles separately. Do not serialize this C layout as a public
 * portable format: field padding and byte order are host implementation details.
 */
typedef struct SegaCD {
    Rf5c164 pcm;
    SegaCDDrive drive;
    SegaCDCdda cdda;
    SegaCDCdc cdc;
    SegaCDStamp stamp;
    SegaCDDma dma;
    uint8_t cdc_destination;
    uint16_t cdc_dma_address;
    uint8_t media_error; /* 1=missing/read failure; 2=unsupported sector mode. */
    uint8_t cdda_error; /* SegaCDCddaResult; sticky until device reset. */
    int32_t media_error_lba;
    uint64_t media_sectors,cdda_sectors;
    uint8_t bios[SEGACD_BIOS_BYTES];
    uint8_t prg[SEGACD_PRG_BYTES];
    uint8_t word[SEGACD_WORD_BYTES];
    uint8_t backup[SEGACD_BACKUP_BYTES];
    uint8_t main_control, write_protect, prg_bank, memory_mode;
    uint8_t irq_mask, irq_pending, flags[2], timer_reload;
    uint16_t command[8], status[8], hint_vector, led;
    uint16_t vdp_word_latch; /* Word RAM output latch on the VDP DMA path. */
    uint16_t font_bits;
    uint8_t font_color;
    uint32_t sub_reset_generation;
    uint64_t sub_cycles, stopwatch_origin, timer_origin, peripheral_ready;
} SegaCD;

int segacd_init(SegaCD *cd, const void *bios, size_t bytes);
/* Gate-array reset preserves BIOS, RAM and battery contents. */
void segacd_reset(SegaCD *cd);
/* Clock domain is the 12.5 MHz sub CPU; never wall-clock time. */
void segacd_advance(SegaCD *cd, uint32_t sub_cycles);
void segacd_advance_audio(SegaCD *cd,uint32_t sub_cycles,Rf5c164Sink,void *user);
void segacd_advance_io(SegaCD *,uint32_t sub_cycles,const SegaCDIO *);
unsigned segacd_irq_level(const SegaCD *cd);
void segacd_irq_ack(SegaCD *cd, unsigned level);
int segacd_sub_runnable(const SegaCD *cd);

SegaCDResult segacd_read8(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint8_t *value);
SegaCDResult segacd_read16(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint16_t *value);
/* Ephemeral contiguous current instruction bytes. No pointer is retained or
 * code identity trusted across operations. NULL keeps ordinary fetch/WAIT for
 * patches, mapping boundaries, ownership stalls and noncontiguous 1M views. */
const uint8_t *segacd_code_span(const SegaCD *,SegaCDCpu,uint32_t,size_t);
SegaCDResult segacd_write8(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint8_t value);
SegaCDResult segacd_write16(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint16_t value);

#endif
