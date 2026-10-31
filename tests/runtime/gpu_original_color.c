#include "pixel_reference/genesis_dac.h"
uint32_t gpu_frozen_dac(uint16_t word,int shade)
{return genesis_dac_cram_to_argb(word,shade);}
