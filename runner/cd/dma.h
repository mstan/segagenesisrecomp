/* CDC DMA adapter. Derived behavior reference: ares/Mega-CD at
 * 9408cb43d4948fc3ea6e152a307a34348df3fe04, cdc-transfer.cpp and mcd.cpp.
 * Adapted code attribution and ISC notice are retained in dma.c.
 * Six-clock word pacing and byte-lane order are a reference profile, not
 * independently measured hardware timing. This implementation retains the local
 * LC8951 FIFO host-read contract and local mapped bus semantics. */
#ifndef SEGACD_DMA_H
#define SEGACD_DMA_H
#include <stdint.h>
struct SegaCD;
typedef enum {
    SEGACD_DMA_OK=0,
    SEGACD_DMA_BAD_DESTINATION,
    SEGACD_DMA_ODD_BYTE_COUNT,
    SEGACD_DMA_DISABLED,
    SEGACD_DMA_WAIT,
    SEGACD_DMA_UNMAPPED
} SegaCDDmaResult;
typedef struct {
    uint32_t address;       /* Internal byte address, modulo 2^19. */
    uint64_t words, waits;
    uint8_t phase;          /* Global sub-clock modulo-six phase. */
    uint8_t active,address_valid;
    SegaCDDmaResult last_result;
} SegaCDDma;
void segacd_dma_init(SegaCDDma *);
/* Call before DTTRG (CDC register 6). The A18..A3 register is shifted to a
 * byte address at initial setup; starts retain internal low bits until the
 * address register is explicitly rewritten and address_programmed is called. */
SegaCDDmaResult segacd_dma_begin(struct SegaCD *,SegaCDDma *);
/* Call after the gate-array FF800A/B address register is explicitly written. */
void segacd_dma_address_programmed(struct SegaCD *,SegaCDDma *);
/* Advances the global six-sub-clock cadence, even while inactive. Returns the
 * last event result (WAIT/UNMAPPED leaves FIFO, source and DMA address intact). */
SegaCDDmaResult segacd_dma_advance(struct SegaCD *,SegaCDDma *,uint32_t clocks);
/* Internal clock-only span: caller owns a non-null, currently inactive DMA.
 * Recheck activity after device callbacks. Preserve every field except phase;
 * an inactive controller still free-runs through the six-clock cadence. */
static inline void segacd_dma_idle_clock(SegaCDDma *dma,uint32_t clocks)
{
    uint32_t total=clocks+dma->phase;
    unsigned phase=total%6u;
    /* Carry contributes 2^32 mod 6 = 4. This also handles noncanonical phase
     * bytes imported into current-build state without overflowing the sum. */
    if(total<clocks) {
        phase+=4u;
        if(phase>=6u)phase-=6u;
    }
    dma->phase=(uint8_t)phase;
}
#endif
