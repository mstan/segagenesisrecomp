#pragma once
#include <stdint.h>

/* Optional enhanced main-program clock. Raster, IRQ handlers, DMA stalls and
 * the Z80 keep their native clocks. Carry fractional CPU cycles across bus
 * accesses: rounding each instruction independently would distort the rate. */
typedef struct { uint32_t remainder; } MainCpuClock;
static inline uint32_t main_cpu_elapsed(MainCpuClock *clock, uint32_t elapsed,
                                       uint32_t stalls, unsigned divisor)
{
    if (divisor <= 1) { clock->remainder = 0; return elapsed; }
    if (stalls > elapsed) stalls = elapsed;
    uint64_t cpu = (uint64_t)(elapsed - stalls) + clock->remainder;
    clock->remainder = (uint32_t)(cpu % divisor);
    return stalls + (uint32_t)(cpu / divisor);
}
