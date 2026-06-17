/*
 * genesis_machine.c — clean-room own-backend scheduler (see genesis_machine.h).
 * Per-scanline frame loop driving the recompiled 68K (glue fiber), the superzazu
 * Z80, and our VDP, delivering V/H interrupts. Original implementation.
 */
#include "genesis_machine.h"
#include "chip_trace.h"   /* [CHIP-TRACE] shared FM/PSG stream ring + g_snd_* globals */
#include <string.h>
#include <stdio.h>   /* [SND-TRACE] temporary sound-command lifecycle trace */

GenesisMachine g_machine;

/* ===================== [SND-TRACE] sound-command lifecycle =================
 * Always-on in-memory ring (no I/O in the hot path — PRINCIPLES #17/#18).
 * Auto-discovers the SMPS Z80 command mailbox by watching the 68K's *gameplay*
 * writes into Z80 RAM (boot upload skipped via the frame gate), and records
 * deposits, Z80 clears/sets, the V-int assert/accept phase, and BUSREQ windows.
 * Z80 *reads* are intentionally NOT recorded (idle polling storms the ring and
 * deposit+clear already tells consumed-vs-dropped). Dump via F9. Strip before
 * commit. */
/* g_snd_trace / g_snd_frame / g_snd_line now live in the shared chip_trace.c
 * (so the oracle build has them too). This scheduler still owns/advances
 * g_snd_frame + g_snd_line per scanline below. */
uint8_t       g_sndwatch[0x2000];    /* 1 = a gameplay 68K write touched this  */

#ifndef GEN_DEV_TRACE
/* Prod builds: no ring memory, no captures, dumps report "not compiled in".
 * (g_sndwatch stays zeroed — the bus checks it and never fires.) */
void snd_trace_68k_write(uint16_t off, uint8_t oldv, uint8_t newv,
                         int busreq, int reset_off)
{ (void)off; (void)oldv; (void)newv; (void)busreq; (void)reset_off; }
void snd_trace_z80(uint16_t off, uint8_t val, int is_write)
{ (void)off; (void)val; (void)is_write; }
void snd_trace_busreq(const char *what, int val) { (void)what; (void)val; }
void snd_trace_dump(const char *path)
{ (void)path; fprintf(stderr, "[SND] trace not compiled in (build with GEN_DEV_TRACE=ON)\n"); }
void z80_ram_dump(const char *path)
{ (void)path; fprintf(stderr, "[SND] z80 dump not compiled in (build with GEN_DEV_TRACE=ON)\n"); }
#else /* GEN_DEV_TRACE */
#define SND_TRACE_START_FRAME 90u    /* skip the boot driver upload            */

enum { SND_68KW = 1, SND_Z80W, SND_VASSERT, SND_VACCEPT, SND_BUS };
typedef struct {
    uint8_t  type, v1, v2, flags;    /* v1/v2: old/new or val or pending/iff   */
    uint16_t addr, pcz, line, pad;
    uint32_t frame;
    uint64_t master;
    const char *tag;                 /* BUS only: REQ / RESET_OFF / REQ-READack */
} SndEvt;
#define SND_RING_N 65536u
static SndEvt   g_snd_ring[SND_RING_N];
static unsigned g_snd_head = 0;
static SndEvt *snd_evt(uint8_t type) {
    SndEvt *e = &g_snd_ring[g_snd_head++ & (SND_RING_N - 1u)];
    e->type = type; e->frame = (uint32_t)g_snd_frame; e->line = (uint16_t)g_snd_line;
    e->master = g_machine.master_cycle;
    e->pcz = (uint16_t)g_machine.z80.pc;
    e->addr = 0; e->v1 = e->v2 = e->flags = 0; e->tag = 0;
    return e;
}

void snd_trace_68k_write(uint16_t off, uint8_t oldv, uint8_t newv,
                         int busreq, int reset_off)
{
    if (!g_snd_trace || g_snd_frame < SND_TRACE_START_FRAME) return;
    off &= 0x1FFFu;
    g_sndwatch[off] = 1;
    SndEvt *e = snd_evt(SND_68KW);
    e->addr = off; e->v1 = oldv; e->v2 = newv;
    e->flags = (uint8_t)((busreq ? 1 : 0) | (reset_off ? 2 : 0));
}
void snd_trace_z80(uint16_t off, uint8_t val, int is_write)
{
    if (!g_snd_trace || !is_write) return;     /* writes only (clears/sets) */
    SndEvt *e = snd_evt(SND_Z80W);
    e->addr = (uint16_t)(off & 0x1FFFu); e->v1 = val;
}
void snd_trace_busreq(const char *what, int val)
{
    if (!g_snd_trace || g_snd_frame < SND_TRACE_START_FRAME) return;
    SndEvt *e = snd_evt(SND_BUS);
    e->v1 = (uint8_t)val; e->tag = what;
}

