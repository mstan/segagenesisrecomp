/* No game/audio/device: known waiting and CPU stalls qualify the observer. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
volatile uint32_t host_fixture_frame;
volatile uint32_t host_fixture_sink;
__declspec(noinline) void fixture_wait(void) { Sleep(250); ++host_fixture_sink; }
__declspec(noinline) void fixture_busy(void)
{
    LARGE_INTEGER f,start,now; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&start);
    do { QueryPerformanceCounter(&now); } while(now.QuadPart-start.QuadPart<f.QuadPart/4);
}
int main(void)
{
    for(host_fixture_frame=0;host_fixture_frame<150;++host_fixture_frame) {
        if(host_fixture_frame==40) fixture_wait();
        if(host_fixture_frame==80) fixture_busy();
        Sleep(16);
    }
    return 0;
}
