/*
 * genesis_bus.c — clean-room Genesis 68K + Z80 bus (see genesis_bus.h).
 * Original implementation from the documented memory map; no emulator code
 * copied. FM/PSG register writes are pushed onto the shared cycle-stamped
 * audio event queue; audio_mixer_drain() applies them to the chips once per
 * wall frame, advancing the chips between writes (see the stamping note in
 * genesis_machine.c).
 */
#include "genesis_bus.h"
#include "../audio/event_queue.h"
#include <string.h>

/* Recompiled 68K memory. */
extern uint8_t g_rom[0x400000];
extern uint8_t g_ram[0x010000];

/* Audio write stamps (master cycles since wall-frame start; see
 * genesis_machine.c "Audio write stamping"). 68K-origin writes use the
 * generated code's per-instruction cycle counter (68K cycles -> x7 master);
 * it keeps advancing through the V-int handler, where the Sonic 1 68K SMPS
 * driver does its whole register burst. Z80-origin writes use the raster
 * cursor. Banked-window writes (Z80 -> 68K bus) stamp with the 68K counter;
 * no game drives FM/PSG that way, and the mixer's sort/clamp keeps even that
 * case safe. */
extern uint32_t g_audio_cycle_counter;        /* glue.c, 68K cycles this wall frame */
extern uint32_t machine_z80_stamp(void);      /* genesis_machine.c, master cycles    */
/* Handler stamp re-base (glue.c): the V-int/H-int handler executes at its
 * delivery raster line, but g_audio_cycle_counter sits wherever the main
 * loop parked (or mid-frame on a lag frame). Without the re-base the
 * handler's writes sort 100+ lines away from their true raster position,
 * interleaving the 68K driver tick into the middle of the Z80's DAC byte
 * stream in stamp space. glue.c sets this to (delivery_raster -
 * counter*7) for the duration of the handler, 0 otherwise. */
extern uint32_t g_68k_stamp_rebase;
#define STAMP_68K() (g_audio_cycle_counter * 7u + g_68k_stamp_rebase)

/* [SND-TRACE] sound-command lifecycle trace (defined in genesis_machine.c). */
extern uint8_t g_sndwatch[0x2000];
extern unsigned long g_snd_frame;
extern void snd_trace_68k_write(uint16_t off, uint8_t oldv, uint8_t newv, int busreq, int reset_off);
extern void snd_trace_z80(uint16_t off, uint8_t val, int is_write);
extern void snd_trace_busreq(const char *what, int val);
/* [CHIP-TRACE] writer attribution for each FM/PSG push: Z80 PC for Z80-origin
 * writes, 0xFFFF for 68K-origin. (Banked-window Z80->68K-bus PSG writes would
 * mis-mark as 68K; no game drives FM/PSG that way.) */
extern unsigned g_snd_pcz;
extern uint32_t machine_z80_pc(void);
/* glue.c: charge 68K->VDP DMA freeze cycles to the recompiled CPU. */
extern void glue_charge_68k_stall(uint32_t cycles);
#define CHIP_PC_68K() (g_snd_pcz = 0xFFFFu)
#define CHIP_PC_Z80() (g_snd_pcz = (unsigned)machine_z80_pc())

void gbus_init(GenesisBus *b, GVDP *vdp)
{
    memset(b, 0, sizeof(*b));
    b->vdp = vdp;
    b->version = 0x80;          /* overseas (non-Japan) NTSC, no FDD          */
    b->z80_reset_off = 0;       /* Z80 starts held in reset                   */
    b->z80_busreq = 0;
}

/* Battery SRAM geometry from the ROM header ($1B0 "RA" + BE start/end
 * longwords). Called from main.c AFTER glue_init populates g_rom —
 * machine_init/gbus_init run before the ROM copy exists. Sonic 3:
 * "RA F8 20" start $200001 end $2003FF (odd-byte serial FRAM). */
