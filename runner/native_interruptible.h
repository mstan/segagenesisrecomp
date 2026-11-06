#ifndef GENESIS_NATIVE_INTERRUPTIBLE_H
#define GENESIS_NATIVE_INTERRUPTIBLE_H
#include <stdint.h>
/* Callback retires one compiled instruction, materializes g_cpu and returns
 * its guest-cycle cost. The subroutine retains the native CPU clock rather
 * than optional main-program headroom. VBlank observes instructions and may change
 * its return PC. The generated caller retains its single guest-stack pop. */
typedef int (*GenesisNativeInstruction)(void *user,unsigned *cycles);
int genesis_interruptible_native(uint32_t entry,GenesisNativeInstruction,void *user);
#endif
