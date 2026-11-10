#define SDL_MAIN_HANDLED 1
#include <SDL2/SDL.h>
#include "gamepad.h"
#include "input_map.h"
#include "load_hold.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#c,SDL_GetError());exit(1);}}while(0)
static void events(void){SDL_Event e;SDL_JoystickUpdate();while(SDL_PollEvent(&e))gamepad_handle_event(&e);}
int main(void)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x0000/0x0000");
    CHECK(!SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS));
    input_map_init_defaults();
    int p1=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
    int p2=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
    CHECK(p1>=0&&p2>=0);
    SDL_Joystick *a=SDL_JoystickOpen(p1),*b=SDL_JoystickOpen(p2);CHECK(a&&b);
    gamepad_init();events();
    CHECK(!SDL_JoystickSetVirtualButton(b,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(!gamepad_consume_quickload()&&!gamepad_quickload_held());
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(gamepad_consume_quickload()==1&&gamepad_quickload_held());
    CHECK(!gamepad_consume_quickload());
    GenesisLoadHold hold={0};genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,100);
    CHECK(!genesis_load_hold_update(&hold,1599,gamepad_quickload_held(),1));
    CHECK(genesis_load_hold_update(&hold,1600,gamepad_quickload_held(),1)==1);
    CHECK(!genesis_load_hold_update(&hold,9000,gamepad_quickload_held(),1));
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_BACK,1));events();
    CHECK(!gamepad_quickload_held());
    CHECK(!genesis_load_hold_update(&hold,9001,gamepad_quickload_held(),1));
    CHECK(!hold.source);
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0));
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_BACK,0));events();
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(gamepad_consume_quickload()==1);
    genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,10000);
    g_input_map.p[0].pad_type=PAD_6BUTTON;
    CHECK(!gamepad_quickload_held());
    CHECK(!genesis_load_hold_update(&hold,11500,gamepad_quickload_held(),1));
    g_input_map.p[0].pad_type=PAD_3BUTTON;
    CHECK(!gamepad_consume_quickload()); /* Mode change cannot create a new press. */
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,1));events();
    CHECK(gamepad_consume_quicksave()==1&&!gamepad_consume_quicksave());
    genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,12000);
    CHECK(!SDL_JoystickDetachVirtual(p1));events();
    CHECK(!gamepad_quickload_held());
    CHECK(!genesis_load_hold_update(&hold,13500,gamepad_quickload_held(),1));
    SDL_JoystickClose(a);SDL_JoystickClose(b);gamepad_shutdown();SDL_Quit();
    puts("PASS real SDL virtual P1/P2, 1500ms one-shot, Back chord, six-button mode, disconnect and quicksave");
    return 0;
}
