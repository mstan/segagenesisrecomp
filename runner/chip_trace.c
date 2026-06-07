/*
 * chip_trace.c — shared dev-only [CHIP-TRACE] ring (see chip_trace.h).
 * Compiled into BOTH the native and _oracle builds. Strip before commit.
 */
#include "chip_trace.h"
#include <stdio.h>

/* Skip the boot SMPS driver upload so the ring holds gameplay/demo writes,
 * not the one-time init blast. Must match genesis_machine.c's gate. */
#define SND_TRACE_START_FRAME 90u

int           g_snd_trace = 1;       /* on by default — the ring is cheap */
unsigned long g_snd_frame = 0;
unsigned      g_snd_line  = 0;
unsigned long g_snd_vint  = 0;       /* cross-backend sync stamp (see chip_trace.h) */

typedef struct { uint32_t frame; uint16_t line; uint8_t kind, port, val, pad; } ChipEvt;
#define CHIP_RING_N 262144u          /* ~16 s of history at peak DAC rate */
static ChipEvt   g_chip_ring[CHIP_RING_N];
static unsigned  g_chip_head = 0;

void snd_trace_chip(int kind, uint8_t port, uint8_t val)
{
    if (!g_snd_trace || g_snd_frame < SND_TRACE_START_FRAME) return;
    ChipEvt *e = &g_chip_ring[g_chip_head++ & (CHIP_RING_N - 1u)];
    /* Stamp with vint (cross-backend sync key), not wall frame. */
    e->frame = (uint32_t)g_snd_vint; e->line = (uint16_t)g_snd_line;
    e->kind  = (uint8_t)kind; e->port = port; e->val = val; e->pad = 0;
}

void chip_trace_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[CHIP] dump: cannot open %s\n", path); return; }
    unsigned n = (g_chip_head < CHIP_RING_N) ? g_chip_head : CHIP_RING_N;
    for (unsigned i = n; i > 0; i--) {
        ChipEvt *e = &g_chip_ring[(g_chip_head - i) & (CHIP_RING_N - 1u)];
        if (e->kind == CHIP_FM)
            fprintf(f, "f=%u sl=%u FM  p%u $%02X\n", e->frame, e->line, e->port, e->val);
        else
            fprintf(f, "f=%u sl=%u PSG $%02X\n",     e->frame, e->line, e->val);
    }
    fclose(f);
    fprintf(stderr, "[CHIP] dumped %u events to %s\n", n, path);
}
