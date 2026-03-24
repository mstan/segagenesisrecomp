/*
 * runtime.c — Genesis 68K memory map and I/O stubs.
 *
 * Implements the m68k_read/m68k_write functions declared in genesis_runtime.h.
 * Routes accesses to:
 *   $000000–$3FFFFF  →  g_rom[]
 *   $A00000–$A0FFFF  →  Z80 address space
 *   $A10000–$A1001F  →  I/O controller ports (3-button pad)
 *   $A11100          →  Z80 BUSREQ
 *   $A11200          →  Z80 RESET
 *   $C00000          →  VDP data port
 *   $C00004          →  VDP control port
 *   $C00011          →  PSG write
 *   $FF0000–$FFFFFF  →  g_ram[] (64KB work RAM)
 *
 * All hardware I/O is stubbed with TODO markers. Implement as game calls them.
 * Ghidra-first: do not implement anything until the game actually hits the stub.
 */
#include "genesis_runtime.h"
#include "launcher.h"
#include "game_extras.h"
#include "vdp.h"
#include "psg.h"
#include "ym2612.h"
#include "z80.h"
#include "logger.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* ---- Global state ---- */
M68KState g_cpu;
uint8_t   g_rom[0x400000];
uint8_t   g_ram[0x010000];
uint64_t  g_frame_count = 0;
uint8_t   g_controller1_buttons = 0;
uint8_t   g_controller2_buttons = 0;

uint32_t g_miss_count_any   = 0;
uint32_t g_miss_last_addr   = 0;
uint64_t g_miss_last_frame  = 0;
uint32_t g_miss_unique_addrs[MAX_MISS_UNIQUE];
int      g_miss_unique_count = 0;

/* ---- Frame sync ---- */
/* VBlank and HBlank are serviced on the GAME THREAD (not the main thread) to
 * eliminate races on g_ram. The main thread sets g_vblank_pending=1 and waits
 * on s_vblank_done. The game thread checks g_vblank_pending at every m68k_read8
 * call, services all interrupts on its own stack, then posts s_vblank_done.
 * This is equivalent to the hardware preemptive interrupt model. */
volatile int  g_vblank_pending = 0;
static SDL_sem *s_vblank_done  = NULL;

static void service_vblank(void) {
    static int sv_count = 0;
    if (++sv_count <= 5)
        fprintf(stderr, "[SERVICE_VBLANK] #%d (frame %llu SR=%04X)\n", sv_count, (unsigned long long)g_frame_count, g_cpu.SR);
    g_vblank_pending = 0;

    /* Respect the 68K interrupt priority mask in SR[10:8].
     * VBlank = IRQ6: fires only when mask < 6.
     * HBlank = IRQ4: fires only when mask < 4.
     * When masked, the interrupt is silently dropped for this frame.
     *
     * Hardware RTI semantics: the CPU saves SR before entering the handler and
     * restores it on RTI. Our recompiled handlers don't do this (RTI → return),
     * so we save/restore SR manually around each call. The handler raises the
     * I-mask to 7 internally but we restore the pre-interrupt SR afterwards,
     * matching the hardware RTI behavior. */
    uint16_t saved_sr = g_cpu.SR;
    int imask = (saved_sr >> 8) & 7;

    vdp_set_vblank(1);

    /* HBlank fires every (reg10 + 1) scanlines (hardware: counter decrements each
     * line, IRQ fires when it wraps through 0, then reloads from reg10).
     * reg10 = 0  → every line (224 calls per frame — Sonic 1 water effect).
     * reg10 = N  → every N+1 lines. */
    if (imask < 4) {
        int hblank_interval = (int)g_vdp_regs[10] + 1;
        for (int line = 0; line < GENESIS_SCREEN_H; line++) {
            if (((line + 1) % hblank_interval) == 0) {
                g_cpu.SR = (uint16_t)((saved_sr & 0x00FFu) | 0x0500u); /* mask=5 during HBlank */
                vdp_set_hblank(1);
                game_call_hblank();
                vdp_set_hblank(0);
                g_cpu.SR = saved_sr;
            }
        }
    }

    if (imask < 6) {
        g_cpu.SR = (uint16_t)((saved_sr & 0x00FFu) | 0x0700u); /* mask=7 during VBlank */
        game_call_vblank();
        g_cpu.SR = saved_sr;
    }

    vdp_set_vblank(0);
    SDL_SemPost(s_vblank_done);
}

void runtime_request_vblank(void) {
    static int rv_count = 0;
    if (++rv_count <= 5)
        fprintf(stderr, "[VBLANK_REQ] #%d (frame %llu)\n", rv_count, (unsigned long long)g_frame_count);
    g_vblank_pending = 1;
    SDL_SemWait(s_vblank_done);
}

