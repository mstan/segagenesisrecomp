/* Keep the frame loop on its normal scheduler stack while Windows owns a
 * modal move/resize or caption-button loop. Only event dispatch runs on a
 * second same-thread fiber; a native window timer yields at frame boundaries. No renderer
 * or game work runs from a window callback or a second operating-system
 * thread. Other platforms and unavailable window backends use SDL directly.
 * The runner initializes after glue and shuts down before glue/window teardown.
 */
#ifndef GENESIS_HOST_WINDOW_EVENTS_H
#define GENESIS_HOST_WINDOW_EVENTS_H
#include <SDL2/SDL.h>
#if defined(_WIN32)
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#include "fiber_compat.h"
static struct {
    HWND window;WNDPROC original;
    fiber_t caller,pump;
    SDL_Event event;
    int result,modal,resume;
    unsigned yields;
} window_pump;
static LRESULT CALLBACK window_pump_proc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    /* Caption tracking need not send WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE.
     * Cover the forwarded press itself, retaining any enclosing modal loop. */
    int caption=message==WM_NCLBUTTONDOWN &&
                (w==HTMINBUTTON || w==HTMAXBUTTON || w==HTCLOSE);
    int enclosing_modal=window_pump.modal;
    if(caption && !enclosing_modal) {
        window_pump.modal=1;
        SetTimer(window,(UINT_PTR)&window_pump,10,NULL);
    }
    if(message==WM_ENTERSIZEMOVE) {
        window_pump.modal=1;
        SetTimer(window,(UINT_PTR)&window_pump,10,NULL);
    } else if(message==WM_EXITSIZEMOVE) {
        KillTimer(window,(UINT_PTR)&window_pump);
        window_pump.modal=0;
    } else if(message==WM_TIMER && w==(UINT_PTR)&window_pump &&
              window_pump.modal && fiber_current()==window_pump.pump) {
        ++window_pump.yields;
        fiber_switch(window_pump.caller);
    }
    LRESULT result=CallWindowProcW(window_pump.original,window,message,w,l);
    if(caption && !enclosing_modal) {
        KillTimer(window,(UINT_PTR)&window_pump);
        window_pump.modal=0;
    }
    return result;
}
static void window_pump_entry(void *unused)
{
    (void)unused;
    for(;;) {
        window_pump.result=SDL_PollEvent(&window_pump.event);
        fiber_switch(window_pump.caller);
    }
}
static int window_pump_init(SDL_Window *window)
{
    if(window_pump.pump)return 1;
    SDL_SysWMinfo info;SDL_VERSION(&info.version);
    if(!fiber_current() || !SDL_GetWindowWMInfo(window,&info) || info.subsystem!=SDL_SYSWM_WINDOWS)return 0;
    window_pump.caller=fiber_current();
    window_pump.pump=fiber_create(4u*1024u*1024u,4u*1024u*1024u,window_pump_entry,NULL);
    if(!window_pump.pump)return 0;
    window_pump.window=info.info.win.window;
    window_pump.original=(WNDPROC)GetWindowLongPtrW(window_pump.window,GWLP_WNDPROC);
    if(!window_pump.original) {
        fiber_destroy(window_pump.pump);memset(&window_pump,0,sizeof window_pump);return 0;
    }
    SetLastError(0);
    if(!SetWindowLongPtrW(window_pump.window,GWLP_WNDPROC,(LONG_PTR)window_pump_proc) && GetLastError()) {
        fiber_destroy(window_pump.pump);memset(&window_pump,0,sizeof window_pump);return 0;
    }
    window_pump.resume=1;return 1;
}
static int window_poll_event(SDL_Event *event)
{
    if(!window_pump.pump)return SDL_PollEvent(event);
    /* After one modal yield, drain only queued events for this frame. A
     * zero return closes the caller's drain loop and enables the next native
     * dispatch on its next iteration, preventing continuous motion events
     * from starving emulation. All runner callers supply an event buffer. */
    if(!window_pump.modal || window_pump.resume) {
        window_pump.resume=0;
        fiber_switch(window_pump.pump);
    }
    int result;
    if(window_pump.modal) {
        do {
            result=SDL_PeepEvents(event,1,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT);
        } while(result>0 && event->type==SDL_POLLSENTINEL);
    } else {
        result=window_pump.result;
        if(result && event)*event=window_pump.event;
    }
    if(result<=0)window_pump.resume=1;
    return result>0;
}
static int window_pump_shutdown(void)
{
    if(!window_pump.pump)return 1;
    if(window_pump.modal) {
        SendMessageW(window_pump.window,WM_CANCELMODE,0,0);
        PostMessageW(window_pump.window,WM_LBUTTONUP,0,0);
        PostMessageW(window_pump.window,WM_KEYDOWN,VK_ESCAPE,0);
        if(GetCapture()==window_pump.window)ReleaseCapture();
        for(unsigned i=0;i<8 && window_pump.modal;i++)fiber_switch(window_pump.pump);
        if(window_pump.modal)return 0;
    }
    KillTimer(window_pump.window,(UINT_PTR)&window_pump);
    SetWindowLongPtrW(window_pump.window,GWLP_WNDPROC,(LONG_PTR)window_pump.original);
    fiber_destroy(window_pump.pump);memset(&window_pump,0,sizeof window_pump);return 1;
}
static int window_wait_event_timeout(SDL_Event *event,int timeout)
{
    if(!window_pump.pump)return SDL_WaitEventTimeout(event,timeout);
    Uint32 start=SDL_GetTicks();
    do {
        if(window_poll_event(event))return 1;
        if(timeout>=0 && SDL_GetTicks()-start>=(Uint32)timeout)return 0;
        SDL_Delay(1);
    } while(1);
}
#else
static int window_pump_init(SDL_Window *window) { (void)window;return 1; }
static int window_poll_event(SDL_Event *event) { return SDL_PollEvent(event); }
static int window_wait_event_timeout(SDL_Event *event,int timeout)
{ return SDL_WaitEventTimeout(event,timeout); }
static int window_pump_shutdown(void) { return 1; }
#endif
#endif
