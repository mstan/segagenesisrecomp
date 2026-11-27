#pragma once
#include <recomp_mod.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Initialize once from the verified ORIGINAL input, before guest execution.
 * Applies only to the owner-thread safe point. The original disk image is
 * never a destination; both guest RAM and loaded cartridge bytes are valid. */
int genesis_mod_initialize(const char *game,const void *original,size_t size);
int genesis_mod_apply(const char *plan,const rmod_patch *patches,size_t count);
const char *genesis_mod_error(void);
#ifdef __cplusplus
}
#endif
