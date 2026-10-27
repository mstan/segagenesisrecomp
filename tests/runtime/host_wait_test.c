#include "host_wait.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>

int main(void)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER) != 0) return 1;
    uint64_t frequency = SDL_GetPerformanceFrequency();
    if (!frequency) return 1;
    for (int i=0;i<32;++i) {
        uint64_t start = SDL_GetPerformanceCounter();
        uint64_t target = start + frequency/1000;
        genesis_host_wait_until(target,frequency);
        uint64_t end = SDL_GetPerformanceCounter();
        if (end < target || end-start > frequency*2) return 1;
    }
    genesis_host_wait_until(SDL_GetPerformanceCounter(),frequency);
    genesis_host_wait_until(SDL_GetPerformanceCounter()+1,0);
    printf("host wait: deadlines honored, native=%d\n",genesis_host_wait_native_active());
    genesis_host_wait_shutdown();
    genesis_host_wait_shutdown();
    if(genesis_host_wait_native_active())return 1;
    SDL_Quit();
    return 0;
}
