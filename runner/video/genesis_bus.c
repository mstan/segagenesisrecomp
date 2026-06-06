/*
 * genesis_bus.c — clean-room Genesis 68K + Z80 bus (see genesis_bus.h).
 * Original implementation from the documented memory map; no emulator code
 * copied. Audio writes are forwarded to our ym2612/psg APIs (cycle-stamped
 * event-queue routing is layered in during scheduler bring-up).
 */
#include "genesis_bus.h"
#include <string.h>

/* Recompiled 68K memory + our audio chip APIs. */
extern uint8_t g_rom[0x400000];
extern uint8_t g_ram[0x010000];
extern void ym2612_write(uint8_t port, uint8_t value);
extern void psg_write(uint8_t value);

void gbus_init(GenesisBus *b, GVDP *vdp)
{
    memset(b, 0, sizeof(*b));
    b->vdp = vdp;
    b->version = 0x80;          /* overseas (non-Japan) NTSC, no FDD          */
    b->z80_reset_off = 0;       /* Z80 starts held in reset                   */
    b->z80_busreq = 0;
}

/* ---- Controllers: standard 3-button TH-multiplexed protocol --------------- */
static uint8_t pad_read(GenesisBus *b, int port)
{
    uint8_t p  = b->pad[port];                 /* GPAD_* bits, 1 = pressed     */
    uint8_t th = b->io_data[port] & 0x40;      /* TH select line               */
    /* Buttons are active-low (0 = pressed). */
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

static void io_write(GenesisBus *b, uint32_t a, uint8_t v)
{
    switch (a & 0x1E) {
        case 0x02: b->io_data[0] = v; break;
        case 0x04: b->io_data[1] = v; break;
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
            uint8_t d = b->z80_ram[a & 0x1FFFu];
            return (uint16_t)((d << 8) | d);
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
    if (a == 0xA11100u) return (uint16_t)(b->z80_busreq ? 0x0000 : 0x0100);
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
    if (a < 0x400000u) return;                   /* ROM: ignore writes         */
    if (a >= 0xC00000u && a < 0xC00010u) {                          /* VDP      */
        if (a < 0xC00004u)      gvdp_write_data(b->vdp, v);
        else if (a < 0xC00008u) gvdp_write_control(b->vdp, v);
        return;
    }
    if (a >= 0xC00010u && a < 0xC00018u) { psg_write((uint8_t)v); return; } /* PSG */
    if (a >= 0xA00000u && a < 0xA10000u) {                          /* Z80/FM   */
        if ((a & 0xFFFFu) < 0x2000u) { b->z80_ram[a & 0x1FFFu] = (uint8_t)(v >> 8); return; }
        if (a >= 0xA04000u && a <= 0xA04003u) { ym2612_write((uint8_t)(a & 3), (uint8_t)v); return; }
        return;
    }
    if (a >= 0xA10000u && a < 0xA10020u) { io_write(b, a, (uint8_t)v); return; }
    if (a == 0xA11100u) { b->z80_busreq    = (v & 0x0100) ? 1 : 0; return; }
    if (a == 0xA11200u) { b->z80_reset_off = (v & 0x0100) ? 1 : 0; return; }
}

uint8_t gbus_read8(GenesisBus *b, uint32_t a)
{
    uint16_t w = gbus_read16(b, a & ~1u);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}

void gbus_write8(GenesisBus *b, uint32_t a, uint8_t v)
{
    a &= 0xFFFFFFu;
    /* Byte writes to RAM / Z80 RAM / PSG / FM need exact targeting. */
    if (a >= 0xFF0000u) { g_ram[a & 0xFFFFu] = v; return; }
    if (a >= 0xA00000u && a < 0xA10000u) {
        if ((a & 0xFFFFu) < 0x2000u) { b->z80_ram[a & 0x1FFFu] = v; return; }
        if (a >= 0xA04000u && a <= 0xA04003u) { ym2612_write((uint8_t)(a & 3), v); return; }
        return;
    }
    if (a == 0xC00011u) { psg_write(v); return; }
    /* Word-aligned fall-through for VDP/IO byte writes (value on both halves). */
    gbus_write16(b, a & ~1u, (uint16_t)((v << 8) | v));
}

/* ---- Z80 bus (SMPS driver's address space) -------------------------------- */
uint8_t gbus_z80_read(GenesisBus *b, uint16_t addr)
{
    if (addr < 0x4000u) return b->z80_ram[addr & 0x1FFFu];          /* RAM+mirror */
    if (addr < 0x6000u) return 0xFFu;                               /* FM status  */
    if (addr < 0x8000u) return 0xFFu;                               /* bank/PSG   */
    /* $8000-$FFFF: banked window into the 68K bus. */
    uint32_t a68 = ((uint32_t)b->z80_bank << 15) + (uint32_t)(addr - 0x8000u);
    return gbus_read8(b, a68);
}

void gbus_z80_write(GenesisBus *b, uint16_t addr, uint8_t val)
{
    if (addr < 0x4000u) { b->z80_ram[addr & 0x1FFFu] = val; return; } /* RAM+mirror */
    if (addr < 0x6000u) { ym2612_write((uint8_t)(addr & 3), val); return; } /* FM   */
    if (addr < 0x6100u) {                                /* $6000 bank register   */
        b->z80_bank = ((b->z80_bank >> 1) | ((val & 1) << 8)) & 0x1FF;
        return;
    }
    if (addr == 0x7F11u) { psg_write(val); return; }                /* PSG          */
    if (addr >= 0x8000u) {                                          /* banked window */
        uint32_t a68 = ((uint32_t)b->z80_bank << 15) + (uint32_t)(addr - 0x8000u);
        gbus_write8(b, a68, val);
    }
}
