/*
 * input_map.c — per-player Genesis controller binding model (see input_map.h).
 */
#include "input_map.h"
#include "gamepad.h"

#include <SDL2/SDL.h>
#include <string.h>

InputMap g_input_map;

/* GPAD_* hardware bit per logical button. MUST stay in sync with the GPAD_*
 * enum in video/genesis_bus.h (mirrored here so shared code needn't include the
 * own-backend bus header). High nibble (X/Y/Z/Mode) is the 6-button extension. */
static const uint16_t k_btn_bit[GB_COUNT] = {
    /* UP   */ 0x0001, /* DOWN */ 0x0002, /* LEFT */ 0x0004, /* RIGHT */ 0x0008,
    /* A    */ 0x0040, /* B    */ 0x0010, /* C    */ 0x0020, /* START */ 0x0080,
    /* X    */ 0x0100, /* Y    */ 0x0200, /* Z    */ 0x0400, /* MODE  */ 0x0800,
};

static const char *k_btn_name[GB_COUNT] = {
    "Up", "Down", "Left", "Right",
    "A", "B", "C", "Start",
    "X", "Y", "Z", "Mode",
};

uint16_t input_button_bit(GenesisButton b)  { return (b >= 0 && b < GB_COUNT) ? k_btn_bit[b] : 0; }
const char *input_button_name(GenesisButton b) { return (b >= 0 && b < GB_COUNT) ? k_btn_name[b] : "?"; }

static void set_btn(PlayerInput *pi, GenesisButton b, int scancode, int pad_button)
{
    pi->key[b]          = scancode;
    pi->pad[b].kind     = GP_BIND_BUTTON;
    pi->pad[b].code     = pad_button;
    pi->pad[b].axis_dir = 0;
}

void input_map_init_defaults(void)
{
    memset(&g_input_map, 0, sizeof(g_input_map));

    PlayerInput *p1 = &g_input_map.p[0];
    p1->device       = INPUT_DEV_KEYBOARD; /* exclusive device select (pick Gamepad in launcher) */
    p1->pad_type     = PAD_3BUTTON;
    p1->deadzone_pct = 25;
    /* Keyboard arrows + Z/X/C + Return (the existing main.c mapping), plus
     * A/S/D/Tab for the 6-button extension. Gamepad: d-pad + A/B/X/Start, with
     * Y/LB/RB/Back for X/Y/Z/Mode. */
    set_btn(p1, GB_UP,    SDL_SCANCODE_UP,     SDL_CONTROLLER_BUTTON_DPAD_UP);
    set_btn(p1, GB_DOWN,  SDL_SCANCODE_DOWN,   SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    set_btn(p1, GB_LEFT,  SDL_SCANCODE_LEFT,   SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    set_btn(p1, GB_RIGHT, SDL_SCANCODE_RIGHT,  SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    set_btn(p1, GB_A,     SDL_SCANCODE_Z,      SDL_CONTROLLER_BUTTON_A);
    set_btn(p1, GB_B,     SDL_SCANCODE_X,      SDL_CONTROLLER_BUTTON_B);
    set_btn(p1, GB_C,     SDL_SCANCODE_C,      SDL_CONTROLLER_BUTTON_X);
    set_btn(p1, GB_START, SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START);
    set_btn(p1, GB_X,     SDL_SCANCODE_A,      SDL_CONTROLLER_BUTTON_Y);
    set_btn(p1, GB_Y,     SDL_SCANCODE_S,      SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    set_btn(p1, GB_Z,     SDL_SCANCODE_D,      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    set_btn(p1, GB_MODE,  SDL_SCANCODE_BACKSPACE, SDL_CONTROLLER_BUTTON_BACK); /* TAB is the turbo key */

    PlayerInput *p2 = &g_input_map.p[1];
    p2->device       = INPUT_DEV_NONE;   /* 2-player is opt-in (no regression) */
    p2->pad_type     = PAD_3BUTTON;
    p2->deadzone_pct = 25;
    /* A usable right-hand keyboard set + second gamepad, ready when the user
     * enables P2 in the launcher. */
    set_btn(p2, GB_UP,    SDL_SCANCODE_I,      SDL_CONTROLLER_BUTTON_DPAD_UP);
    set_btn(p2, GB_DOWN,  SDL_SCANCODE_K,      SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    set_btn(p2, GB_LEFT,  SDL_SCANCODE_J,      SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    set_btn(p2, GB_RIGHT, SDL_SCANCODE_L,      SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    set_btn(p2, GB_A,     SDL_SCANCODE_N,      SDL_CONTROLLER_BUTTON_A);
    set_btn(p2, GB_B,     SDL_SCANCODE_M,      SDL_CONTROLLER_BUTTON_B);
    set_btn(p2, GB_C,     SDL_SCANCODE_COMMA,  SDL_CONTROLLER_BUTTON_X);
    set_btn(p2, GB_START, SDL_SCANCODE_RSHIFT, SDL_CONTROLLER_BUTTON_START);
    set_btn(p2, GB_X,     SDL_SCANCODE_U,      SDL_CONTROLLER_BUTTON_Y);
    set_btn(p2, GB_Y,     SDL_SCANCODE_O,      SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    set_btn(p2, GB_Z,     SDL_SCANCODE_P,      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    set_btn(p2, GB_MODE,  SDL_SCANCODE_SLASH,  SDL_CONTROLLER_BUTTON_BACK);
}

uint16_t input_map_key_mask(int player)
{
    if (player < 0 || player > 1) return 0;
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    if (!ks) return 0;
    const PlayerInput *pi = &g_input_map.p[player];
    uint16_t m = 0;
    for (int b = 0; b < GB_COUNT; b++) {
        int sc = pi->key[b];
        if (sc > 0 && sc < SDL_NUM_SCANCODES && ks[sc])
            m |= k_btn_bit[b];
    }
    return m;
}

uint16_t input_current_mask(int player)
{
    if (player < 0 || player > 1) return 0;
    const PlayerInput *pi = &g_input_map.p[player];
    uint16_t m = 0;
    if (pi->device & INPUT_DEV_KEYBOARD) m |= input_map_key_mask(player);
    if (pi->device & INPUT_DEV_GAMEPAD)  m |= gamepad_player_mask(player);
    return m;
}

cc_bool input_player_enabled(int player)
{
    if (player < 0 || player > 1) return cc_false;
    return g_input_map.p[player].device != INPUT_DEV_NONE ? cc_true : cc_false;
}
