#define SDL_MAIN_HANDLED 1
#include <SDL2/SDL.h>
#include "gamepad.h"
#include "input_map.h"
#include "load_hold.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#c,SDL_GetError());exit(1);}}while(0)
static GenesisLoadHold hold;
static void events(void)
{
    SDL_Event e;SDL_JoystickUpdate();
    while(SDL_PollEvent(&e)){
        if(gamepad_cancels_quickload(&e))genesis_load_hold_cancel(&hold);
        gamepad_handle_event(&e);
    }
}
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
    genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,100);
    CHECK(!genesis_load_hold_update(&hold,1599,gamepad_quickload_held(),1));
    /* Both edges arrive in one event batch. Polling just the final button
     * state would incorrectly finish the previous press at time 1600. */
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0));SDL_JoystickUpdate();
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(!hold.source&&gamepad_consume_quickload()==1&&gamepad_quickload_held());
    genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,1600);
    CHECK(!genesis_load_hold_update(&hold,1600,gamepad_quickload_held(),1));
    CHECK(!genesis_load_hold_update(&hold,3099,gamepad_quickload_held(),1));
    CHECK(genesis_load_hold_update(&hold,3100,gamepad_quickload_held(),1)==1);
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
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,0));
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0));events();
    g_input_map.shortcuts.enabled=0;
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(!gamepad_consume_quickload()&&!gamepad_quickload_held());
    g_input_map.shortcuts.enabled=1;g_input_map.shortcuts.load_button=INPUT_SHORTCUT_NONE;
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0));events();
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1));events();
    CHECK(!gamepad_consume_quickload()&&!gamepad_quickload_held());
    g_input_map.shortcuts.load_button=SDL_CONTROLLER_BUTTON_RIGHTSTICK;g_input_map.p[0].pad_type=PAD_6BUTTON;
    CHECK(!SDL_JoystickSetVirtualButton(b,SDL_CONTROLLER_BUTTON_RIGHTSTICK,1));events();
    CHECK(!gamepad_consume_quickload());
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSTICK,1));events();
    CHECK(gamepad_consume_quickload()&&gamepad_quickload_held());
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_RIGHTSTICK,0));events();
    CHECK(!gamepad_quickload_held());
    g_input_map.shortcuts.load_button=SDL_CONTROLLER_BUTTON_A;
    g_input_map.p[0].pad[GB_A]=(GamepadBind){GP_BIND_BUTTON,SDL_CONTROLLER_BUTTON_A,0};
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_A,1));events();
    CHECK(gamepad_consume_quickload()&&!(gamepad_player_mask(0)&input_button_bit(GB_A)));
    g_input_map.shortcuts.enabled=0;
    CHECK(gamepad_player_mask(0)&input_button_bit(GB_A));
    g_input_map.shortcuts.enabled=1;g_input_map.shortcuts.rewind_button=SDL_CONTROLLER_BUTTON_LEFTSTICK;
    CHECK(!SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_LEFTSTICK,1));events();
    CHECK(gamepad_consume_rewind()&&!gamepad_consume_rewind());
    genesis_load_hold_begin(&hold,GENESIS_LOAD_HOLD_PAD,1,12000);
    CHECK(!SDL_JoystickDetachVirtual(p1));events();
    CHECK(!gamepad_quickload_held());
    CHECK(!genesis_load_hold_update(&hold,13500,gamepad_quickload_held(),1));
    SDL_JoystickClose(a);SDL_JoystickClose(b);gamepad_shutdown();SDL_Quit();
    puts("PASS real SDL virtual P1/P2, 1500ms one-shot, Back chord, six-button mode, disconnect and quicksave");
    return 0;
}