void gbus_sram_setup(GenesisBus *b)
{
    if (g_rom[0x1B0] == 'R' && g_rom[0x1B1] == 'A') {
        uint32_t start = ((uint32_t)g_rom[0x1B4] << 24) | ((uint32_t)g_rom[0x1B5] << 16)
                       | ((uint32_t)g_rom[0x1B6] << 8)  |  (uint32_t)g_rom[0x1B7];
        uint32_t end   = ((uint32_t)g_rom[0x1B8] << 24) | ((uint32_t)g_rom[0x1B9] << 16)
                       | ((uint32_t)g_rom[0x1BA] << 8)  |  (uint32_t)g_rom[0x1BB];
        uint32_t base  = start & ~1u;
        if (end > base && (end - base + 1u) <= sizeof(b->sram)) {
            b->sram_present = 1;
            b->sram_base    = base;
            b->sram_end     = end;
            b->sram_size    = end - base + 1u;
        }
    }
}

/* Explicit SRAM geometry for carts that don't self-describe via the header
 * (S3&K lock-on, standalone S&K). Header detection wins: if gbus_sram_setup
 * already mapped SRAM ("RA" present), this is a no-op so a self-describing
 * cart is never overridden. Otherwise map [start & ~1, end] if it fits the
 * buffer. The $A130F1 overlay gate (sram_hit) is unchanged, so when the game
 * disables SRAM the same addresses read back as ROM — exactly the lock-on
 * behaviour where this range overlaps the locked-on Sonic 3 bank. */
void gbus_sram_set_geometry(GenesisBus *b, uint32_t start, uint32_t end)
{
    if (b->sram_present) return;                 /* header already mapped it */
    if (start == 0u) return;                     /* not requested            */
    uint32_t base = start & ~1u;
    if (end > base && (end - base + 1u) <= sizeof(b->sram)) {
        b->sram_present = 1;
        b->sram_base    = base;
        b->sram_end     = end;
        b->sram_size    = end - base + 1u;
    }
}

/* SRAM overlay active for this address? The $A130F1 enable bit gates the
 * overlay when the SRAM range shadows ROM (cart < 4 MB games still toggle it;
 * honour the bit always — games that never touch $A130F1 and expect
 * always-mapped SRAM can be handled when one shows up). */
static int sram_hit(const GenesisBus *b, uint32_t a)
{
    return b->sram_present && b->sram_enabled
        && a >= b->sram_base && a <= b->sram_end;
}

/* Persistence accessors (main.c .srm load/flush). */
uint8_t *gbus_sram_buffer(GenesisBus *b)        { return b->sram; }
uint32_t gbus_sram_size(const GenesisBus *b)    { return b->sram_present ? b->sram_size : 0u; }

/* ---- Controllers: 3-button (default) + 6-button TH-multiplexed protocol ----
 *
 * 3-button: TH=1 -> ?1CBRLDU, TH=0 -> ?0SA00DU (unchanged from the original).
 *
 * 6-button: the pad maintains a counter incremented on each TH 1->0 edge (see
 * io_write), reset between read bursts (machine_set_pad zeroes it per frame, the
 * ~1.5ms hardware timeout). After the 3rd falling edge the controller reveals
 * the extra buttons:
 *   c<3, TH=1 -> ?1CBRLDU      c<3,  TH=0 -> ?0SA00DU      (as 3-button)
 *   c==3,TH=0 -> ?0SA0000      (low nibble 0 = "6-button present" signature)
 *   c>=3,TH=1 -> ?1CB MXYZ     (Mode, X, Y, Z in the low nibble)
 *   c>=4,TH=0 -> ?0SA1111      (sequence terminator)
 */
