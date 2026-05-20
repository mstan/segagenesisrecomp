/*
 * gamepad.c — SDL_GameController integration for the shared runner.
 *
 * See gamepad.h for the public API + mapping rationale. We use
 * SDL_GameController so a single code path covers XInput (Xbox pads on
 * Windows), HID (PS / Switch Pro), and SDL's controller database.
 *
 * Held state (face buttons, D-pad, sticks) is read on demand via
 * SDL_GameControllerGet*; only the edge-triggered shoulder taps for
 * quicksave/quickload are latched via event-driven flags.
 */

#include "gamepad.h"

#include <SDL2/SDL.h>
#include <stdio.h>

/* Open at most one controller — P1. Future 2P support can index this. */
static SDL_GameController *s_pad      = NULL;
static SDL_JoystickID      s_pad_jid  = -1;

/* Edge-triggered shoulder latches (consumed by main loop once per press). */
static int s_pending_save = 0;
static int s_pending_load = 0;

/* Analog deadzone for the left stick. SDL axis values are int16
 * (-32768..32767). 8000 is roughly 25%, the common emulator default. */
#define GAMEPAD_STICK_DEADZONE 8000

static void open_pad_index(int joystick_index)
{
    if (s_pad) return;                       /* already have one */
    if (!SDL_IsGameController(joystick_index)) return;

    SDL_GameController *c = SDL_GameControllerOpen(joystick_index);
    if (!c) {
        fprintf(stderr, "[gamepad] SDL_GameControllerOpen(%d): %s\n",
                joystick_index, SDL_GetError());
        return;
    }
    s_pad     = c;
    s_pad_jid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
    fprintf(stderr, "[gamepad] opened: %s\n", SDL_GameControllerName(c));
}

static void close_pad_by_jid(SDL_JoystickID jid)
{
    if (!s_pad || jid != s_pad_jid) return;
    fprintf(stderr, "[gamepad] removed: %s\n",
            SDL_GameControllerName(s_pad));
    SDL_GameControllerClose(s_pad);
    s_pad     = NULL;
    s_pad_jid = -1;
}

void gamepad_init(void)
{
    /* Walk the already-attached joysticks so a pad plugged in before
     * the window opened still works without waiting for a re-plug. */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && !s_pad; i++)
        open_pad_index(i);
}

void gamepad_shutdown(void)
{
    if (s_pad) {
        SDL_GameControllerClose(s_pad);
        s_pad = NULL;
    }
    s_pad_jid = -1;
    s_pending_save = 0;
    s_pending_load = 0;
}

void gamepad_handle_event(const SDL_Event *ev)
{
    if (!ev) return;
    switch (ev->type) {
        case SDL_CONTROLLERDEVICEADDED:
            /* cdevice.which is a joystick index here (not an instance id). */
            open_pad_index(ev->cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            /* cdevice.which is an instance id on REMOVED. */
            close_pad_by_jid((SDL_JoystickID)ev->cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            /* Edge-trigger shoulders so a single press fires once even
             * though the frame loop polls every tick. */
            if (s_pad && ev->cbutton.which == s_pad_jid) {
                if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                    s_pending_save = 1;
                else if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                    s_pending_load = 1;
            }
            break;
        default:
            break;
    }
}

/* Held-state helpers. Returning cc_false when no pad is attached makes
 * the OR-with-keyboard fallback in main.c a no-op for keyboard-only users. */

static int btn_held(SDL_GameControllerButton b)
{
    return s_pad ? SDL_GameControllerGetButton(s_pad, b) : 0;
}

static int axis_neg(SDL_GameControllerAxis a)
{
    if (!s_pad) return 0;
    return SDL_GameControllerGetAxis(s_pad, a) < -GAMEPAD_STICK_DEADZONE;
}

static int axis_pos(SDL_GameControllerAxis a)
{
    if (!s_pad) return 0;
    return SDL_GameControllerGetAxis(s_pad, a) >  GAMEPAD_STICK_DEADZONE;
}

cc_bool gamepad_button_pressed(ClownMDEmu_Button btn)
{
    if (!s_pad) return cc_false;

    switch (btn) {
        case CLOWNMDEMU_BUTTON_UP:
            return (btn_held(SDL_CONTROLLER_BUTTON_DPAD_UP)
                 || axis_neg(SDL_CONTROLLER_AXIS_LEFTY)) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_DOWN:
            return (btn_held(SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                 || axis_pos(SDL_CONTROLLER_AXIS_LEFTY)) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_LEFT:
            return (btn_held(SDL_CONTROLLER_BUTTON_DPAD_LEFT)
                 || axis_neg(SDL_CONTROLLER_AXIS_LEFTX)) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_RIGHT:
            return (btn_held(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
                 || axis_pos(SDL_CONTROLLER_AXIS_LEFTX)) ? cc_true : cc_false;
        /* Xbox A (south) and Y (north) both map to Genesis A so any face
         * button jumps comfortably; B → B, X → C. */
        case CLOWNMDEMU_BUTTON_A:
            return (btn_held(SDL_CONTROLLER_BUTTON_A)
                 || btn_held(SDL_CONTROLLER_BUTTON_Y)) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_B:
            return btn_held(SDL_CONTROLLER_BUTTON_B) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_C:
            return btn_held(SDL_CONTROLLER_BUTTON_X) ? cc_true : cc_false;
        case CLOWNMDEMU_BUTTON_START:
            return btn_held(SDL_CONTROLLER_BUTTON_START) ? cc_true : cc_false;
        default:
            return cc_false;
    }
}

int gamepad_turbo_held(void)
{
    return btn_held(SDL_CONTROLLER_BUTTON_BACK);
}

int gamepad_consume_quicksave(void)
{
    if (!s_pending_save) return 0;
    s_pending_save = 0;
    return 1;   /* slot 1 */
}

int gamepad_consume_quickload(void)
{
    if (!s_pending_load) return 0;
    s_pending_load = 0;
    return 1;   /* slot 1 */
}
