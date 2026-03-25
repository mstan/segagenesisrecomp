/*
 * vdp.h — Sega Genesis VDP (Video Display Processor) interface.
 * The VDP is a Yamaha 315-5313 derivative.
 *
 * Registers accessed via $C00004 (control port) and $C00000 (data port).
 * DMA transfers, scroll planes A/B/Window, sprites, CRAM, VSRAM — all TODO.
 */
#pragma once
#include <stdint.h>

/* VDP register count */
#define VDP_REG_COUNT 24

/* VDP status register bits */
#define VDP_STATUS_FIFO_EMPTY  (1u << 9)
#define VDP_STATUS_FIFO_FULL   (1u << 8)
#define VDP_STATUS_VBLANK      (1u << 3)
#define VDP_STATUS_HBLANK      (1u << 2)
#define VDP_STATUS_DMA_BUSY    (1u << 1)
#define VDP_STATUS_PAL         (1u << 0)

/* VDP VRAM size: 64KB */
#define VDP_VRAM_SIZE  0x10000
/* CRAM: 64 words (128 bytes) */
#define VDP_CRAM_SIZE  128
/* VSRAM: 40 words (80 bytes) */
#define VDP_VSRAM_SIZE 80

extern uint8_t  g_vdp_vram[VDP_VRAM_SIZE];
extern uint16_t g_vdp_cram[64];
extern uint16_t g_vdp_vsram[40];
extern uint8_t  g_vdp_regs[VDP_REG_COUNT];

#define GENESIS_SCREEN_W 320
#define GENESIS_SCREEN_H 224

void     vdp_init(void);
void     vdp_lock(void);
void     vdp_unlock(void);
void     vdp_write_data(uint16_t val);
void     vdp_write_ctrl(uint16_t val);
uint16_t vdp_read_data(void);
uint16_t vdp_read_status(void);
/* Set or clear the VBLANK status bit. Call with active=1 before VBlank handler,
 * active=0 when active display resumes. */
void     vdp_set_vblank(int active);
/* Set or clear the HBLANK status bit. Call with active=1 at start of each
 * HBlank period, active=0 at end (before HBlank handler returns). */
void     vdp_set_hblank(int active);
void     vdp_render_frame(uint32_t *framebuf);  /* 320x224 ARGB8888 */
