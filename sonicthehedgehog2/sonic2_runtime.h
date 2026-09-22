#pragma once
#include <stdint.h>
#include <stddef.h>
size_t s2_runtime_state_size(void);
int s2_runtime_state_save(void *data,size_t size);
int s2_runtime_state_load(const void *data,size_t size,int apply);
int s2_runtime_state_at_boundary(void);
int s2_runtime_hook(uint32_t pc);
void s2_runtime_load(void);
void s2_runtime_capture(void);
const char *s2_runtime_state_unavailable_reason(void);
struct GVDP;
void s2_runtime_overlay(const struct GVDP *vdp, int line, uint32_t *out, int width);