static uint8_t pad_read(GenesisBus *b, int port)
{
    uint16_t p  = b->pad[port];                /* GPAD_* bits, 1 = pressed     */
    uint8_t  th = b->io_data[port] & 0x40;     /* TH select line               */
    /* Buttons are active-low (0 = pressed). */

    if (b->pad_type[port] == 1) {              /* 6-button extended protocol   */
        uint8_t c = b->pad_th_count[port];
        if (th) {                              /* TH = 1 */
            if (c >= 3)                        /* ?1CB MXYZ (extras)           */
                return (uint8_t)(0x40
                    | ((p & GPAD_C)    ? 0 : 0x20) | ((p & GPAD_B) ? 0 : 0x10)
                    | ((p & GPAD_MODE) ? 0 : 0x08) | ((p & GPAD_X) ? 0 : 0x04)
                    | ((p & GPAD_Y)    ? 0 : 0x02) | ((p & GPAD_Z) ? 0 : 0x01));
            return (uint8_t)(0x40              /* ?1CBRLDU                     */
                | ((p & GPAD_C)     ? 0 : 0x20) | ((p & GPAD_B)    ? 0 : 0x10)
                | ((p & GPAD_RIGHT) ? 0 : 0x08) | ((p & GPAD_LEFT) ? 0 : 0x04)
                | ((p & GPAD_DOWN)  ? 0 : 0x02) | ((p & GPAD_UP)   ? 0 : 0x01));
        }
        /* TH = 0 */
        if (c >= 4)                            /* ?0SA1111 (terminator)        */
            return (uint8_t)(0x0F
                | ((p & GPAD_START) ? 0 : 0x20) | ((p & GPAD_A) ? 0 : 0x10));
        if (c == 3)                            /* ?0SA0000 (signature)         */
            return (uint8_t)(0x00
                | ((p & GPAD_START) ? 0 : 0x20) | ((p & GPAD_A) ? 0 : 0x10));
        return (uint8_t)(0x00                  /* ?0SA00DU                     */
            | ((p & GPAD_START) ? 0 : 0x20) | ((p & GPAD_A) ? 0 : 0x10)
            | ((p & GPAD_DOWN)  ? 0 : 0x02) | ((p & GPAD_UP) ? 0 : 0x01));
    }

    /* 3-button (default). */
    if (th) {                                  /* TH=1: --CBRLDU              */
        return (uint8_t)(0x40
            | ((p & GPAD_C)     ? 0 : 0x20) | ((p & GPAD_B)    ? 0 : 0x10)
            | ((p & GPAD_RIGHT) ? 0 : 0x08) | ((p & GPAD_LEFT) ? 0 : 0x04)
            | ((p & GPAD_DOWN)  ? 0 : 0x02) | ((p & GPAD_UP)   ? 0 : 0x01));
    }
    /* TH=0: --SA00DU (Left/Right bits read 0). */
    return (uint8_t)(0x00
        | ((p & GPAD_START) ? 0 : 0x20) | ((p & GPAD_A) ? 0 : 0x10)
        | ((p & GPAD_DOWN)  ? 0 : 0x02) | ((p & GPAD_UP) ? 0 : 0x01));
}

static uint16_t io_read(GenesisBus *b, uint32_t a)
{
    switch (a & 0x1E) {
        case 0x00: return b->version;            /* $A10000/1 version          */
        case 0x02: return pad_read(b, 0);        /* $A10002/3 P1 data          */
        case 0x04: return pad_read(b, 1);        /* $A10004/5 P2 data          */
        case 0x06: return 0xFF;                  /* $A10006/7 EXP data         */
        case 0x08: return b->io_ctrl[0];
        case 0x0A: return b->io_ctrl[1];
        case 0x0C: return b->io_ctrl[2];
        default:   return 0x00;
    }
}

/* Advance the 6-button TH edge counter on a 1->0 transition of the port's TH
 * line. No-op for 3-button pads. The counter is zeroed once per frame by
 * machine_set_pad (modeling the ~1.5ms read-burst timeout). */
static void pad_io_tick(GenesisBus *b, int port, uint8_t new_data)
{
    if (b->pad_type[port] != 1) { b->pad_th_prev[port] = new_data & 0x40; return; }
    uint8_t th = new_data & 0x40;
    if (b->pad_th_prev[port] && !th) {           /* falling edge 1 -> 0 */
        if (b->pad_th_count[port] < 0xFF) b->pad_th_count[port]++;
    }
    b->pad_th_prev[port] = th;
}

