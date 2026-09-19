#pragma once
#include <stdint.h>
int s2_runtime_hook(uint32_t pc);
void s2_runtime_load(void);
const char *s2_runtime_state_unavailable_reason(void);
struct GVDP;
void s2_runtime_overlay(const struct GVDP *vdp, int line, uint32_t *out, int width);