/* Dump the ring oldest->newest to a file (called from the F9 handler). */
void snd_trace_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[SND] dump: cannot open %s\n", path); return; }
    unsigned n = (g_snd_head < SND_RING_N) ? g_snd_head : SND_RING_N;
    for (unsigned i = n; i > 0; i--) {
        SndEvt *e = &g_snd_ring[(g_snd_head - i) & (SND_RING_N - 1u)];
        switch (e->type) {
        case SND_68KW:
            fprintf(f, "f=%u sl=%u mc=%llu 68KW   z80[$%04X] $%02X->$%02X busreq=%d reset=%d\n",
                    e->frame, e->line, (unsigned long long)e->master, e->addr, e->v1, e->v2,
                    (e->flags & 1) ? 1 : 0, (e->flags & 2) ? 1 : 0); break;
        case SND_Z80W:
            fprintf(f, "f=%u sl=%u mc=%llu Z80W   pcz=$%04X z80[$%04X]=$%02X\n",
                    e->frame, e->line, (unsigned long long)e->master, e->pcz, e->addr, e->v1); break;
        case SND_VASSERT:
            fprintf(f, "f=%u sl=%u mc=%llu VINT-ASSERT pcz=$%04X pending_was=%d iff1=%d\n",
                    e->frame, e->line, (unsigned long long)e->master, e->pcz, e->v1, e->v2); break;
        case SND_VACCEPT:
            fprintf(f, "f=%u sl=%u mc=%llu VINT-ACCEPT pcz=$%04X\n",
                    e->frame, e->line, (unsigned long long)e->master, e->pcz); break;
        case SND_BUS:
            fprintf(f, "f=%u sl=%u mc=%llu BUS    %s=%d\n",
                    e->frame, e->line, (unsigned long long)e->master, e->tag ? e->tag : "?", e->v1); break;
        }
    }
    fclose(f);
    fprintf(stderr, "[SND] dumped %u events to %s\n", n, path);
}

/* [SND-TRACE] one-shot Z80 RAM dump (the uploaded SMPS driver image) so the
 * sync-wait loop the Z80 sits in at vblank can be disassembled. */
void z80_ram_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[SND] z80 dump: cannot open %s\n", path); return; }
    fwrite(g_machine.bus.z80_ram, 1, sizeof(g_machine.bus.z80_ram), f);
    fclose(f);
    fprintf(stderr, "[SND] dumped Z80 RAM (%u bytes) to %s\n",
            (unsigned)sizeof(g_machine.bus.z80_ram), path);
}
#endif /* GEN_DEV_TRACE */

/* The [CHIP-TRACE] FM/PSG register-write ring (snd_trace_chip / chip_trace_dump)
 * lives in the shared runner/chip_trace.c; both builds are captured by the
 * audio_event_push tap in audio/event_queue.c. */

/* Writer attribution: the Z80 PC for Z80-origin FM/PSG pushes (genesis_bus.c
 * sets g_snd_pcz from this at each push site; cheap scalar, all builds). */
uint32_t machine_z80_pc(void)
{
    return (uint32_t)g_machine.z80.pc;
}

/* glue.c hooks (recompiled-68K fiber + own-backend interrupt delivery). */
extern void glue_run_game_chunk(uint32_t cycles);
extern void glue_own_interrupt(int level, GVDP *vdp);
extern int  glue_own_vint_service_latched(GVDP *vdp);  /* deliver a V-int latched while 68K IRQs were masked */

/* ARGB palette cache (normal/shadow/highlight), fed by VDP CRAM writes. The
 * extra trailing slot (index GVDP_WS_BAR_INDEX) is a synthetic entry the VDP
 * emits for black-mode widescreen pillarbox columns; seeded to opaque black in
 * machine_init() so the index->ARGB conversion stays branch-free. CRAM writes
 * only ever touch indices < GVDP_TOTAL_PALETTE, so the slot is never clobbered. */
