/*
 * input_map.h — per-player Genesis controller binding model.
 *
 * Replaces the hardcoded keyboard switch in main.c and the fixed gamepad map in
 * gamepad.c with a rebindable, per-player table covering all 12 logical Genesis
 * buttons (3-button: U/D/L/R/A/B/C/Start; 6-button adds X/Y/Z/Mode). The launcher
 * edits this table; the runner consumes it; app_config persists it to settings.ini.
 *
 * One module so both input paths (the GenesisButton input_requested_cb used by
 * the oracle, and the own-backend per-frame pad push to machine_set_pad) agree on
 * a single source of truth. Defaults reproduce today's mapping exactly, so a user
 * who never opens the launcher / never writes settings.ini sees no change.
 */
#ifndef RUNNER_INPUT_MAP_H
#define RUNNER_INPUT_MAP_H

#include <stdint.h>
#include "clowncommon.h"   /* cc_bool */

#ifdef __cplusplus
extern "C" {
#endif

/* Logical Genesis buttons, in a stable order used for arrays + UI rows. */
typedef enum GenesisButton {
    GB_UP = 0, GB_DOWN, GB_LEFT, GB_RIGHT,
    GB_A, GB_B, GB_C, GB_START,
    GB_X, GB_Y, GB_Z, GB_MODE,
    GB_COUNT
} GenesisButton;

/* Device source for a player. The value IS a bitmask: bit0 = keyboard live,
 * bit1 = gamepad live. So BOTH (3) ORs keyboard + gamepad like today's P1. */
typedef enum InputDevice {
    INPUT_DEV_NONE     = 0,
    INPUT_DEV_KEYBOARD = 1,
    INPUT_DEV_GAMEPAD  = 2,
    INPUT_DEV_BOTH     = 3,
} InputDevice;

/* Pad protocol exposed on the controller port. */
typedef enum PadType { PAD_3BUTTON = 0, PAD_6BUTTON = 1 } PadType;

/* A gamepad binding: an SDL_GameController button, or an axis + direction. */
typedef enum GamepadBindKind {
    GP_BIND_NONE = 0, GP_BIND_BUTTON = 1, GP_BIND_AXIS = 2
} GamepadBindKind;

typedef struct GamepadBind {
    int kind;       /* GamepadBindKind */
    int code;       /* SDL_GameControllerButton or SDL_GameControllerAxis */
    int axis_dir;   /* -1 / +1 for GP_BIND_AXIS, else 0 */
} GamepadBind;

typedef struct PlayerInput {
    int         device;            /* InputDevice */
    int         pad_type;          /* PadType */
    int         deadzone_pct;      /* 0..100 (stick analog deadzone) */
    int         key[GB_COUNT];     /* SDL_Scancode per button (0 = unbound) */
    GamepadBind pad[GB_COUNT];     /* gamepad binding per button */
} PlayerInput;

typedef struct InputMap {
    PlayerInput p[2];
} InputMap;

/* The single live instance the runner + launcher read/write. */
extern InputMap g_input_map;

/* Populate g_input_map with the default mapping (today's behaviour: P1 keyboard
 * arrows/Z/X/C/Return + first gamepad, P2 disabled). Call once at startup before
 * app_config_load() (which overrides any fields present in settings.ini). */
void input_map_init_defaults(void);

/* Keyboard contribution for a player: a GPAD_* bit mask of currently-held bound
 * keys (reads SDL_GetKeyboardState). Does NOT apply device gating. */
uint16_t input_map_key_mask(int player);

/* Full, device-gated pressed mask for a player in GPAD_* bit layout, combining
 * keyboard (input_map_key_mask) and that player's gamepad (gamepad_player_mask).
 * This is what gets pushed to machine_set_pad() / queried by input_requested_cb. */
uint16_t input_current_mask(int player);

/* Map a GenesisButton to its GPAD_* hardware bit (genesis_bus.h layout). */
uint16_t input_button_bit(GenesisButton b);

/* Stable human label for a button ("Up", "A", "Mode", ...) — used by the launcher
 * binding table and by app_config keys. */
const char *input_button_name(GenesisButton b);

/* Whether a player currently contributes input (device != NONE). */
cc_bool input_player_enabled(int player);

#ifdef __cplusplus
}
#endif

#endif /* RUNNER_INPUT_MAP_H */
