/* Native hidden-window dispatch loop: no desktop capture, cursor or key input.
 * Real move/resize integration is qualified separately with the game runner. */
#define SDL_MAIN_HANDLED 1
#include <stdio.h>
#include "host_window_events.h"
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line%d: %s (%s)\n",__LINE__,#c,SDL_GetError());return 1;} }while(0)
#ifdef _WIN32
static WNDPROC original;
static HWND handle;
static int in_loop;
static LRESULT CALLBACK modal_loop(HWND w,UINT message,WPARAM a,LPARAM b)
{
    if(message==WM_APP+1) {
        in_loop=1;SendMessageW(w,WM_ENTERSIZEMOVE,0,0);
        MSG msg;
        while(in_loop && GetMessageW(&msg,NULL,0,0)>0) {
            if(msg.message==WM_APP+2)break;
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        SendMessageW(w,WM_EXITSIZEMOVE,0,0);in_loop=0;return 0;
    }
    if(message==WM_CANCELMODE)in_loop=0;
    return CallWindowProcW(original,w,message,a,b);
}
static DWORD WINAPI post_events(void *unused)
{
    (void)unused;Sleep(10);PostMessageW(handle,WM_APP+1,0,0);
    for(unsigned i=0;i<10;i++) {
        Sleep(10);SDL_Event e;SDL_zero(e);e.type=SDL_USEREVENT;e.user.code=42;SDL_PushEvent(&e);
    }
    PostMessageW(handle,WM_APP+2,0,0);return 0;
}
#endif
int main(void)
{
    SDL_SetMainReady();
#ifndef _WIN32
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
#endif
    CHECK(!SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window=SDL_CreateWindow("Window event test",0,0,64,64,SDL_WINDOW_HIDDEN);
    CHECK(window);
#ifdef _WIN32
    CHECK(fiber_convert_thread());SDL_SysWMinfo info;SDL_VERSION(&info.version);
    CHECK(SDL_GetWindowWMInfo(window,&info));handle=info.info.win.window;
    original=(WNDPROC)SetWindowLongPtrW(handle,GWLP_WNDPROC,(LONG_PTR)modal_loop);CHECK(original);
    CHECK(window_pump_init(window));
    HANDLE worker=CreateThread(NULL,0,post_events,NULL,0,NULL);CHECK(worker);
    unsigned modal_ticks=0,received=0;Uint32 start=SDL_GetTicks();
    while(SDL_GetTicks()-start<220) {
        SDL_Event event;while(window_poll_event(&event))if(event.type==SDL_USEREVENT && event.user.code==42)received++;
        if(window_pump.modal)modal_ticks++;
        SDL_Delay(1);
    }
    CHECK(WaitForSingleObject(worker,1000)==WAIT_OBJECT_0);CloseHandle(worker);
    CHECK(modal_ticks>=3 && received==10 && window_pump.yields>=3);
    PostMessageW(handle,WM_APP+1,0,0);start=SDL_GetTicks();
    while(!window_pump.modal && SDL_GetTicks()-start<200) {
        SDL_Event event;while(window_poll_event(&event)){}
        SDL_Delay(1);
    }
    CHECK(window_pump.modal); /* Shutdown must unwind an active callback. */
    CHECK(window_pump_shutdown());CHECK(!in_loop);
    SetWindowLongPtrW(handle,GWLP_WNDPROC,(LONG_PTR)original);
#else
    CHECK(window_pump_init(window));
    SDL_Event e;SDL_zero(e);e.type=SDL_USEREVENT;e.user.code=42;CHECK(SDL_PushEvent(&e)==1);
    int received=0;while(window_poll_event(&e))if(e.type==SDL_USEREVENT && e.user.code==42)received++;
    CHECK(received==1 && window_pump_shutdown());
#endif
    SDL_Event final_event;CHECK(!window_wait_event_timeout(&final_event,2));
#ifdef _WIN32
    fiber_revert_thread();
#endif
    SDL_DestroyWindow(window);SDL_Quit();
    puts("Window events: modal frame progress, input queue and clean shutdown PASS");return 0;
}
