#include "host_wait.h"
#include <SDL2/SDL.h>

/* The optional high-resolution timer is a Windows desktop facility. Other
 * targets, disabled builds and systems rejecting it retain the existing SDL
 * sleep/spin implementation. No machine-wide timer resolution is changed. */
#if defined(_WIN32) && !defined(_XBOX) && !defined(NXDK) && !defined(GENESIS_DISABLE_NATIVE_HOST_WAIT)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static HANDLE s_timer;
static int s_timer_attempted;

static int wait_native(uint64_t remaining, uint64_t frequency)
{
    if (!s_timer_attempted) {
        s_timer_attempted = 1;
        s_timer = CreateWaitableTimerExW(NULL, NULL,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    if (!s_timer) return 0;
    /* Leave 100 us for the final deadline check. The frame pacer bounds this
     * interval to one NTSC period, so conversion cannot overflow uint64_t. */
    uint64_t units = remaining * 10000000ull / frequency;
    if (units > 2000) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(units - 1000);
        if (!SetWaitableTimer(s_timer, &due, 0, NULL, NULL, FALSE) ||
            WaitForSingleObject(s_timer, INFINITE) != WAIT_OBJECT_0) return 0;
    }
    return 1;
}
int genesis_host_wait_native_active(void) { return s_timer != NULL; }
void genesis_host_wait_shutdown(void)
{
    if (s_timer) CloseHandle(s_timer);
    s_timer = NULL;
    s_timer_attempted = 0;
}
#else
static int wait_native(uint64_t remaining, uint64_t frequency)
{ (void)remaining; (void)frequency; return 0; }
int genesis_host_wait_native_active(void) { return 0; }
void genesis_host_wait_shutdown(void) { }
#endif

void genesis_host_wait_until(uint64_t deadline, uint64_t frequency)
{
    uint64_t now = SDL_GetPerformanceCounter();
    if (!frequency || now >= deadline) return;
    if (!wait_native(deadline - now, frequency)) {
        uint64_t remaining_ms = (deadline - now) * 1000 / frequency;
        if (remaining_ms > 2) SDL_Delay((Uint32)(remaining_ms - 1));
    }
    while (SDL_GetPerformanceCounter() < deadline) { }
}
