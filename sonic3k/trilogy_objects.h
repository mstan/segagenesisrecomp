#pragma once
#include "trilogy_assets.h"
#include <stdint.h>
#include "trilogy_state.h"
void tr_objects_reset(TrStageAssets *assets);
void tr_objects_load(void);
int tr_objects_dispatch(uint32_t address);
int tr_objects_read16(uint32_t address,uint16_t *value);
unsigned tr_objects_live(void);
int tr_objects_results_started(void);
void tr_objects_special_save(void);
void tr_objects_special_restore(void);
void tr_objects_start_results(void);
void tr_objects_checkpoint_stars(unsigned checkpoint);
void tr_objects_state(TrStateIO *io,TrStageAssets *const imports[4]);
