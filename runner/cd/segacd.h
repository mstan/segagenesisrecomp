/* Sega CD devices, independent of a game, renderer, CPU implementation or OS.
 * Original implementation from Sega's Hardware Manual v1.0 (1991-10-14).
 * Addresses and register behavior are documented in docs/SEGA_CD_BACKEND.md.
 * The caller supplies its own BIOS. No BIOS/disc bytes belong in this library.
 */
#ifndef SEGACD_H
#define SEGACD_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SEGACD_MAIN = 0, SEGACD_SUB = 1
} SegaCDCpu;
typedef enum {
    SEGACD_UNMAPPED = 0, SEGACD_OK = 1,
    /* The scheduler must run the other processor until ownership changes.
     * A denied access never reads/writes the other processor's memory. */
    SEGACD_WAIT = 2, SEGACD_ADDRESS_ERROR = 3
} SegaCDResult;

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
    uint8_t bios[SEGACD_BIOS_BYTES];
    uint8_t prg[SEGACD_PRG_BYTES];
    uint8_t word[SEGACD_WORD_BYTES];
    uint8_t backup[SEGACD_BACKUP_BYTES];
    uint8_t main_control, write_protect, prg_bank, memory_mode;
    uint8_t irq_mask, irq_pending, flags[2], timer_reload;
    uint16_t command[8], status[8], hint_vector, led;
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
unsigned segacd_irq_level(const SegaCD *cd);
void segacd_irq_ack(SegaCD *cd, unsigned level);
int segacd_sub_runnable(const SegaCD *cd);

SegaCDResult segacd_read8(const SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint8_t *value);
SegaCDResult segacd_read16(const SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint16_t *value);
SegaCDResult segacd_write8(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint8_t value);
SegaCDResult segacd_write16(SegaCD *cd, SegaCDCpu cpu, uint32_t addr, uint16_t value);

#endif