static uint32_t s_cram_argb[GVDP_TOTAL_PALETTE + 1];

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static uint32_t bgr9_to_argb(uint16_t c, int lvl)
{
    int r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7, scale, off;
    if      (lvl == 1) { scale = 18; off = 0;   }
    else if (lvl == 2) { scale = 18; off = 130; }
    else               { scale = 36; off = 0;   }
    return 0xFF000000u
         | ((uint32_t)clamp8(off + r * scale) << 16)
         | ((uint32_t)clamp8(off + g * scale) << 8)
         |  (uint32_t)clamp8(off + b * scale);
}
static void colour_cb(void *u, unsigned idx, uint16_t bgr9)
{
    (void)u;
    if (idx >= GVDP_CRAM_ENTRIES) return;
    s_cram_argb[idx]                          = bgr9_to_argb(bgr9, 0);
    s_cram_argb[idx + GVDP_PALETTE_SHADOW]    = bgr9_to_argb(bgr9, 1);
    s_cram_argb[idx + GVDP_PALETTE_HIGHLIGHT] = bgr9_to_argb(bgr9, 2);
}

/* VDP DMA source reads come from our own bus. */
static uint16_t vdp_bus_read(void *u, uint32_t a) { (void)u; return gbus_read16(&g_machine.bus, a); }

/* Z80 core memory callbacks -> Z80-side bus (superzazu signatures). The
 * Genesis Z80 has no separate I/O space wired up; port IN/OUT collapse onto
 * the memory bus using the 16-bit BC port address (no Genesis game driver
 * executes IN/OUT). */
static uint8_t z80_read (void *u, uint16_t a)            { return gbus_z80_read((GenesisBus *)u, a); }
static void    z80_write(void *u, uint16_t a, uint8_t v) { gbus_z80_write((GenesisBus *)u, a, v); }
static uint8_t z80_port_in(z80 *z, uint8_t port)
{
    return gbus_z80_read((GenesisBus *)z->userdata, (uint16_t)((z->b << 8) | port));
}
static void z80_port_out(z80 *z, uint8_t port, uint8_t value)
{
    gbus_z80_write((GenesisBus *)z->userdata, (uint16_t)((z->b << 8) | port), value);
}

/* Z80 reset-line pulse: restart at $0000 with interrupts disabled, leaving the
 * general-purpose registers (hardware /RESET behaviour). */
static void z80_own_reset(z80 *z)
{
    z->pc = 0;
    z->iff1 = z->iff2 = 0;
    z->halted = 0;
    z->interrupt_mode = 0;
    z->iff_delay = 0;
    z->int_pending = z->nmi_pending = 0;
    z->int_data = 0xFF;    /* IM1 ignores the data bus */
}

/* Pointer wiring shared by machine_init and machine_load_state (a raw struct
 * restore clobbers the function/user pointers with the saving process's). */
static void machine_wire_pointers(void)
{
    g_machine.vdp.bus_read       = vdp_bus_read;
    g_machine.vdp.colour_updated = colour_cb;
    g_machine.bus.vdp            = &g_machine.vdp;
    g_machine.z80.read_byte      = z80_read;
    g_machine.z80.write_byte     = z80_write;
    g_machine.z80.port_in        = z80_port_in;
    g_machine.z80.port_out       = z80_port_out;
    g_machine.z80.userdata       = &g_machine.bus;
}

void machine_init(void)
{
    memset(&g_machine, 0, sizeof(g_machine));
    gvdp_init(&g_machine.vdp);
    gbus_init(&g_machine.bus, &g_machine.vdp);
    z80_init(&g_machine.z80);       /* superzazu power-on defaults */
    z80_own_reset(&g_machine.z80);
    machine_wire_pointers();
    /* Widescreen black-bar pillarbox colour (opaque black). */
    s_cram_argb[GVDP_WS_BAR_INDEX] = 0xFF000000u;
}

/* ---- Own-backend save states ----------------------------------------------
 * Snapshot/restore the whole machine (VDP incl. VRAM/CRAM/VSRAM, bus incl.
 * Z80 RAM + SRAM, full superzazu Z80 core state — the embedded struct now
 * carries everything, no side blob). The format is a raw struct image,
 * private to a build. main.c owns the file container. */
