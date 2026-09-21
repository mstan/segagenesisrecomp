#pragma once
#include <stdint.h>
#include <stddef.h>
void tr_runtime_settings(const char *path);
void tr_runtime_sram_loaded(void);
int tr_runtime_hook(uint32_t pc);
int tr_runtime_read16(uint32_t address,uint16_t *word);
const char *tr_runtime_state_reason(void);
size_t tr_runtime_state_size(void);
int tr_runtime_state_save(void *data,size_t size);
int tr_runtime_state_load(const void *data,size_t size,int apply);
int tr_runtime_netplay_allowed(void);
unsigned tr_runtime_stage_id(void);
int tr_runtime_scene_required(void);
void tr_runtime_checkpoint_special(unsigned guest,unsigned number);
const uint32_t *tr_runtime_sprite_palette(uint32_t mapping);
