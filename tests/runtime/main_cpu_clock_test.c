#include "../../runner/main_cpu_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    MainCpuClock clock = {0};
    assert(main_cpu_elapsed(&clock, 97, 11, 1) == 97);
    assert(main_cpu_elapsed(&clock, 99, 11, 4) == 33);
    unsigned sum=0;
    for(unsigned n=0;n<400;++n)sum+=main_cpu_elapsed(&clock, 7, 0, 4);
    assert(sum==700 && !clock.remainder);
    assert(main_cpu_elapsed(&clock, 2000, 2000, 4)==2000);
    assert(main_cpu_elapsed(&clock, 3, 0, 4)==0 && clock.remainder==3);
    assert(main_cpu_elapsed(&clock, 5, 0, 1)==5 && !clock.remainder);
    puts("Enhanced main CPU clock: native identity, fraction carry, DMA PASS");
    return 0;
}