/* ---- I/O controller state ---- */
/* Last byte written to each port data register — bit6 is the TH line.
 * Default TH=high ($40), matching power-on state. */
static uint8_t s_port_th[2]  = {0x40, 0x40};
static uint8_t s_ym_addr[2]  = {0, 0};   /* YM2612 selected register per port */
static uint8_t s_busreq     = 0;   /* Z80 bus request state (ignored; always granted) */

/* Reconstruct the hardware active-low byte for a controller port read.
 * g_controller1_buttons / g_controller2_buttons are 1=pressed in final format:
 *   bit0=Up, bit1=Down, bit2=Left, bit3=Right, bit4=B, bit5=C, bit6=A, bit7=Start
 *
 * TH=1 phase (verified against plutiedev.com + s1disasm ReadJoypads):
 *   bit6=TH(1), bit5=~C, bit4=~B, bit3=~Right, bit2=~Left, bit1=~Down, bit0=~Up
 *
 * TH=0 phase (verified against plutiedev.com + s1disasm ReadJoypads):
 *   bit6=TH(0), bit5=~Start, bit4=~A, bit3=0(forced), bit2=0(forced), bit1=~Down, bit0=~Up
 *   Sonic 1 ReadJoypads reads bits 5:4 via: lsl.b #2,d0 / andi.b #$C0,d0 */
static uint8_t io_read_port(int port) {
    uint8_t buttons = (port == 0) ? g_controller1_buttons : g_controller2_buttons;
    uint8_t b = ~buttons;   /* invert: active-low (0=pressed on hardware) */
    if (s_port_th[port] & 0x40) {
        /* TH=high: D-pad + B + C active-low in bits[5:0], TH=1 in bit6 */
        return (uint8_t)((b & 0x3F) | 0x40);
    } else {
        /* TH=low: ~Start at bit5, ~A at bit4, 0 forced at bits[3:2], ~Down/~Up at bits[1:0]
         * b bit7=~Start → (b>>2)&0x20 = bit5
         * b bit6=~A     → (b>>2)&0x10 = bit4 */
        return (uint8_t)((b & 0x03) | ((b >> 2) & 0x30));
    }
}

/* ---- Memory read ---- */

uint8_t m68k_read8(uint32_t addr) {
    /* Frame sync yield point: service VBlank+HBlank on game thread if requested */
    if (g_vblank_pending) service_vblank();
    addr &= 0xFFFFFF;
    if (addr == 0xFFF62A) {
        static int r62a_count = 0;
        if (++r62a_count <= 10)
            fprintf(stderr, "[READ_F62A] #%d val=%02X (frame %llu)\n",
                    r62a_count, g_ram[addr - 0xFF0000], (unsigned long long)g_frame_count);
    }
    if (addr < 0x800000) {
        uint32_t rom_size = launcher_get_rom_size();
        /* Log ROM reads in NemDec code-table init range ($1DC00–$1E000) */
        if (addr >= 0x1DC00 && addr < 0x1E000) {
            static int nem_rom_count = 0;
            if (++nem_rom_count <= 64)
                fprintf(stderr, "[NEM_ROM] addr=%06X val=%02X\n",
                        addr, rom_size ? g_rom[addr % rom_size] : 0xFF);
        }
        if (rom_size) return g_rom[addr % rom_size];
        return 0xFF;
    }
    if (addr >= 0xFF0000) {
        /* Log reads from code table range $FFAA00-$FFAAFF (NemDec code table) */
        if (addr >= 0xFFAA00 && addr <= 0xFFAAFF) {
            static int nem_read_count = 0;
            if (++nem_read_count <= 32)
                fprintf(stderr, "[NEM_READ] addr=%06X val=%02X (frame %llu)\n",
                        addr, g_ram[addr - 0xFF0000], (unsigned long long)g_frame_count);
        }
        return g_ram[addr - 0xFF0000];
    }
    if (addr >= 0xA00000 && addr < 0xA10000) return z80_read((uint16_t)(addr & 0xFFFF));
    if (addr >= 0xA10000 && addr <= 0xA1001F) {
        /* I/O controller registers (byte-width, accessed at odd addresses):
         *   $A10001 = Version register (domestic NTSC, no disk)
         *   $A10003 = Port 1 data (joypad 1, TH-multiplexed)
         *   $A10005 = Port 2 data (joypad 2)
         *   $A10009/$A1000B = Port 1/2 control (direction); write-only in practice */
        if (addr == 0xA10001) return 0x00;           /* version: domestic, NTSC, rev 0 */
        if (addr == 0xA10003) return io_read_port(0); /* joypad 1 */
        if (addr == 0xA10005) return io_read_port(1); /* joypad 2 */
        return 0xFF;
    }
    /* Z80 BUSREQ ($A11100): bit0=0 means bus granted; always grant immediately */
    if (addr == 0xA11100 || addr == 0xA11200) return 0x00;
    /* YM2612: $A04000/$A04002 = status register (bit7=busy), $A04001/$A04003 = write-only data */
    if (addr >= 0xA04000 && addr <= 0xA04003) {
        return (addr & 1) ? 0x00 : ym2612_read_status();
    }
    /* VDP: even address → high byte, odd address → low byte (matches 68K byte lane) */
    if (addr >= 0xC00000 && addr <= 0xC00003) {
        uint16_t d = vdp_read_data();
        return (addr & 1) ? (uint8_t)(d & 0xFF) : (uint8_t)(d >> 8);
    }
    if (addr >= 0xC00004 && addr <= 0xC00007) {
        uint16_t s = vdp_read_status();
        return (addr & 1) ? (uint8_t)(s & 0xFF) : (uint8_t)(s >> 8);
    }
    return 0xFF;
}

