/*
 * chip_trace.h — dev-only [CHIP-TRACE] FM/PSG register-write stream ring,
 * SHARED by both the own-backend (native) and clownmdemu (_oracle) builds so the
 * two chip-write streams can be captured and diffed (native synth vs the
 * clownmdemu reference). Strip with the rest of the [SND-TRACE] diagnostics
 * before commit (PRINCIPLES #18).
 *
 * Capture site (both builds): event_queue.c audio_event_push() tap — the
 * clownmdemu fork routes every FM/PSG write there, and the own backend now
 * pushes all its FM/PSG writes there too (genesis_bus.c), so one tap covers
 * either build with ZERO edits to the AGPL core.
 *
 * Both stamp (frame, line) the same way (frame = wall frame since boot; line =
 * scanline 0..261), so the dumps are directly comparable and parse identically
 * in tools/synth_replay + tools/chip_stream_diff.py.
 */
#ifndef CHIP_TRACE_H
#define CHIP_TRACE_H

#include <stdint.h>

enum { CHIP_FM = 0, CHIP_PSG = 1 };

/* Stamping globals (defined in chip_trace.c). The own-backend scheduler updates
 * g_snd_frame/g_snd_line per scanline; the oracle sets g_snd_frame per wall
 * frame (main.c) and the event_queue tap derives g_snd_line from the cycle. */
extern int           g_snd_trace;    /* master on/off (default on) */
extern unsigned long g_snd_frame;    /* current wall frame since boot */
extern unsigned      g_snd_line;     /* current scanline within the frame */
/* vint_runcount ($FFFE0C) at capture time. This — NOT the wall frame — is the
 * cross-backend sync key: native & oracle reach a given vint at the same logical
 * sound state, but at DIFFERENT wall frames (V-int skew + boot drift). The ring
 * stamps events with this so the two dumps align in chip_stream_diff. Set per
 * frame in main.c for both builds. */
extern unsigned long g_snd_vint;
/* The event's queue cycle stamp (master cycles within the wall frame; the
 * sample-boundary truth for "did the EG clock between these two writes").
 * Set by the audio_event_push tap for both builds. Dev-only. */
extern unsigned long g_snd_mc;
/* Writer attribution for the next pushed chip event: the Z80 PC for Z80-origin
 * writes, 0xFFFF for 68K-origin writes (set at each push site in
 * genesis_bus.c; the oracle build leaves it 0 = unattributed). Lets a dump
 * answer "which driver routine emitted this write". Dev-only. */
extern unsigned      g_snd_pcz;

/* kind = CHIP_FM (port 0..3 = YM2612 addr1/data1/addr2/data2) or CHIP_PSG
 * (port ignored). Gated on g_snd_trace and the boot-upload skip frame. */
void snd_trace_chip(int kind, uint8_t port, uint8_t val);

/* Dump the ring oldest->newest as "f=%u sl=%u FM p%u $%02X" / "... PSG $%02X". */
void chip_trace_dump(const char *path);

#endif /* CHIP_TRACE_H */
