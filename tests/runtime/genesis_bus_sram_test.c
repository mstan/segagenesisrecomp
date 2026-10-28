#include "genesis_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_mapping_policy(void)
{
    GenesisBus bus;
    memset(&bus, 0, sizeof(bus));
    bus.sram_present = 1;
    bus.sram_base = 0x200000u;
    bus.sram_end = 0x203FFFu;
    bus.sram_size = 0x4000u;

    CHECK(!gbus_sram_mapped_at(&bus, 0x200000u));
    bus.sram_enabled = 1;
    CHECK(gbus_sram_mapped_at(&bus, 0x200000u));
    CHECK(gbus_sram_mapped_at(&bus, 0x203FFFu));
    CHECK(!gbus_sram_mapped_at(&bus, 0x204000u));

    bus.sram_enabled = 0;
    bus.sram_always_mapped = 1;
    CHECK(gbus_sram_mapped_at(&bus, 0x200000u));
    CHECK(gbus_sram_mapped_at(&bus, 0x203FFFu));

    CHECK(!gbus_sram_range_is_always_mapped(0x100000u, 0u));
    CHECK(!gbus_sram_range_is_always_mapped(0x0FFFFEu, 0x100000u));
    CHECK(gbus_sram_range_is_always_mapped(0x100000u, 0x100000u));
    CHECK(gbus_sram_range_is_always_mapped(0x200000u, 0x100000u));
}

static void test_geometry_restore(void)
{
    GenesisBus bus;
    memset(&bus, 0, sizeof(bus));
    bus.sram_present = 1;
    bus.sram_base = 0x200000u;
    bus.sram_end = 0x203FFFu;
    bus.sram_size = 0x4000u;
    bus.sram_always_mapped = 1;

    GenesisBus saved;
    memset(&saved, 0, sizeof(saved));
    saved.sram_present = 1;
    saved.sram_enabled = 1;
    saved.sram_base = 0x100000u;
    saved.sram_end = 0x1003FFu;
    saved.sram_size = 0x400u;
    saved.sram[0] = 0x53u;
    saved.sram[0x3FFF] = 0x46u;

    const GenesisSramGeometry geometry = gbus_sram_geometry(&bus);
    memcpy(&bus, &saved, sizeof(bus));
    gbus_sram_restore_geometry(&bus, geometry);

    CHECK(bus.sram_present == 1);
    CHECK(bus.sram_enabled == 1);
    CHECK(bus.sram_always_mapped == 1);
    CHECK(bus.sram_base == 0x200000u);
    CHECK(bus.sram_end == 0x203FFFu);
    CHECK(bus.sram_size == 0x4000u);
    CHECK(bus.sram[0] == 0x53u);
    CHECK(bus.sram[0x3FFF] == 0x46u);
}

int main(void)
{
    test_mapping_policy();
    test_geometry_restore();
    return 0;
}
