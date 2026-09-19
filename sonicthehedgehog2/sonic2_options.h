#pragma once
#include <stdint.h>
struct GVDP;
int s2_options_hook(uint32_t pc);
void s2_options_overlay(const struct GVDP *vdp, int line, uint32_t *out, int width);
void s2_options_load(const char *settings_path);
int s2_options_netplay_allowed(void);
