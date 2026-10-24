#include "genesis_machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

unsigned long g_snd_frame;
unsigned g_snd_line;

void gbus_init(GenesisBus *b, GVDP *vdp)
{
    memset(b, 0, sizeof(*b));
    b->vdp = vdp;
}

uint16_t gbus_read16(GenesisBus *b, uint32_t addr)
{
    (void)b;
    (void)addr;
    return 0;
}

uint8_t gbus_z80_read(GenesisBus *b, uint16_t addr)
{
    (void)b;
    (void)addr;
    return 0;
}

void gbus_z80_write(GenesisBus *b, uint16_t addr, uint8_t val)
{
    (void)b;
    (void)addr;
    (void)val;
}

void glue_run_game_chunk(uint32_t cycles)
{
    (void)cycles;
}

void glue_own_interrupt(int level, GVDP *vdp)
{
    (void)level;
    (void)vdp;
}

int glue_own_vint_service_latched(GVDP *vdp)
{
    (void)vdp;
    return 0;
}

void gvdp_init(GVDP *v)
{
    memset(v, 0, sizeof(*v));
}

unsigned gvdp_begin_scanline(GVDP *v, int line)
{
    (void)v;
    (void)line;
    return 0;
}

void gvdp_evaluate_scanline(GVDP *v, int line)
{
    (void)v;
    (void)line;
}

int gvdp_ext_active(void)
{
    return 0;
}

uint16_t gvdp_ext_color(int row, int line, int nib)
{
    (void)row;
    (void)line;
    (void)nib;
    return 0;
}

const uint32_t *gvdp_host_palette(void)
{
    return NULL;
}

int gvdp_interlace_double(const GVDP *v)
{
    (void)v;
    return 0;
}

uint32_t gvdp_rb_pending_stall(void)
{
    return 0;
}

void gvdp_rb_set_pending_stall(uint32_t v)
{
    (void)v;
}

int gvdp_render_scanline(GVDP *v, int line, uint8_t *out)
{
    (void)v;
    (void)line;
    (void)out;
    return 0;
}

int gvdp_screen_height(const GVDP *v)
{
    (void)v;
    return 224;
}

void gvdp_set_unlimited_sprites(int enabled)
{
    (void)enabled;
}

int gvdp_unlimited_sprites(void)
{
    return 0;
}

void z80_init(z80 *const state)
{
    (void)state;
}

void z80_step(z80 *const state)
{
    (void)state;
}

static void set_current_geometry(GenesisBus *bus)
{
    bus->sram_present = 1;
    bus->sram_base = 0x200000u;
    bus->sram_end = 0x203FFFu;
    bus->sram_size = 0x4000u;
    bus->sram_always_mapped = 1;
}

static void set_saved_state(GenesisMachine *machine)
{
    memset(machine, 0, sizeof(*machine));
    machine->bus.sram_present = 1;
    machine->bus.sram_enabled = 1;
    machine->bus.sram_base = 0x100000u;
    machine->bus.sram_end = 0x1003FFu;
    machine->bus.sram_size = 0x400u;
    machine->bus.sram[0] = 0x53u;
    machine->bus.sram[0x3FFF] = 0x46u;
}

static void check_loaded_state(void)
{
    CHECK(g_machine.bus.sram_present == 1);
    CHECK(g_machine.bus.sram_enabled == 1);
    CHECK(g_machine.bus.sram_always_mapped == 1);
    CHECK(g_machine.bus.sram_base == 0x200000u);
    CHECK(g_machine.bus.sram_end == 0x203FFFu);
    CHECK(g_machine.bus.sram_size == 0x4000u);
    CHECK(g_machine.bus.sram[0] == 0x53u);
    CHECK(g_machine.bus.sram[0x3FFF] == 0x46u);
}

static void test_file_state_load(void)
{
    GenesisMachine saved;
    set_saved_state(&saved);

    FILE *state = tmpfile();
    CHECK(state != NULL);
    CHECK(fwrite(&saved, sizeof(saved), 1, state) == 1);
    const uint64_t saved_frame = 77u;
    CHECK(fwrite(&saved_frame, sizeof(saved_frame), 1, state) == 1);
    rewind(state);

    memset(&g_machine, 0, sizeof(g_machine));
    set_current_geometry(&g_machine.bus);
    CHECK(machine_load_state(state));
    CHECK(g_snd_frame == saved_frame);
    check_loaded_state();
    fclose(state);
}

static void test_rollback_state_load(void)
{
    set_saved_state(&g_machine);
    g_snd_frame = 91u;
    size_t state_size = machine_rb_save(NULL, 0);
    void *state = malloc(state_size);
    CHECK(state != NULL);
    CHECK(machine_rb_save(state, state_size) == state_size);

    memset(&g_machine, 0, sizeof(g_machine));
    set_current_geometry(&g_machine.bus);
    g_snd_frame = 0;
    CHECK(machine_rb_load(state, state_size));
    CHECK(g_snd_frame == 91u);
    check_loaded_state();
    free(state);
}

int main(void)
{
    test_file_state_load();
    test_rollback_state_load();
    return 0;
}
