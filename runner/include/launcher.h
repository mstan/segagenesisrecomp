#pragma once
#include <stdbool.h>
#include <stdint.h>

bool     launcher_pick_rom (char *out_path, int max_len);
bool     launcher_load_rom (const char *path);
bool     launcher_verify_crc(void);
uint32_t launcher_get_rom_size(void);