static void io_write(GenesisBus *b, uint32_t a, uint8_t v)
{
    switch (a & 0x1E) {
        case 0x02: pad_io_tick(b, 0, v); b->io_data[0] = v; break;
        case 0x04: pad_io_tick(b, 1, v); b->io_data[1] = v; break;
        case 0x06: b->io_data[2] = v; break;
        case 0x08: b->io_ctrl[0] = v; break;
        case 0x0A: b->io_ctrl[1] = v; break;
        case 0x0C: b->io_ctrl[2] = v; break;
        default: break;
    }
}

/* ---- 68K bus -------------------------------------------------------------- */
uint16_t gbus_read16(GenesisBus *b, uint32_t a)
{
    a &= 0xFFFFFFu;
    if (sram_hit(b, a)) {                                            /* SRAM    */
        uint32_t o = a - b->sram_base;
        return (uint16_t)((b->sram[o] << 8) | b->sram[(o + 1u) % b->sram_size]);
    }
    if (a < 0x400000u)
        return (uint16_t)((g_rom[a] << 8) | g_rom[a + 1]);          /* ROM     */
    if (a >= 0xFF0000u) {                                            /* RAM     */
        uint16_t o = (uint16_t)(a & 0xFFFFu);
        return (uint16_t)((g_ram[o] << 8) | g_ram[(uint16_t)(o + 1)]);
    }
    if (a >= 0xC00000u && a < 0xC00010u) {                          /* VDP      */
        if (a < 0xC00004u) return gvdp_read_data(b->vdp);
        if (a < 0xC00008u) return gvdp_read_control(b->vdp);
        return gvdp_read_hv_counter(b->vdp);
    }
    if (a >= 0xA00000u && a < 0xA10000u) {                          /* Z80/FM   */
        if ((a & 0xFFFFu) < 0x2000u) {
            /* Z80 RAM is byte-addressable. Return a byte-accurate word
             * (high = even byte, low = odd byte) so that the byte-read paths
             * (m68k_read8 / gbus_read8, which call gbus_read16(addr & ~1) then
             * pick a half) recover the correct byte for BOTH parities. The old
             * (d<<8)|d mirror made odd-address byte reads return the even
             * neighbour, which doubled every other byte in the Saxman sound-
             * driver decompression's back-reference copy (MOVE.B (A4,D4.W),
             * (A5)+) — the S2 silent-audio bug. */
            unsigned off = a & 0x1FFFu;
            return (uint16_t)(((uint16_t)b->z80_ram[off] << 8)
                              | b->z80_ram[(off + 1u) & 0x1FFFu]);
        }
        /* YM2612 status ($A04000-$A04003, mirrored to $A05FFF): bit 7 = BUSY,
         * bits 1..0 = timer B/A overflow. We model an infinitely-fast chip
         * that is never busy and (since the 68K SMPS driver doesn't use the
         * FM timers) has no overflow — so the status reads back 0. Returning
         * 0xFF here would leave bit 7 set, and the driver's WriteFMI/WriteFMII
         * busy-wait (btst #7; bne) would spin forever. */
        if ((a & 0xFFFFu) < 0x6000u) return 0x0000u;   /* FM status: not busy */
        return 0xFFFFu;                          /* bank/PSG region: open bus  */
    }
    if (a >= 0xA10000u && a < 0xA10020u) return io_read(b, a);      /* I/O      */
    if (a == 0xA11100u) { uint16_t ack = (uint16_t)(b->z80_busreq ? 0x0000 : 0x0100);
        snd_trace_busreq("REQ-READack", ack ? 1 : 0); return ack; }  /* [SND-TRACE] */
    if (a == 0xA11200u) return (uint16_t)(b->z80_reset_off ? 0x0100 : 0x0000);
    return 0xFFFFu;
}

