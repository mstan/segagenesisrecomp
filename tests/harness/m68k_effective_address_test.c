#include <stdint.h>
#include <stdio.h>

#include "genesis_runtime.h"

static int failures;

#define CHECK(actual, expected, label) do { \
    uint32_t _actual = (actual); \
    uint32_t _expected = (expected); \
    if (_actual != _expected) { \
        fprintf(stderr, "FAIL %s: got $%08X, expected $%08X\n", \
                (label), _actual, _expected); \
        failures++; \
    } \
} while (0)

int main(void)
{
    /* Puyo uses D3.L=$0000E040 at $015238. Treating this as D3.W sends the
     * score load to $FEE04A instead of the intended WRAM address $FFE04A. */
    CHECK(m68k_brief_index_value(0x3000u, 0x0000E040u, 0),
          0xFFFFE040u, "Dn.W sign extension");
    CHECK(m68k_brief_index_value(0x3800u, 0x0000E040u, 0),
          0x0000E040u, "Dn.L full-width index");

    CHECK(m68k_brief_index_value(0xB000u, 0, 0x12348000u),
          0xFFFF8000u, "An.W sign extension");
    CHECK(m68k_brief_index_value(0xB800u, 0, 0x12348000u),
          0x12348000u, "An.L full-width index");

    CHECK(0x00FF000Au + m68k_brief_index_value(0x3800u, 0x0000E040u, 0),
          0x00FFE04Au, "Puyo score effective address");

    if (failures) {
        fprintf(stderr, "m68k_effective_address_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("m68k_effective_address_test: all tests passed");
    return 0;
}
