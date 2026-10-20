/* Context-bound correctness floor for the original MC68000. AOT module misses
 * execute current program bytes through the same scheduler/exception boundary.
 * Bus/address faults and data-dependent cycle qualification remain separate. */
#ifndef SEGACD_M68K_FLOOR_H
#define SEGACD_M68K_FLOOR_H
#include "m68k_step.h"
M68kStepResult segacd_m68k_floor_step(M68kStepContext *);
#endif
