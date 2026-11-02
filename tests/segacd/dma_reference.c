/* Compile the complete unoptimized device path under distinct public symbols.
 * The test compares whole device state and callback traces against this path. */
#define SEGACD_DMA_IDLE_SPANS 0
#define SEGACD_CELL_PERMUTATION 0
#define segacd_reset reference_segacd_reset
#define segacd_init reference_segacd_init
#define segacd_sub_runnable reference_segacd_sub_runnable
#define segacd_advance_io reference_segacd_advance_io
#define segacd_advance_audio reference_segacd_advance_audio
#define segacd_advance reference_segacd_advance
#define segacd_irq_level reference_segacd_irq_level
#define segacd_irq_ack reference_segacd_irq_ack
#define segacd_code_span reference_segacd_code_span
#define segacd_memory_region reference_segacd_memory_region
#define segacd_read8 reference_segacd_read8
#define segacd_read16 reference_segacd_read16
#define segacd_write8 reference_segacd_write8
#define segacd_write16 reference_segacd_write16
#include "../../runner/cd/segacd.c"