int machine_save_state(FILE *f)
{
    return fwrite(&g_machine, sizeof g_machine, 1, f) == 1;
}

int machine_load_state(FILE *f)
{
    if (fread(&g_machine, sizeof g_machine, 1, f) != 1) return 0;
    machine_wire_pointers();
    /* Rebuild the ARGB palette cache from restored CRAM (the cache is a
     * derived file-static, not part of the snapshot). */
    for (unsigned i = 0; i < GVDP_CRAM_ENTRIES; i++)
        colour_cb(NULL, i, g_machine.vdp.cram[i]);
    return 1;
}

void machine_set_pad(int port, uint8_t buttons)
{
    if (port >= 0 && port < 2) g_machine.bus.pad[port] = buttons;
}

/* NTSC raster timing. */
#define LINES_TOTAL     262
#define MASTER_PER_LINE 3420u
#define M68K_PER_LINE   488u
#define Z80_PER_LINE    228u
#define MASTER_PER_Z80  (MASTER_PER_LINE / Z80_PER_LINE)   /* 3420/228 = 15 master/Z80 cyc */

/* ---- Audio write stamping -------------------------------------------------
 * FM/PSG register writes are NOT applied to the chips here. They are pushed
 * onto the shared cycle-stamped audio event queue (audio/event_queue.c) by
 * genesis_bus.c, and audio_mixer_drain() (main.c, once per wall frame) sorts
 * them by stamp and advances the chips BETWEEN writes — the same proven
 * architecture the clownmdemu gold-ref uses.
 *
 * Why not advance the chips live at each write (the previous design): the
 * Sonic 1 sound driver is 68K SMPS running inside the V-int handler, which
 * this scheduler executes as one atomic lump at the vblank line. A live
 * chip-position cursor is FROZEN for that whole driver tick, so every write
 * of the tick (including key-off->key-on retriggers) landed at the SAME
 * chip cycle. ymfm detects key edges at its per-sample envelope clock, so
 * zero-sample-spaced off->on pairs never retriggered: faint / missing /
 * tail-only notes (measured: 100% of $28 off->on pairs at gap 0, 2-6 kHz
 * band at 0.03-0.25x the reference in SFX windows).
 *
 * Stamp sources (master cycles since wall-frame start):
 *   Z80-origin writes : s_line_base + s_z80_off (raster position).
 *   68K-origin writes : g_audio_cycle_counter * 7 (per-instruction counter
 *                       bumped by generated code — keeps advancing through
 *                       the V-int handler, which is what restores real
 *                       spacing inside the driver tick), PLUS
 *                       g_68k_stamp_rebase (glue.c): during interrupt
 *                       handlers the counter sits wherever the main loop
 *                       parked, so the handler's writes are re-based to
 *                       start at the raster cursor of delivery — the 68K
 *                       SMPS driver tick lands at the vblank line on the
 *                       frame timeline, as on hardware, instead of 100+
 *                       lines early amid the Z80's DAC byte stream.
 * The two axes interleave; the mixer's stable stamp sort plus its FM
 * address/data pair guard (mixer.c) keep cross-axis interleaving safe —
 * a foreign write can never split a (latch, data) pair. */
static uint32_t s_line_base   = 0;   /* master cycle at the start of the current scanline   */
static uint32_t s_z80_off     = 0;   /* progress within the scanline, in master cycles      */

/* Stamp for Z80-origin chip writes (called from genesis_bus.c). */
uint32_t machine_z80_stamp(void)
{
    return s_line_base + s_z80_off;
}