uint16_t m68k_read16(uint32_t addr) {
    if (g_vblank_pending) service_vblank();
    addr &= 0xFFFFFF;
    if (addr < 0x800000) {
        uint32_t rom_size = launcher_get_rom_size();
        if (rom_size) return (uint16_t)(g_rom[addr % rom_size] << 8 | g_rom[(addr+1) % rom_size]);
        return 0xFFFF;
    }
    if (addr >= 0xFF0000) {
        uint32_t r = addr - 0xFF0000;
        return (uint16_t)(g_ram[r] << 8 | g_ram[r+1]);
    }
    if (addr >= 0xA00000 && addr < 0xA10000) {
        uint16_t a = addr & 0xFFFF;
        return (uint16_t)(z80_read(a) << 8 | z80_read((uint16_t)(a + 1)));
    }
    if (addr >= 0xA10000 && addr <= 0xA1001F) {
        /* 16-bit I/O reads: register value in high byte, low byte undefined */
        /* GPGX: register value replicated to both bytes (data << 8 | data) */
        if (addr == 0xA10000) return 0x0000;
        if (addr == 0xA10002) { uint16_t d = io_read_port(0); return (uint16_t)(d << 8 | d); }
        if (addr == 0xA10004) { uint16_t d = io_read_port(1); return (uint16_t)(d << 8 | d); }
        return 0xFFFF;
    }
    /* Z80 BUSREQ: always grant */
    if (addr == 0xA11100 || addr == 0xA11200) return 0x0000;
    /* YM2612: status in high byte (even addr), low byte = 0 */
    if (addr == 0xA04000 || addr == 0xA04002) {
        uint16_t s = ym2612_read_status(); return (uint16_t)(s << 8);
    }
    if (addr == 0xC00000 || addr == 0xC00002) return vdp_read_data();
    if (addr == 0xC00004 || addr == 0xC00006) return vdp_read_status();
    /* TODO: other regions */
    return 0xFFFF;
}

uint32_t m68k_read32(uint32_t addr) {
    return (uint32_t)(m68k_read16(addr) << 16) | m68k_read16(addr + 2);
}

/* ---- Memory write ---- */

void m68k_write8(uint32_t addr, uint8_t val) {
    addr &= 0xFFFFFF;
    if (addr >= 0xFF0000) { g_ram[addr - 0xFF0000] = val; return; }
    if (addr >= 0xA00000 && addr < 0xA10000) { z80_write((uint16_t)(addr & 0xFFFF), val); return; }
    /* I/O port writes: capture TH line state (bit6) per port */
    if (addr == 0xA10003) { s_port_th[0] = val; return; }  /* port 1 data */
    if (addr == 0xA10005) { s_port_th[1] = val; return; }  /* port 2 data */
    if (addr == 0xA10009 || addr == 0xA1000B) { return; }  /* port ctrl/dir — ignored */
    /* YM2612: two-phase write (addr then data) */
    if (addr == 0xA04000) { s_ym_addr[0] = val; return; }  /* port 0 address */
    if (addr == 0xA04001) { ym2612_write(0, s_ym_addr[0], val); return; }  /* port 0 data */
    if (addr == 0xA04002) { s_ym_addr[1] = val; return; }  /* port 1 address */
    if (addr == 0xA04003) { ym2612_write(1, s_ym_addr[1], val); return; }  /* port 1 data */
    /* Z80 BUSREQ/RESET: store state but always grant (no real Z80) */
    if (addr == 0xA11100) { s_busreq = val; fprintf(stderr, "[Z80_BYTE] busreq=%02X\n", val); return; }
    if (addr == 0xA11200) { return; }
    /* VDP byte writes: replicate byte to both halves, matching GPGX vdp_write_byte behavior */
    if (addr >= 0xC00000 && addr <= 0xC00003) { vdp_write_data((uint16_t)(val << 8 | val)); return; }
    if (addr >= 0xC00004 && addr <= 0xC00007) { vdp_write_ctrl((uint16_t)(val << 8 | val)); return; }
    if (addr == 0xC00011) { psg_write(val); return; }
}