void gbus_write16(GenesisBus *b, uint32_t a, uint16_t v)
{
    a &= 0xFFFFFFu;
    if (a >= 0xFF0000u) {                                            /* RAM     */
        uint16_t o = (uint16_t)(a & 0xFFFFu);
        g_ram[o] = (uint8_t)(v >> 8); g_ram[(uint16_t)(o + 1)] = (uint8_t)v;
        return;
    }
    if (sram_hit(b, a)) {                                            /* SRAM    */
        uint32_t o = a - b->sram_base;
        b->sram[o] = (uint8_t)(v >> 8); b->sram[(o + 1u) % b->sram_size] = (uint8_t)v;
        return;
    }
    if (a == 0xA130F0u) { b->sram_enabled = (uint8_t)(v & 1); return; } /* SRAM overlay reg (word) */
    if (a < 0x400000u) return;                   /* ROM: ignore writes         */
    if (a >= 0xC00000u && a < 0xC00010u) {                          /* VDP      */
        if (a < 0xC00004u)      gvdp_write_data(b->vdp, v);
        else if (a < 0xC00008u) gvdp_write_control(b->vdp, v);
        /* A control write that triggered a 68K->VDP DMA froze the 68K for the
         * transfer — charge those cycles to the recompiled CPU's accounting. */
        { uint32_t st = gvdp_consume_68k_stall(b->vdp);
          if (st) glue_charge_68k_stall(st); }
        return;
    }
    if (a >= 0xC00010u && a < 0xC00018u) { CHIP_PC_68K(); audio_event_push(STAMP_68K(), AUDIO_PORT_PSG, (uint8_t)v); return; } /* PSG */
    if (a >= 0xA00000u && a < 0xA10000u) {                          /* Z80/FM   */
        if ((a & 0xFFFFu) < 0x2000u) { unsigned off = a & 0x1FFFu;
            snd_trace_68k_write((uint16_t)off, b->z80_ram[off], (uint8_t)(v >> 8),
                                b->z80_busreq, b->z80_reset_off);  /* [SND-TRACE] */
            b->z80_ram[off] = (uint8_t)(v >> 8); return; }
        if (a >= 0xA04000u && a <= 0xA04003u) { CHIP_PC_68K(); audio_event_push(STAMP_68K(), (uint8_t)(a & 3), (uint8_t)v); return; }
        return;
    }
    if (a >= 0xA10000u && a < 0xA10020u) { io_write(b, a, (uint8_t)v); return; }
    if (a == 0xA11100u) { b->z80_busreq = (v & 0x0100) ? 1 : 0;
        snd_trace_busreq("REQ", b->z80_busreq); return; }                /* [SND-TRACE] */
    if (a == 0xA11200u) { int o = b->z80_reset_off; b->z80_reset_off = (v & 0x0100) ? 1 : 0;
        /* Asserting the reset line (1->0) resets the Z80 core: it must restart
         * at $0000 on release. The actual core reset is deferred to step_z80
         * (the core lives in the scheduler). Without this the Z80 resumed from
         * a stale PC after the post-upload reset pulse and ran the driver from
         * garbage — the S2 silent-audio bad-jump. */
        if (o == 1 && b->z80_reset_off == 0) b->z80_reset_pending = 1;
        snd_trace_busreq("RESET_OFF", b->z80_reset_off);  /* [SND-TRACE] */
        return; }
}

uint8_t gbus_read8(GenesisBus *b, uint32_t a)
{
    /* Z80 RAM ($A00000-$A01FFF) is byte-addressable from the 68K. Reading it
     * through the word path is wrong: gbus_read16 mirrors the addressed byte
     * onto BOTH halves of the word, so a byte read at an ODD address returns
     * the EVEN-neighbour byte instead of the addressed one. That corrupted the
     * Saxman sound-driver decompression's back-reference copy
     * (MOVE.B (A4,D4.W),(A5)+ with D4 incrementing): odd D4 read the even
     * neighbour, so every other source byte was doubled and the rest dropped
     * — the S2 silent-audio bug. Read Z80 RAM directly (symmetric with
     * gbus_write8). */
    if (a >= 0xA00000u && a < 0xA10000u && (a & 0xFFFFu) < 0x2000u)
        return b->z80_ram[a & 0x1FFFu];
    if (sram_hit(b, a & 0xFFFFFFu))                       /* SRAM: exact byte  */
        return b->sram[(a & 0xFFFFFFu) - b->sram_base];
    uint16_t w = gbus_read16(b, a & ~1u);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}

