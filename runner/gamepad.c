/*
 * gamepad.c — SDL_GameController integration for the shared runner.
 *
 * See gamepad.h for the public API. We use SDL_GameController so a single code
 * path covers XInput (Xbox pads on Windows), HID (PS / Switch Pro), and SDL's
 * controller database.
 *
 * Up to two controllers are opened and assigned to player 0 / player 1 in plug
 * order. Each player's button mask is resolved through g_input_map's rebindable
 * per-player bindings (set in the launcher), plus an always-on left-analog-stick
 * -> d-pad convenience. The quicksave / quickload / turbo shortcuts live on
 * player 0's shoulder / Back buttons, but only while player 0 is a 3-button pad
 * (6-button mode hands those buttons to Y / Z / Mode).
 */

#include "gamepad.h"
#include "input_map.h"

#include <SDL2/SDL.h>
#include <stdio.h>

/* Up to two controllers, indexed by player. */
static SDL_GameController *s_pad[2]     = { NULL, NULL };
static SDL_JoystickID      s_pad_jid[2] = { -1, -1 };

/* Edge-triggered shoulder latches (consumed by main loop once per press). */
static int s_pending_save = 0;
static int s_pending_load = 0;

static void open_pad_index(int joystick_index)
{
    if (!SDL_IsGameController(joystick_index)) return;

    /* Find a free player slot (0 then 1). */
    int slot = -1;
    for (int i = 0; i < 2; i++) if (!s_pad[i]) { slot = i; break; }
    if (slot < 0) return;                    /* both players already have a pad */

    SDL_GameController *c = SDL_GameControllerOpen(joystick_index);
    if (!c) {
        fprintf(stderr, "[gamepad] SDL_GameControllerOpen(%d): %s\n",
                joystick_index, SDL_GetError());
        return;
    }
    /* Don't open the same physical device twice (event + initial scan race). */
    SDL_JoystickID jid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
    for (int i = 0; i < 2; i++) {
        if (s_pad[i] && s_pad_jid[i] == jid) { SDL_GameControllerClose(c); return; }
    }
    s_pad[slot]     = c;
    s_pad_jid[slot] = jid;
    fprintf(stderr, "[gamepad] P%d: %s\n", slot + 1, SDL_GameControllerName(c));
}

static void close_pad_by_jid(SDL_JoystickID jid)
{
    for (int i = 0; i < 2; i++) {
        if (s_pad[i] && s_pad_jid[i] == jid) {
            fprintf(stderr, "[gamepad] P%d removed: %s\n", i + 1,
                    SDL_GameControllerName(s_pad[i]));
            SDL_GameControllerClose(s_pad[i]);
            s_pad[i]     = NULL;
            s_pad_jid[i] = -1;
        }
    }
}

void gamepad_init(void)
{
    /* Walk the already-attached joysticks so pads plugged in before the window
     * opened still work without waiting for a re-plug. */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) open_pad_index(i);
}

void gamepad_shutdown(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_pad[i]) SDL_GameControllerClose(s_pad[i]);
        s_pad[i]     = NULL;
        s_pad_jid[i] = -1;
    }
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
            /* Quicksave / quickload taps from player 1's shoulders — only when
             * P1 is a 3-button pad (6-button uses LB/RB for Y/Z). */
            if (s_pad[0] && ev->cbutton.which == s_pad_jid[0] &&
                g_input_map.p[0].pad_type != PAD_6BUTTON) {
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

/* Deadzone (SDL axis units) for a player's analog sticks, from the launcher. */
static int player_deadzone(int player)
{
    int pct = g_input_map.p[player].deadzone_pct;
    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    return pct * 32767 / 100;
}

static int bind_held(SDL_GameController *c, const GamepadBind *bind, int deadzone)
{
    if (!c) return 0;
    if (bind->kind == GP_BIND_BUTTON) {
        return SDL_GameControllerGetButton(c, (SDL_GameControllerButton)bind->code) ? 1 : 0;
    }
    if (bind->kind == GP_BIND_AXIS) {
        int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)bind->code);
        return bind->axis_dir < 0 ? (v < -deadzone) : (v > deadzone);
    }
    return 0;
}

uint16_t gamepad_player_mask(int player)
{
    if (player < 0 || player > 1) return 0;
    SDL_GameController *c = s_pad[player];
    if (!c) return 0;

    const PlayerInput *pi = &g_input_map.p[player];
    const int dz = player_deadzone(player);
    uint16_t m = 0;

    for (int b = 0; b < GB_COUNT; b++) {
        /* In 3-button mode the X/Y/Z/Mode binds are inert. */
        if (pi->pad_type != PAD_6BUTTON && b >= GB_X) continue;
        if (bind_held(c, &pi->pad[b], dz))
            m |= input_button_bit((GenesisButton)b);
    }

    /* Always-on convenience: left analog stick -> d-pad directions. */
    int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (lx < -dz) m |= input_button_bit(GB_LEFT);
    if (lx >  dz) m |= input_button_bit(GB_RIGHT);
    if (ly < -dz) m |= input_button_bit(GB_UP);
    if (ly >  dz) m |= input_button_bit(GB_DOWN);

    return m;
}

int gamepad_turbo_held(void)
{
    /* Player 1's Back/View button, 3-button mode only (6-button uses it for Mode). */
    if (!s_pad[0] || g_input_map.p[0].pad_type == PAD_6BUTTON) return 0;
    return SDL_GameControllerGetButton(s_pad[0], SDL_CONTROLLER_BUTTON_BACK);
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
