/*
 * return_capture_test.c — synthetic all-path return-capture proofs.
 */
#include <stdio.h>
#include <string.h>

#include "return_capture.h"

static int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, message); \
        failures++; \
    } \
} while (0)

static GenesisRom make_rom(uint8_t *bytes, size_t size)
{
    GenesisRom rom;
    memset(&rom, 0, sizeof(rom));
    rom.rom_data = bytes;
    rom.rom_size = (uint32_t)size;
    return rom;
}

int main(void)
{
    /* Puyo $2B1C shape: MOVE.W D0,$24(A0); MOVE.L (A7)+,$2(A0); RTS. */
    uint8_t linear[] = {
        0x31, 0x40, 0x00, 0x24,
        0x21, 0x5F, 0x00, 0x02,
        0x4E, 0x75,
    };
    GenesisRom linear_rom = make_rom(linear, sizeof(linear));
    CHECK(m68k_return_capture_is_unconditional(&linear_rom, 0),
          "straight-line return capture must be proven");

    /*
     * Exact Puyo $2B26 shape: two conditional paths converge on distinct
     * MOVE.L (A7)+ instructions, so every path consumes the return address.
     */
    uint8_t all_paths[] = {
        0x4A, 0x68, 0x00, 0x24, 0x67, 0x00, 0x00, 0x0E,
        0x53, 0x68, 0x00, 0x24, 0x67, 0x00, 0x00, 0x06,
        0x20, 0x1F, 0x4E, 0x75, 0x21, 0x5F, 0x00, 0x02,
        0x4E, 0x75,
    };
    GenesisRom all_paths_rom = make_rom(all_paths, sizeof(all_paths));
    CHECK(m68k_return_capture_is_unconditional(&all_paths_rom, 0),
          "conditional helper with a pop on every path must be proven");

    /*
     * A superficially similar helper where the fall-through path returns
     * normally. Treating this as non-returning would skip valid caller code.
     */
    uint8_t mixed_paths[] = {
        0x4A, 0x40,             /* TST.W D0 */
        0x67, 0x02,             /* BEQ.S capture */
        0x4E, 0x75,             /* RTS */
        0x20, 0x1F,             /* capture: MOVE.L (A7)+,D0 */
        0x4E, 0x75,             /* RTS */
    };
    GenesisRom mixed_paths_rom = make_rom(mixed_paths, sizeof(mixed_paths));
    CHECK(!m68k_return_capture_is_unconditional(&mixed_paths_rom, 0),
          "helper with one ordinary-return path must be rejected");

    /*
     * Puyo $2C88's important shape: one path returns normally, while another
     * consumes the return slot with MOVEM.L (A7)+,D0 before RTS.  MOVEM must
     * be recognized as a return pop, but the routine as a whole must remain
     * conditional so codegen can propagate the skip dynamically.
     */
    uint8_t mixed_movem[] = {
        0x08, 0x00, 0x00, 0x00, /* BTST #0,D0 */
        0x66, 0x02,             /* BNE.S capture */
        0x4E, 0x75,             /* RTS */
        0x4C, 0xDF, 0x00, 0x01, /* capture: MOVEM.L (A7)+,D0 */
        0x4E, 0x75,             /* RTS */
    };
    GenesisRom mixed_movem_rom = make_rom(mixed_movem, sizeof(mixed_movem));
    CHECK(!m68k_return_capture_is_unconditional(&mixed_movem_rom, 0),
          "conditional MOVEM return capture must not be classified unconditional");
    CHECK(m68k_return_capture_is_unconditional(&mixed_movem_rom, 8),
          "MOVEM.L (A7)+ must be recognized as a return capture");

    if (failures) {
        fprintf(stderr, "%d return-capture test(s) failed\n", failures);
        return 1;
    }
    puts("return_capture_test: all tests passed");
    return 0;
}