void m68k_write16(uint32_t addr, uint16_t val) {
    addr &= 0xFFFFFF;
    if (addr >= 0xFF0000) {
        uint32_t r = addr - 0xFF0000;
        /* Log writes to code table range $FFAA00-$FFAAFF (NemDec code table init) */
        if (addr >= 0xFFAA00 && addr <= 0xFFAAFF) {
            if (val != 0) {
                /* Non-zero: always log — these are the colored pixel group entries */
                fprintf(stderr, "[NEM_ENTRY] addr=%06X val=%04X (frame %llu)\n",
                        addr, val, (unsigned long long)g_frame_count);
            } else {
                /* Zero: only log first 5 (clearing phase) */
                static int nem_zero_count = 0;
                if (++nem_zero_count <= 5)
                    fprintf(stderr, "[NEM_ZERO] #%d addr=%06X (frame %llu)\n",
                            nem_zero_count, addr, (unsigned long long)g_frame_count);
            }
        }
        g_ram[r]   = (uint8_t)(val >> 8);
        g_ram[r+1] = (uint8_t)(val & 0xFF);
        return;
    }
    if (addr >= 0xA00000 && addr < 0xA10000) {
        uint16_t a = addr & 0xFFFF;
        z80_write(a,                  (uint8_t)(val >> 8));
        z80_write((uint16_t)(a + 1),  (uint8_t)(val & 0xFF));
        return;
    }
    /* I/O port writes: high byte is the data byte (register at odd address) */
    if (addr == 0xA10002) { s_port_th[0] = (uint8_t)(val >> 8); return; }
    if (addr == 0xA10004) { s_port_th[1] = (uint8_t)(val >> 8); return; }
    if (addr == 0xA10008 || addr == 0xA1000A) { return; }  /* port ctrl — ignored */
    /* YM2612 16-bit: high byte = address latch, low byte = data (matches hardware byte-lane behavior) */
    if (addr == 0xA04000) { s_ym_addr[0] = (uint8_t)(val >> 8); ym2612_write(0, s_ym_addr[0], (uint8_t)(val & 0xFF)); return; }
    if (addr == 0xA04002) { s_ym_addr[1] = (uint8_t)(val >> 8); ym2612_write(1, s_ym_addr[1], (uint8_t)(val & 0xFF)); return; }
    /* Z80 BUSREQ/RESET: always grant */
    if (addr == 0xA11100) { s_busreq = (uint8_t)(val >> 8); fprintf(stderr, "[Z80_WORD] busreq=%04X\n", val); return; }
    if (addr == 0xA11200) { return; }
    if (addr == 0xC00000 || addr == 0xC00002) { vdp_write_data(val); return; }
    if (addr == 0xC00004 || addr == 0xC00006) { vdp_write_ctrl(val); return; }
    if (addr == 0xC00011) { psg_write((uint8_t)(val & 0xFF)); return; }
    /* TODO: other write targets */
}

void m68k_write32(uint32_t addr, uint32_t val) {
    m68k_write16(addr,     (uint16_t)(val >> 16));
    m68k_write16(addr + 2, (uint16_t)(val & 0xFFFF));
}

/* ---- Dispatch miss ---- */

void genesis_log_dispatch_miss(uint32_t addr) {
    g_miss_count_any++;
    g_miss_last_addr  = addr;
    g_miss_last_frame = g_frame_count;

    for (int i = 0; i < g_miss_unique_count; i++)
        if (g_miss_unique_addrs[i] == addr) return;

    if (g_miss_unique_count < MAX_MISS_UNIQUE) {
        g_miss_unique_addrs[g_miss_unique_count++] = addr;
        fprintf(stderr, "[DISPATCH MISS] $%06X (frame %llu)\n",
                addr, (unsigned long long)g_frame_count);
    }
}

/* ---- Runtime init ---- */

void runtime_init(void) {
    memset(&g_cpu, 0, sizeof(g_cpu));
    memset(g_ram,  0, sizeof(g_ram));
    s_vblank_done = SDL_CreateSemaphore(0);
    vdp_init();
    z80_init();
    ym2612_init(44100);
}

void genesis_vblank_callback(void) {
    /* TODO: trigger V-blank interrupt to 68K */
    game_call_vblank();
}
