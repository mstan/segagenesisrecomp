#pragma once
#include <stdint.h>
void s2_save_menu_load(const char *settings_path);
int s2_save_menu_hook(uint32_t pc);
int s2_save_menu_overlay(int line,uint32_t *out,int width);
int s2_save_menu_enabled(void);
