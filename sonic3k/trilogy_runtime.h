#pragma once
#include <stdint.h>
void tr_runtime_settings(const char *path);
void tr_runtime_sram_loaded(void);
int tr_runtime_hook(uint32_t pc);
int tr_runtime_read16(uint32_t address,uint16_t *word);
const char *tr_runtime_state_reason(void);
int tr_runtime_netplay_allowed(void);
unsigned tr_runtime_stage_id(void);