static void step_z80(GenesisMachine *m, uint32_t target)
{
    if (!m->bus.z80_reset_off || m->bus.z80_busreq) return;   /* Z80 halted */
    /* The 68K pulsed the Z80 reset line while halted — restart the core at
     * $0000 now that it's released, so it runs the freshly-uploaded driver
     * from its entry point (not from a stale, pre-upload PC). */
    if (m->bus.z80_reset_pending) {
        z80_own_reset(&m->z80);
        m->bus.z80_reset_pending = 0;
    }
    int pend_before = m->z80.int_pending ? 1 : 0;   /* [SND-TRACE] */
    uint32_t done = m->z80_cycle_debt;
    m->z80.cyc = 0;            /* per-slice t-state counter (kept bounded) */
    while (done < target) {
        /* Stamp chip writes made by this instruction at its start cycle
         * (master) — machine_z80_stamp() reads this for the event queue. */
        s_z80_off = done * MASTER_PER_Z80;
        unsigned long c0 = m->z80.cyc;
        z80_step(&m->z80);     /* one instruction, then interrupt processing */
        done += (uint32_t)(m->z80.cyc - c0);
    }
    m->z80_cycle_debt = done - target;
#ifdef GEN_DEV_TRACE
    /* [SND-TRACE] V-int acceptance: pending fell 1->0 during this slice. */
    if (g_snd_trace && pend_before && !m->z80.int_pending && g_snd_frame >= SND_TRACE_START_FRAME)
        snd_evt(SND_VACCEPT);
#else
    (void)pend_before;
#endif
}

void machine_run_frame(GenesisScanlineSink sink, void *user)
{
    GenesisMachine *m = &g_machine;
    int active_h = gvdp_screen_height(&m->vdp);
    static uint8_t  idxbuf[GVDP_MAX_WIDTH];
    static uint32_t rowbuf[GVDP_MAX_WIDTH];

    for (int line = 0; line < LINES_TOTAL; line++) {
        unsigned irq = gvdp_begin_scanline(&m->vdp, line);
        g_snd_line = (unsigned)line;   /* [SND-TRACE] */
        s_line_base = (uint32_t)line * MASTER_PER_LINE;
        s_z80_off   = 0;   /* 68K writes during the chunk below land at the line start */

        /* Advance the recompiled 68K ~one scanline (it parks at WaitForVBlank). */
        glue_run_game_chunk(M68K_PER_LINE);

        /* If a prior V-int was latched while the 68K had IRQs masked (e.g. a
         * move #$2700,sr screen transition), deliver it now that the chunk above
         * may have dropped the mask — keeps v_vblank_count in step with the
         * oracle instead of losing the masked V-int. */
        glue_own_vint_service_latched(&m->vdp);

        /* Step the Z80 (SMPS driver) for its share of the line. */
        step_z80(m, Z80_PER_LINE);

        /* Deliver interrupts to the 68K (and the Z80 at vblank). */
        if (irq & GVDP_IRQ_HBLANK) glue_own_interrupt(4, &m->vdp);
        if (irq & GVDP_IRQ_VBLANK) {
            /* Assert the Z80 vblank IRQ and LEAVE it pending — the Z80 takes it
             * on a later step_z80 (when IFF1 is enabled) and superzazu clears
             * int_pending on accept. The old code de-asserted immediately with
             * no Z80 step in between, so the Z80 never took the interrupt: its
             * $0038 handler never ran, the SMPS driver's main loop waited
             * forever on the per-frame flag the handler sets (e.g. Z80 RAM
             * $1C30), and no music played. */
#ifdef GEN_DEV_TRACE
            /* [SND-TRACE] pending_was=1 means last frame's V-int was never
             * accepted (sticky stack-up) — the suspected phase divergence. */
            if (g_snd_trace && g_snd_frame >= SND_TRACE_START_FRAME) {
                SndEvt *e = snd_evt(SND_VASSERT);
                e->v1 = m->z80.int_pending ? 1 : 0;   /* pending_was */
                e->v2 = m->z80.iff1        ? 1 : 0;   /* iff1        */
            }
#endif
            m->z80.int_pending = 1;   /* level-triggered vblank IRQ assert */
            glue_own_interrupt(6, &m->vdp);
        }

        /* Render + emit active scanlines. In interlace mode 2 each raster
         * line yields TWO output rows (the even and odd fields' lines,
         * rendered progressively into a 448-row frame). */
        if (line < active_h && sink) {
            int dbl = gvdp_interlace_double(&m->vdp);
            for (int sub = 0; sub <= dbl; sub++) {
                int row = (line << dbl) + sub;
                int n = gvdp_render_scanline(&m->vdp, row, idxbuf);
                for (int x = 0; x < n; x++) rowbuf[x] = s_cram_argb[idxbuf[x]];
                sink(user, row, rowbuf, n);
            }
        }

        m->master_cycle += MASTER_PER_LINE;
    }
    g_snd_frame++;   /* [SND-TRACE] */
}