void gbus_write8(GenesisBus *b, uint32_t a, uint8_t v)
{
    a &= 0xFFFFFFu;
    /* Byte writes to RAM / Z80 RAM / PSG / FM need exact targeting. */
    if (a >= 0xFF0000u) { g_ram[a & 0xFFFFu] = v; return; }
    if (a >= 0xA00000u && a < 0xA10000u) {
        if ((a & 0xFFFFu) < 0x2000u) { unsigned off = a & 0x1FFFu;
            snd_trace_68k_write((uint16_t)off, b->z80_ram[off], v,
                                b->z80_busreq, b->z80_reset_off);  /* [SND-TRACE] */
            b->z80_ram[off] = v; return; }
        if (a >= 0xA04000u && a <= 0xA04003u) { CHIP_PC_68K(); audio_event_push(STAMP_68K(), (uint8_t)(a & 3), v); return; }
        return;
    }
    if (a == 0xC00011u) { CHIP_PC_68K(); audio_event_push(STAMP_68K(), AUDIO_PORT_PSG, v); return; }
    if (sram_hit(b, a)) { b->sram[a - b->sram_base] = v; return; }  /* SRAM: exact byte */
    if (a == 0xA130F1u || a == 0xA130F0u) { b->sram_enabled = (uint8_t)(v & 1); return; } /* overlay reg */
    /* Word-aligned fall-through for VDP/IO byte writes (value on both halves). */
    gbus_write16(b, a & ~1u, (uint16_t)((v << 8) | v));
}

/* ---- Z80 bus (SMPS driver's address space) -------------------------------- */
uint8_t gbus_z80_read(GenesisBus *b, uint16_t addr)
{
    if (addr < 0x4000u) return b->z80_ram[addr & 0x1FFFu];         /* RAM+mirror */
    /* YM2612 status ($4000-$5FFF): bit 7 = BUSY. Model a never-busy, no-timer-
     * overflow chip -> 0. Returning 0xFF (bit 7 set) made the SMPS Z80 driver's
     * busy-wait spin forever at init (PC stuck ~$0008, zero FM writes) — the
     * same bug as the 68K-side WriteFMI; see gbus_read16. */
    if (addr < 0x6000u) return 0x00u;                               /* FM status  */
    if (addr < 0x8000u) return 0xFFu;                               /* bank/PSG   */
    /* $8000-$FFFF: banked window into the 68K bus. */
    uint32_t a68 = ((uint32_t)b->z80_bank << 15) + (uint32_t)(addr - 0x8000u);
    return gbus_read8(b, a68);
}

void gbus_z80_write(GenesisBus *b, uint16_t addr, uint8_t val)
{
    if (addr < 0x4000u) { unsigned off = addr & 0x1FFFu;
        if (g_sndwatch[off]) snd_trace_z80((uint16_t)off, val, 1);  /* [SND-TRACE] */
        b->z80_ram[off] = val; return; }                           /* RAM+mirror */
    if (addr < 0x6000u) { CHIP_PC_Z80(); audio_event_push(machine_z80_stamp(), (uint8_t)(addr & 3), val); return; } /* FM */
    if (addr < 0x6100u) {                                /* $6000 bank register   */
        b->z80_bank = ((b->z80_bank >> 1) | ((val & 1) << 8)) & 0x1FF;
        return;
    }
    if (addr == 0x7F11u) { CHIP_PC_Z80(); audio_event_push(machine_z80_stamp(), AUDIO_PORT_PSG, val); return; }       /* PSG          */
    if (addr >= 0x8000u) {                                          /* banked window */
        uint32_t a68 = ((uint32_t)b->z80_bank << 15) + (uint32_t)(addr - 0x8000u);
        gbus_write8(b, a68, val);
    }
}
