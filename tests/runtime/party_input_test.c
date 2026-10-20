#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "input_map.h"
#include "gamepad.h"
#include "app_config.h"
#include "consoles/genesis/genesis_binds.h"
#ifndef RUI_GEN_MAX_PLAYERS
/* Older supported launcher bridges expose two bind pages. The engine's
 * six-pad assignment/mask contract is still exercised independently below. */
#define RUI_GEN_MAX_PLAYERS 2
#endif
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#c,SDL_GetError()); exit(1); } } while (0)
static void events(void) { SDL_Event e; while (SDL_PollEvent(&e)) gamepad_handle_event(&e); }
int main(int argc,char **argv)
{
    CHECK(argc == 2);
    const char *path = argv[1];
    input_map_init_defaults(); app_config_defaults();
    CHECK(g_input_map.p[0].device==INPUT_DEV_BOTH && g_input_map.p[1].device==INPUT_DEV_NONE);
    CHECK(g_input_map.p[2].device==INPUT_DEV_GAMEPAD && g_input_map.p[3].device==INPUT_DEV_GAMEPAD);
    CHECK(app_config_save(path));
    rui_genesis_binds_init(path);
    const int ui_players=RUI_GEN_MAX_PLAYERS<INPUT_MAX_PLAYERS?RUI_GEN_MAX_PLAYERS:INPUT_MAX_PLAYERS;
    CHECK(ui_players>=2);
    for (int p=0;p<ui_players;++p) for (int b=0;b<GB_COUNT;++b) {
        int kind,code,dir;
        CHECK(rui_genesis_binds_get_key(path,p,b)==g_input_map.p[p].key[b]);
        rui_genesis_binds_get_pad(path,p,b,&kind,&code,&dir);
        CHECK(kind==g_input_map.p[p].pad[b].kind && code==g_input_map.p[p].pad[b].code);
    }
    const int ui_a=ui_players-2,ui_b=ui_players-1;
    rui_genesis_binds_set_key(path,ui_a,GB_B,SDL_SCANCODE_Q);
    rui_genesis_binds_set_key(path,ui_b,GB_B,SDL_SCANCODE_W);
    rui_genesis_binds_set_pad(path,ui_b,GB_A,GP_BIND_AXIS,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,1);
    CHECK(app_config_load(path));
    CHECK(g_input_map.p[ui_a].key[GB_B]==SDL_SCANCODE_Q && g_input_map.p[ui_b].key[GB_B]==SDL_SCANCODE_W);
    CHECK(g_input_map.p[ui_b].pad[GB_A].kind==GP_BIND_AXIS);
    /* Keep attached owner hardware out of this synthetic six-pad fixture.
     * SDL_JoystickAttachVirtual uses VID/PID 0000/0000. Test-local only. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x0000/0x0000");
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)==0);
    /* Virtual pads exercise the real SDL assignment and disconnect paths. */
    SDL_Joystick *joystick[INPUT_MAX_PLAYERS]; int device[INPUT_MAX_PLAYERS];
    for (int p=0;p<INPUT_MAX_PLAYERS;++p) {
        device[p]=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
        CHECK(device[p]>=0);
        joystick[p]=SDL_JoystickOpen(device[p]); CHECK(joystick[p]);
    }
    gamepad_init(); events();
    CHECK(gamepad_player_connected(0) && gamepad_player_connected(1));
    CHECK(!gamepad_player_connected(2) && !gamepad_player_connected(3));
    gamepad_shutdown();
    gamepad_init_players(0); events();
    CHECK(gamepad_player_connected(0) && gamepad_player_connected(1));
    CHECK(!gamepad_player_connected(2) && !gamepad_player_connected(3));
    gamepad_shutdown();
    gamepad_init_players(INPUT_MAX_PLAYERS); events();
    for (int p=0;p<INPUT_MAX_PLAYERS;++p) CHECK(gamepad_player_connected(p));
    CHECK(!gamepad_player_connected(INPUT_MAX_PLAYERS) && !gamepad_player_connected(-1));
    CHECK(SDL_JoystickSetVirtualButton(joystick[2],SDL_CONTROLLER_BUTTON_A,1)==0);
    CHECK(SDL_JoystickSetVirtualButton(joystick[3],SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1)==0);
    SDL_JoystickUpdate(); events();
    CHECK(input_current_mask(2)==input_button_bit(GB_B));
    CHECK(input_current_mask(3)==input_button_bit(GB_RIGHT));
    CHECK(!input_current_mask(0) && !input_current_mask(1));
    CHECK(SDL_JoystickSetVirtualButton(joystick[4],SDL_CONTROLLER_BUTTON_B,1)==0);
    CHECK(SDL_JoystickSetVirtualButton(joystick[5],SDL_CONTROLLER_BUTTON_X,1)==0);
    SDL_JoystickUpdate(); events();
    CHECK(input_current_mask(4)==input_button_bit(GB_A));
    CHECK(input_current_mask(5)==input_button_bit(GB_C));
    CHECK(input_player_connected(2) && input_player_connected(3));
    SDL_JoystickClose(joystick[2]);
    CHECK(SDL_JoystickDetachVirtual(device[2])==0); events();
    CHECK(!input_player_connected(2) && input_player_connected(3));
    CHECK(!input_current_mask(2) && input_current_mask(3)==input_button_bit(GB_RIGHT));
    gamepad_shutdown();
    for (int p=INPUT_MAX_PLAYERS-1;p>=0;--p) if (p!=2) {
        int index=-1;
        SDL_JoystickID id=SDL_JoystickInstanceID(joystick[p]);
        for (int d=0;d<SDL_NumJoysticks();++d) if (SDL_JoystickGetDeviceInstanceID(d)==id) index=d;
        SDL_JoystickClose(joystick[p]);
        if (index>=0) CHECK(SDL_JoystickDetachVirtual(index)==0);
    }
    SDL_Quit(); remove(path);
    puts("party_input: default two-pad limit, opt-in six-device masks, disconnect isolation, UI/engine settings round-trip OK");
    return 0;
}
