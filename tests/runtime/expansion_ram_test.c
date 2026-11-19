#include "genesis_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext,g_sndwatch[0x2000];
uint32_t g_rom_ext_size,g_audio_cycle_counter,g_68k_stamp_rebase;
unsigned long g_snd_frame;
unsigned g_snd_pcz;
static uint32_t s_test_z80_stamp;
uint32_t machine_z80_stamp(void) { return s_test_z80_stamp; }
uint32_t machine_z80_pc(void) { return 0; }
void glue_charge_68k_stall(uint32_t x) { (void)x; }
void snd_trace_68k_write(uint16_t a,uint8_t b,uint8_t c,int d,int e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
void snd_trace_z80(uint16_t a,uint8_t b,int c) { (void)a;(void)b;(void)c; }
void snd_trace_busreq(const char *a,int b) { (void)a;(void)b; }
void audio_event_push(uint32_t a,uint8_t b,uint8_t c) { (void)a;(void)b;(void)c; }
uint16_t gvdp_read_data(GVDP *v) { (void)v;return 0x1234; }
uint16_t gvdp_read_control(GVDP *v) { (void)v;return 0x5678; }
uint16_t gvdp_peek_data(const GVDP *v) { (void)v;return 0x1234; }
uint16_t gvdp_peek_status(const GVDP *v) { (void)v;return 0x5678; }
uint16_t gvdp_read_hv_counter(const GVDP *v) { (void)v;return 0; }
void gvdp_write_data(GVDP *v,uint16_t x) { (void)v;(void)x; }
void gvdp_write_control(GVDP *v,uint16_t x) { (void)v;(void)x; }
uint32_t gvdp_consume_68k_stall(GVDP *v) { (void)v;return 0; }

static void test_timer_b_acknowledge(void)
{
    GenesisBus b;
    memset(&b, 0, sizeof b);
    g_snd_frame = 0;
    s_test_z80_stamp = 105;
    gbus_z80_write(&b, 0x4000, 0x26);
    gbus_z80_write(&b, 0x4001, 0xff);
    gbus_z80_write(&b, 0x4000, 0x27);
    gbus_z80_write(&b, 0x4001, 0x2a); /* clear B, load B, enable B flag */
    const uint64_t deadline = b.ym_timer_b_deadline;
    CHECK(deadline == 105u + 16128u);
    s_test_z80_stamp = 805;
    gbus_z80_write(&b, 0x4001, 0x2a); /* acknowledge without stopping */
    CHECK(b.ym_timer_b_deadline == deadline);
    s_test_z80_stamp = (uint32_t)deadline - 1;
    CHECK((gbus_z80_read(&b, 0x4000) & 2) == 0);
    s_test_z80_stamp++;
    CHECK((gbus_z80_read(&b, 0x4000) & 2) == 2);
    CHECK(b.ym_timer_b_deadline == deadline + 16128u);
    gbus_z80_write(&b, 0x4001, 0x2a);
    CHECK((gbus_z80_read(&b, 0x4000) & 2) == 0);
    CHECK(b.ym_timer_b_deadline == deadline + 16128u);
    gbus_z80_write(&b, 0x4001, 0x28); /* stop, then a new load restarts */
    CHECK(!b.ym_timer_b_running);
    s_test_z80_stamp += 700;
    gbus_z80_write(&b, 0x4001, 0x2a);
    CHECK(b.ym_timer_b_deadline == s_test_z80_stamp + 16128u);
    /* A status read in the next guest frame uses the same absolute clock. */
    g_snd_frame = 1;
    s_test_z80_stamp = 0;
    CHECK((gbus_z80_read(&b, 0x4000) & 2) == 2);
    g_snd_frame = 0;
    s_test_z80_stamp = 0;
}

static void write_ym_z80(GenesisBus *bus, uint8_t reg, uint8_t value)
{
    gbus_z80_write(bus, 0x4000, reg);
    gbus_z80_write(bus, 0x4001, value);
}

static void test_timer_a_reload(void)
{
    GenesisBus bus;
    gbus_init(&bus, NULL);
    s_test_z80_stamp = 105;
    write_ym_z80(&bus, 0x24, 0xfe);
    write_ym_z80(&bus, 0x25, 0xfd); /* Low two bits only: period 7 * 1008. */
    write_ym_z80(&bus, 0x27, 0x05);
    const uint32_t first_deadline = 105u + 7056u;
    CHECK(bus.ym_timer_a_deadline == first_deadline);

    s_test_z80_stamp = 805;
    write_ym_z80(&bus, 0x27, 0x15);
    CHECK(bus.ym_timer_a_deadline == first_deadline);
    write_ym_z80(&bus, 0x25, 0x02); /* Timer A reloads with 6 * 1008. */
    write_ym_z80(&bus, 0x27, 0x55);
    CHECK(bus.ym_timer_a_deadline == first_deadline);
    s_test_z80_stamp = first_deadline - 1;
    CHECK((gbus_z80_read(&bus, 0x4000) & 3u) == 0);
    s_test_z80_stamp++;
    CHECK((gbus_z80_read(&bus, 0x4000) & 3u) == 1);
    CHECK(bus.ym_timer_a_deadline == first_deadline + 6048u);
    write_ym_z80(&bus, 0x27, 0x11); /* Clear A and turn off its flag. */
    s_test_z80_stamp += 2u * 6048u;
    CHECK((gbus_z80_read(&bus, 0x4000) & 3u) == 0);
    CHECK(bus.ym_timer_a_deadline == first_deadline + 3u * 6048u);
    write_ym_z80(&bus, 0x27, 0x04);
    CHECK(!bus.ym_timer_a_running);
    s_test_z80_stamp += 17;
    write_ym_z80(&bus, 0x27, 0x05);
    CHECK(bus.ym_timer_a_deadline == s_test_z80_stamp + 6048u);
    s_test_z80_stamp = 0;
}

int main(void)
{
    test_timer_b_acknowledge();
    test_timer_a_reload();
    GenesisBus b;
    memset(&b,0,sizeof b);
    uint8_t guarded[10];memset(guarded,0xCC,sizeof guarded);
    CHECK(gbus_read16(&b,0x900000)==0xFFFF);
    CHECK(gbus_map_expansion_ram(0x900000,guarded+1,8));
    gbus_write16(&b,0x900000,0x1234);
    CHECK(gbus_read8(&b,0x900000)==0x12 && gbus_read8(&b,0x900001)==0x34);
    gbus_write8(&b,0x900007,0xAB);
    CHECK(gbus_peek8(&b,0x900007)==0xAB && gbus_peek16(&b,0x900006)==0xCCAB);
    CHECK(gbus_read16(&b,0x8FFFFE)==0xFFFF && gbus_read16(&b,0x900008)==0xFFFF);
    gbus_write16(&b,0x900008,0xBEEF);gbus_write8(&b,0x8FFFFF,0);
    CHECK(guarded[0]==0xCC && guarded[9]==0xCC);
    CHECK(!gbus_map_expansion_ram(0x900001,guarded+1,8));
    CHECK(!gbus_map_expansion_ram(0x900000,guarded+1,7));
    CHECK(!gbus_map_expansion_ram(0x9FFFFE,guarded+1,4));
    CHECK(!gbus_map_expansion_ram(0xA00000,guarded+1,8));
    CHECK(!gbus_map_expansion_ram(0x400000,guarded+1,8));
    CHECK(!gbus_map_expansion_ram(0xFF0000,guarded+1,8));
    CHECK(gbus_read16(&b,0x900000)==0x1234); /* invalid map is atomic */
    gbus_write16(&b,0xFF0000,0x2468);CHECK(g_ram[0]==0x24 && g_ram[1]==0x68);
    b.z80_bank=0x900000>>15;
    CHECK(gbus_z80_read(&b,0x8001)==0x34);
    gbus_z80_write(&b,0x8001,0x56);CHECK(gbus_read16(&b,0x900000)==0x1256);
    CHECK(gbus_map_expansion_ram(0,NULL,0));
    CHECK(gbus_read16(&b,0x900000)==0xFFFF && guarded[2]==0x56);
    puts("Expansion RAM byte/word/peek/Z80 mapping, guards, hardware isolation and disable passed");
    return 0;
}
