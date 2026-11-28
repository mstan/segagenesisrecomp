/* SDL already owns Switch video, controllers and audio. Keep GL resolution
 * at the context creation seam shared by the launcher and runtime overlay. */
#include <switch.h>
#include <SDL2/SDL.h>
#include <glad/glad.h>

/* SDL2 2.28.5's Switch backend registers touch 0 but reports touch 1.
 * Register its reported device and enable mouse events for Dear ImGui. */
extern int SDL_AddTouch(SDL_TouchID id, SDL_TouchDeviceType type, const char *name);

int genesis_switch_controller_connected(int slot)
{
    PadState pad;
    if(slot<0 || slot>=8)return 0;
    if(slot==0)padInitializeDefault(&pad);
    else padInitialize(&pad,(HidNpadIdType)(HidNpadIdType_No1+slot));
    padUpdate(&pad);
    return padIsConnected(&pad);
}

SDL_GLContext __real_SDL_GL_CreateContext(SDL_Window *window);
SDL_GLContext __wrap_SDL_GL_CreateContext(SDL_Window *window)
{
    SDL_AddTouch(1,SDL_TOUCH_DEVICE_DIRECT,"Switch touchscreen");
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS,"1",SDL_HINT_OVERRIDE);
    /* The SDK suppresses touch and gamepad polling while text input is on.
     * Start the normal menu outside the software-keyboard input mode. */
    SDL_StopTextInput();
    SDL_GLContext context = __real_SDL_GL_CreateContext(window);
    if (context && !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(context);
        SDL_SetError("Switch OpenGL entry point loading failed");
        return NULL;
    }
    return context;
}
