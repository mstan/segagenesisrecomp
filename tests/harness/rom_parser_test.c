/*
 * rom_parser_test.c - ROM header acceptance/rejection checks.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rom_parser.h"

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); \
        g_failures++; \
    } \
} while (0)

static int write_file(const char *path, const uint8_t *bytes, size_t nbytes) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(bytes, 1, nbytes, f) == nbytes;
    fclose(f);
    return ok;
}

static void make_valid_rom(uint8_t *rom, size_t size) {
    memset(rom, 0xFF, size);
    rom[0] = 0x00;
    rom[1] = 0xFF;
    rom[2] = 0xFE;
    rom[3] = 0x00;
    rom[4] = 0x00;
    rom[5] = 0x00;
    rom[6] = 0x02;
    rom[7] = 0x00;
    memcpy(rom + 0x100, "SEGA MEGA DRIVE ", 16);
    memset(rom + 0x120, ' ', 48);
    memcpy(rom + 0x120, "TEST ROM", 8);
}

int main(void) {
    const char *valid_path = "rom_parser_valid.tmp.bin";
    const char *html_path = "rom_parser_html.tmp.bin";

    uint8_t valid[0x400];
    make_valid_rom(valid, sizeof(valid));
    CHECK(write_file(valid_path, valid, sizeof(valid)), "write valid ROM fixture");

    uint8_t html[0x400];
    const char *html_text =
        "<!DOCTYPE html><html><head><meta name=\"viewport\" "
        "content=\"width=device-width, initial-scale=1\">";
    memset(html, ' ', sizeof(html));
    memcpy(html, html_text, strlen(html_text));
    CHECK(write_file(html_path, html, sizeof(html)), "write HTML fixture");

    GenesisRom rom = {0};
    CHECK(rom_parse(valid_path, &rom), "valid SEGA header is accepted");
    CHECK(rom.rom_size == sizeof(valid), "valid ROM size is preserved");
    CHECK(rom.initial_sp == 0x00FFFE00u, "initial SSP parsed big-endian");
    CHECK(rom.initial_pc == 0x00000200u, "initial PC parsed big-endian");
    rom_free(&rom);

    memset(&rom, 0, sizeof(rom));
    CHECK(!rom_parse(html_path, &rom), "HTML saved as .bin is rejected");
    CHECK(rom.rom_data == NULL, "failed parse leaves no ROM allocation");
    CHECK(rom.rom_size == 0, "failed parse clears ROM size");

    remove(valid_path);
    remove(html_path);

    return g_failures ? 1 : 0;
}
