#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include "input_map.h"
#include "runtime_ui_gamepad.h"
#include "recomp_runtime_ui_internal.h"

static SDL_JoystickID owned_id=-1;
int genesis_switch_controller_connected(int id){return id==owned_id;}
static int actions;
static int action(void *context,const RecompRuntimeUiItem *item){(void)context;(void)item;++actions;return 1;}
static SDL_Joystick *joystick;
static void button(SDL_GameControllerButton b,int down){
    assert(!SDL_JoystickSetVirtualButton(joystick,(int)b,(Uint8)down));
    SDL_JoystickUpdate();SDL_GameControllerUpdate();
}
static void axes(int x,int y){
    assert(!SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTX,(Sint16)x));
    assert(!SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,(Sint16)y));
    SDL_JoystickUpdate();SDL_GameControllerUpdate();
}
int main(void){
    assert(!SDL_Init(SDL_INIT_GAMECONTROLLER));
    int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,SDL_CONTROLLER_BUTTON_MAX,0);
    assert(index>=0);owned_id=SDL_JoystickGetDeviceInstanceID(index);
    joystick=SDL_JoystickOpen(index);assert(joystick);
    input_map_init_defaults();gamepad_init_players(1);assert(gamepad_player_connected(0));
    static const RecompRuntimeUiItem items[]={
        {"one","One","One","",RECOMP_RUNTIME_UI_ACTION},
        {"two","Two","Two","",RECOMP_RUNTIME_UI_ACTION}
    };
    RecompRuntimeUiConfig config={0};config.title="Controller fixture";config.items=items;config.item_count=2;
    config.callbacks.run_action=action;
    RecompRuntimeUi *ui=recomp_runtime_ui_create(&config);assert(ui);
    RuntimeUiPad pad={0};recomp_runtime_ui_open(ui);
#define POLL(t) runtime_ui_gamepad_poll(ui,&pad,gamepad_overlay_mask(),t,1)
    axes(2000,2000);assert(!(gamepad_overlay_mask()&(GP_OVERLAY_UP|GP_OVERLAY_DOWN|GP_OVERLAY_LEFT|GP_OVERLAY_RIGHT)));
    axes(0,20000);POLL(0);assert(ui->section_index==1);
    POLL(100);assert(ui->section_index==1);POLL(280);assert(ui->section_index==0);
    POLL(360);assert(ui->section_index==1);axes(0,0);POLL(370);
    /* SDL's right button is Nintendo A, bottom button is Nintendo B. */
    button(SDL_CONTROLLER_BUTTON_B,1);POLL(400);assert(ui->in_section);
    POLL(900);assert(!actions);button(SDL_CONTROLLER_BUTTON_B,0);POLL(910);
    button(SDL_CONTROLLER_BUTTON_B,1);POLL(920);assert(actions==1);POLL(1500);assert(actions==1);
    button(SDL_CONTROLLER_BUTTON_B,0);POLL(1510);
    button(SDL_CONTROLLER_BUTTON_A,1);POLL(1520);assert(!ui->in_section&&ui->open);
    POLL(2000);assert(ui->open);button(SDL_CONTROLLER_BUTTON_A,0);POLL(2010);
    button(SDL_CONTROLLER_BUTTON_A,1);POLL(2020);assert(!ui->open);
    button(SDL_CONTROLLER_BUTTON_A,0);POLL(2030);recomp_runtime_ui_open(ui);
    button(SDL_CONTROLLER_BUTTON_DPAD_UP,1);POLL(2040);assert(ui->section_index==0);
    button(SDL_CONTROLLER_BUTTON_DPAD_UP,0);POLL(2050);
    axes(20000,12000);assert((gamepad_overlay_mask()&(GP_OVERLAY_RIGHT|GP_OVERLAY_DOWN))==GP_OVERLAY_RIGHT);
    recomp_runtime_ui_destroy(ui);gamepad_shutdown();SDL_JoystickClose(joystick);
    assert(!SDL_JoystickDetachVirtual(index));SDL_Quit();
    puts("PASS real controller polling, stick/deadzone, D-pad, repeat, Nintendo A/B and action release gate");
    return 0;
}
