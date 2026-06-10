/*
 * gamepad.h — SDL_GameController integration for the shared runner.
 *
 * On Windows, SDL_GameController routes Xbox-family pads through XInput;
 * other pads (PS / Switch Pro / generic HID) work via SDL's controller
 * database. The runner opens whichever controller appears first and uses
 * it as the P1 input source layered ON TOP of the keyboard (logical OR).
 *
 * Lifecycle:
 *   gamepad_init()           — call AFTER SDL_Init() with
 *                              SDL_INIT_GAMECONTROLLER added.
 *   gamepad_handle_event(ev) — call for every SDL_Event the runner polls.
 *                              Handles add/remove and shoulder-edge taps.
 *   gamepad_button_pressed(btn)   — held state for a Genesis button.
 *   gamepad_turbo_held()          — 1 while Back/View is held.
 *   gamepad_consume_quicksave()   — returns slot 1..9 once per LB press,
 *                                   0 otherwise. (Currently always 1.)
 *   gamepad_consume_quickload()   — same shape for RB.
 *   gamepad_shutdown()
 *
 * The consume_* helpers are edge-triggered: each press fires the slot
 * exactly once even though the runner's event loop polls every frame.
 */
#ifndef RUNNER_GAMEPAD_H
#define RUNNER_GAMEPAD_H

#include "clowncommon.h"
#if OWN_BACKEND
#include "backend_decls.h"   /* own decls — native builds have no clownmdemu paths */
#else
#include "clownmdemu.h"
#endif

union SDL_Event;
typedef union SDL_Event SDL_Event;

void gamepad_init(void);
void gamepad_shutdown(void);
void gamepad_handle_event(const SDL_Event *ev);

cc_bool gamepad_button_pressed(ClownMDEmu_Button btn);
int     gamepad_turbo_held(void);
int     gamepad_consume_quicksave(void);
int     gamepad_consume_quickload(void);

#endif /* RUNNER_GAMEPAD_H */
