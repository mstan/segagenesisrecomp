#include "presentation.h"

#include "presentation_ui.h"
#include "sonic_cd_limits.h"
#include "../audio.h"
#include "backup.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEGACD_DEFAULT_WIDTH 320
#define SEGACD_DEFAULT_HEIGHT 224
#define SEGACD_MAX_SETTINGS_BYTES 4096
#define SEGACD_GAME_KEY_COUNT 9

struct SegaCDPresentation {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SegaCDPresentationUI *ui;
    SegaCDPresentationSettings settings;
    char *settings_path;
    uint32_t *frame_copy;
    size_t frame_capacity;
    uint32_t controller_buttons;
    int game_keys[SEGACD_GAME_KEY_COUNT];
    int16_t controller_left_x;
    int16_t controller_left_y;
    int width;
    int height;
    int force_mute;
    int block_until_release;
};

static char *copy_string(const char *value) {
    size_t length;
    char *copy;
    if (!value) return NULL;
    length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, value, length + 1);
    return copy;
}

static int clamp_value(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void load_one_setting(char *line, SegaCDPresentationSettings *settings) {
    char *equals = strchr(line, '=');
    char *end;
    long value;
    if (!equals) return;
    *equals++ = '\0';
    errno = 0;
    value = strtol(equals, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (errno || end == equals || *end || value < INT_MIN || value > INT_MAX)
        return;
    if (strcmp(line, "fullscreen") == 0)
        settings->fullscreen = clamp_value((int)value, 0, 2);
    else if (strcmp(line, "window_scale") == 0)
        settings->window_scale = clamp_value((int)value, 1, 8);
    else if (strcmp(line, "linear_filter") == 0)
        settings->linear_filter = clamp_value((int)value, 0, 1);
    else if (strcmp(line, "audio_enabled") == 0)
        settings->audio_enabled = clamp_value((int)value, 0, 1);
    else if (strcmp(line, "volume") == 0)
        settings->volume = clamp_value((int)value, 0, 100);
    else if (strcmp(line, "view_mode") == 0)
        settings->view_mode = clamp_value((int)value, 0, 2);
}

static void load_settings(const char *path,
                          SegaCDPresentationSettings *settings) {
    SDL_RWops *file;
    Sint64 size;
    char *data;
    size_t read_size;
    char *line;
    char *next;
    if (!path || !settings) return;
    file = SDL_RWFromFile(path, "rb");
    if (!file) return;
    size = SDL_RWsize(file);
    if (size <= 0 || size > SEGACD_MAX_SETTINGS_BYTES) {
        SDL_RWclose(file);
        return;
    }
    data = (char *)malloc((size_t)size + 1);
    if (!data) {
        SDL_RWclose(file);
        return;
    }
    read_size = SDL_RWread(file, data, 1, (size_t)size);
    if (SDL_RWclose(file) != 0 || read_size != (size_t)size) {
        free(data);
        return;
    }
    data[size] = '\0';
    line = data;
    while (line && *line) {
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        load_one_setting(line, settings);
        line = next;
    }
    free(data);
}

static int save_settings(SegaCDPresentation *presentation) {
    static const char format[] =
        "fullscreen=%d\nwindow_scale=%d\nlinear_filter=%d\n"
        "audio_enabled=%d\nvolume=%d\nview_mode=%d\n";
    char contents[192];
    int content_length;
    if (!presentation || !presentation->settings_path) return 0;
    content_length = snprintf(contents, sizeof(contents), format,
                              presentation->settings.fullscreen,
                              presentation->settings.window_scale,
                              presentation->settings.linear_filter,
                              presentation->settings.audio_enabled,
                              presentation->settings.volume,
                              presentation->settings.view_mode);
    if (content_length < 0 || (size_t)content_length >= sizeof(contents)) {
        return 0;
    }
    return segacd_atomic_file_store(presentation->settings_path, contents,
                                    (size_t)content_length);
}

static void apply_audio(SegaCDPresentation *presentation) {
    int volume;
    if (!presentation) return;
    volume = presentation->settings.audio_enabled
                 ? presentation->settings.volume : 0;
    if (presentation->force_mute) volume = 0;
    audio_set_master_volume(volume);
}

static int apply_setting(void *context, const char *key, int value) {
    SegaCDPresentation *presentation = (SegaCDPresentation *)context;
    if (!presentation || !key) return 0;
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_FULLSCREEN) == 0) {
        Uint32 flags = 0;
        if (value == 1) flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        else if (value == 2) flags = SDL_WINDOW_FULLSCREEN;
        if (SDL_SetWindowFullscreen(presentation->window, flags) != 0)
            return 0;
        presentation->settings.fullscreen = value;
        return 1;
    }
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE) == 0) {
        int width = presentation->width > 0
                        ? presentation->width : SEGACD_DEFAULT_WIDTH;
        int height = presentation->height > 0
                         ? presentation->height : SEGACD_DEFAULT_HEIGHT;
        if (value < 1 || value > 8 || width > INT_MAX / value ||
            height > INT_MAX / value)
            return 0;
        SDL_SetWindowSize(presentation->window, width * value, height * value);
        presentation->settings.window_scale = value;
        return 1;
    }
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER) == 0) {
        if (presentation->texture) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
            if (SDL_SetTextureScaleMode(presentation->texture,
                value ? SDL_ScaleModeLinear : SDL_ScaleModeNearest) != 0)
                return 0;
#else
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, value ? "1" : "0");
            SDL_DestroyTexture(presentation->texture);
            presentation->texture = NULL;
#endif
        }
        presentation->settings.linear_filter = value != 0;
        return 1;
    }
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_AUDIO) == 0) {
        presentation->settings.audio_enabled = value != 0;
        apply_audio(presentation);
        return 1;
    }
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_VOLUME) == 0) {
        presentation->settings.volume = clamp_value(value, 0, 100);
        apply_audio(presentation);
        return 1;
    }
    if (strcmp(key, RECOMP_RUNTIME_UI_KEY_VIEW_MODE) == 0) {
        if (value < RECOMP_RUNTIME_UI_VIEW_NATIVE ||
            value > RECOMP_RUNTIME_UI_VIEW_ADAPTIVE) return 0;
        presentation->settings.view_mode=value;
        return 1;
    }
    return 0;
}

static void persist_settings(void *context,
                             const SegaCDPresentationSettings *settings) {
    SegaCDPresentation *presentation = (SegaCDPresentation *)context;
    if (!presentation || !settings) return;
    presentation->settings = *settings;
    if (!save_settings(presentation))
        fprintf(stderr, "Cannot atomically save Sonic CD settings: %s\n",
                presentation->settings_path ? presentation->settings_path
                                            : "(no path)");
}

static unsigned supported_view_modes(void *context) {
    (void)context;
    return RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE |
           RECOMP_RUNTIME_UI_VIEW_MODE_FIXED_16_9 |
           RECOMP_RUNTIME_UI_VIEW_MODE_ADAPTIVE;
}

static int is_game_key(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_Z:
    case SDL_SCANCODE_X:
    case SDL_SCANCODE_C:
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return 1;
    default:
        return 0;
    }
}

static int game_key_slot(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_UP: return 0;
    case SDL_SCANCODE_DOWN: return 1;
    case SDL_SCANCODE_LEFT: return 2;
    case SDL_SCANCODE_RIGHT: return 3;
    case SDL_SCANCODE_Z: return 4;
    case SDL_SCANCODE_X: return 5;
    case SDL_SCANCODE_C: return 6;
    case SDL_SCANCODE_RETURN: return 7;
    case SDL_SCANCODE_KP_ENTER: return 8;
    default: return -1;
    }
}

static int map_key(SDL_Scancode key, RecompRuntimeUiInput *input) {
    switch (key) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_F1:
        *input = RECOMP_RUNTIME_UI_INPUT_TOGGLE;
        return 1;
    case SDL_SCANCODE_UP: *input = RECOMP_RUNTIME_UI_INPUT_UP; return 1;
    case SDL_SCANCODE_DOWN: *input = RECOMP_RUNTIME_UI_INPUT_DOWN; return 1;
    case SDL_SCANCODE_LEFT: *input = RECOMP_RUNTIME_UI_INPUT_LEFT; return 1;
    case SDL_SCANCODE_RIGHT: *input = RECOMP_RUNTIME_UI_INPUT_RIGHT; return 1;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        *input = RECOMP_RUNTIME_UI_INPUT_ACCEPT;
        return 1;
    case SDL_SCANCODE_BACKSPACE:
        *input = RECOMP_RUNTIME_UI_INPUT_BACK;
        return 1;
    default:
        return 0;
    }
}

static int map_controller_button(Uint8 button, int menu_open,
                                 RecompRuntimeUiInput *input) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        *input = RECOMP_RUNTIME_UI_INPUT_UP; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        *input = RECOMP_RUNTIME_UI_INPUT_DOWN; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        *input = RECOMP_RUNTIME_UI_INPUT_LEFT; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        *input = RECOMP_RUNTIME_UI_INPUT_RIGHT; return 1;
    case SDL_CONTROLLER_BUTTON_A:
        *input = RECOMP_RUNTIME_UI_INPUT_ACCEPT; return 1;
    case SDL_CONTROLLER_BUTTON_B:
        if (!menu_open) return 0; /* Genesis B remains the game's C button. */
        *input = RECOMP_RUNTIME_UI_INPUT_BACK;
        return 1;
    case SDL_CONTROLLER_BUTTON_BACK:
        *input = menu_open ? RECOMP_RUNTIME_UI_INPUT_BACK
                           : RECOMP_RUNTIME_UI_INPUT_TOGGLE;
        return 1;
    default:
        return 0;
    }
}

static int game_inputs_held(const SegaCDPresentation *presentation) {
    size_t i;
    if (!presentation) return 0;
    for (i = 0; i < sizeof(presentation->game_keys) /
                    sizeof(presentation->game_keys[0]); ++i) {
        if (presentation->game_keys[i]) return 1;
    }
    return presentation->controller_buttons != 0 ||
           presentation->controller_left_x < -16000 ||
           presentation->controller_left_x > 16000 ||
           presentation->controller_left_y < -16000 ||
           presentation->controller_left_y > 16000;
}

SegaCDPresentation *segacd_presentation_create(SDL_Window *window,
                                               SDL_Renderer *renderer,
                                               const char *settings_path,
                                               int force_mute) {
    SegaCDPresentation *presentation;
    SegaCDPresentationUIConfig ui_config;
    if (!window || !renderer || !settings_path || !*settings_path) return NULL;
    presentation = (SegaCDPresentation *)calloc(1, sizeof(*presentation));
    if (!presentation) return NULL;
    presentation->window = window;
    presentation->renderer = renderer;
    presentation->settings_path = copy_string(settings_path);
    presentation->width = SEGACD_DEFAULT_WIDTH;
    presentation->height = SEGACD_DEFAULT_HEIGHT;
    presentation->force_mute = force_mute != 0;
    presentation->settings.fullscreen = 0;
    presentation->settings.window_scale = 3;
    presentation->settings.linear_filter = 0;
    presentation->settings.audio_enabled = 1;
    presentation->settings.volume = 100;
    presentation->settings.view_mode = RECOMP_RUNTIME_UI_VIEW_NATIVE;
    if (!presentation->settings_path) {
        free(presentation);
        return NULL;
    }
    load_settings(presentation->settings_path, &presentation->settings);

    /* Owner-facing launches always start in a normal window, even if an older
       settings file requested fullscreen. The UI can still change it in-session. */
    presentation->settings.fullscreen = 0;
    SDL_SetWindowTitle(window, "Sonic CD");
    if (SDL_SetWindowFullscreen(window, 0) != 0) {
        free(presentation->settings_path);
        free(presentation);
        return NULL;
    }
    int initial_width=presentation->settings.view_mode==RECOMP_RUNTIME_UI_VIEW_NATIVE ?
        presentation->width : 398;
    SDL_SetWindowSize(window,
                      initial_width * presentation->settings.window_scale,
                      presentation->height * presentation->settings.window_scale);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                presentation->settings.linear_filter ? "1" : "0");
    apply_audio(presentation);

    memset(&ui_config, 0, sizeof(ui_config));
    ui_config.settings = &presentation->settings;
    ui_config.callbacks.context = presentation;
    ui_config.callbacks.apply_setting = apply_setting;
    ui_config.callbacks.save_settings = persist_settings;
    ui_config.callbacks.supported_view_modes = supported_view_modes;
    ui_config.window_scale_max = 8;
    presentation->ui = segacd_presentation_ui_create(&ui_config);
    if (!presentation->ui) {
        free(presentation->settings_path);
        free(presentation);
        return NULL;
    }
    return presentation;
}

void segacd_presentation_destroy(SegaCDPresentation *presentation) {
    if (!presentation) return;
    segacd_presentation_ui_destroy(presentation->ui);
    if (presentation->texture) SDL_DestroyTexture(presentation->texture);
    free(presentation->frame_copy);
    free(presentation->settings_path);
    free(presentation);
}

int segacd_presentation_handle_event(SegaCDPresentation *presentation,
                                     const SDL_Event *event) {
    RecompRuntimeUiInput input;
    int was_open;
    int was_draining;
    int consumed = 0;
    if (!presentation || !event) return 0;
    was_open = segacd_presentation_ui_is_open(presentation->ui);
    was_draining = presentation->block_until_release;
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_Scancode key = event->key.keysym.scancode;
        int pressed = event->type == SDL_KEYDOWN;
        int slot = game_key_slot(key);
        if (slot >= 0) presentation->game_keys[slot] = pressed;
        if (map_key(key, &input)) {
            consumed = segacd_presentation_ui_handle_input(
                presentation->ui, input, pressed, event->key.repeat != 0);
        }
        if (was_open && !segacd_presentation_ui_is_open(presentation->ui))
            presentation->block_until_release = 1;
        if (consumed || was_open || segacd_presentation_ui_is_open(presentation->ui))
            return 1;
        if (was_draining) {
            (void)segacd_presentation_blocks_game_input(presentation);
            return 1;
        }
        return (is_game_key(key) &&
                segacd_presentation_blocks_game_input(presentation)) ? 1 : 0;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN ||
        event->type == SDL_CONTROLLERBUTTONUP) {
        Uint8 button = event->cbutton.button;
        int pressed = event->type == SDL_CONTROLLERBUTTONDOWN;
        if (button < 32) {
            uint32_t bit = UINT32_C(1) << button;
            if (pressed) presentation->controller_buttons |= bit;
            else presentation->controller_buttons &= ~bit;
        }
        if (map_controller_button(button, was_open, &input)) {
            consumed = segacd_presentation_ui_handle_input(
                presentation->ui, input, pressed, 0);
        }
        if (was_open && !segacd_presentation_ui_is_open(presentation->ui))
            presentation->block_until_release = 1;
        if (consumed || was_open || segacd_presentation_ui_is_open(presentation->ui))
            return 1;
        if (was_draining) {
            (void)segacd_presentation_blocks_game_input(presentation);
            return 1;
        }
        return segacd_presentation_blocks_game_input(presentation);
    }
    if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        presentation->controller_buttons = 0;
        presentation->controller_left_x = 0;
        presentation->controller_left_y = 0;
        return segacd_presentation_blocks_game_input(presentation);
    }
    if (event->type == SDL_CONTROLLERAXISMOTION) {
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            presentation->controller_left_x = event->caxis.value;
        else if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            presentation->controller_left_y = event->caxis.value;
        return segacd_presentation_is_open(presentation) ||
               segacd_presentation_blocks_game_input(presentation);
    }
    if (event->type == SDL_WINDOWEVENT &&
        event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        return segacd_presentation_is_open(presentation) ||
               segacd_presentation_blocks_game_input(presentation);
    return was_open || segacd_presentation_ui_is_open(presentation->ui);
}

int segacd_presentation_is_open(const SegaCDPresentation *presentation) {
    return presentation &&
           segacd_presentation_ui_is_open(presentation->ui);
}

unsigned segacd_presentation_view_width(const SegaCDPresentation *presentation) {
    int w=0,h=0;
    if (!presentation || presentation->settings.view_mode==RECOMP_RUNTIME_UI_VIEW_NATIVE)
        return SEGACD_DEFAULT_WIDTH;
    if (presentation->settings.view_mode==RECOMP_RUNTIME_UI_VIEW_FIXED_16_9) {
        w=16;h=9;
    } else if (SDL_GetRendererOutputSize(presentation->renderer,&w,&h)!=0 || w<=0 || h<=0)
        return SEGACD_DEFAULT_WIDTH;
    uint64_t width=((uint64_t)SEGACD_DEFAULT_HEIGHT*(unsigned)w+(unsigned)h/2u)/(unsigned)h;
    if (width<SEGACD_DEFAULT_WIDTH)width=SEGACD_DEFAULT_WIDTH;
    if (width>SEGACD_SONIC_CD_MAX_VIEW_WIDTH)width=SEGACD_SONIC_CD_MAX_VIEW_WIDTH;
    return (unsigned)width & ~1u;
}

int segacd_presentation_blocks_game_input(
    SegaCDPresentation *presentation) {
    if (!presentation) return 0;
    if (segacd_presentation_ui_is_open(presentation->ui)) return 1;
    if (!presentation->block_until_release) return 0;
    if (game_inputs_held(presentation)) return 1;
    presentation->block_until_release = 0;
    return 0;
}

static int ensure_frame_capacity(SegaCDPresentation *presentation,
                                 size_t needed) {
    uint32_t *resized;
    if (needed <= presentation->frame_capacity) return 1;
    resized = (uint32_t *)realloc(presentation->frame_copy,
                                  needed * sizeof(*resized));
    if (!resized) return 0;
    presentation->frame_copy = resized;
    presentation->frame_capacity = needed;
    return 1;
}

static int ensure_texture(SegaCDPresentation *presentation,
                          int width, int height) {
    if (presentation->texture && presentation->width == width &&
        presentation->height == height)
        return 1;
    if (presentation->texture) SDL_DestroyTexture(presentation->texture);
    presentation->texture = SDL_CreateTexture(
        presentation->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!presentation->texture) return 0;
    SDL_SetTextureBlendMode(presentation->texture, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(presentation->texture,
        presentation->settings.linear_filter ? SDL_ScaleModeLinear
                                             : SDL_ScaleModeNearest);
#endif
    presentation->width = width;
    presentation->height = height;
    return 1;
}

int segacd_presentation_present(SegaCDPresentation *presentation,
                                const uint32_t *pixels, int width, int height,
                                int source_pitch_bytes) {
    size_t row_bytes;
    size_t count;
    int y;
    if (!presentation || !pixels || width <= 0 || height <= 0 ||
        width > INT_MAX / (int)sizeof(uint32_t))
        return 0;
    row_bytes = (size_t)width * sizeof(uint32_t);
    if (source_pitch_bytes < 0 || (size_t)source_pitch_bytes < row_bytes ||
        (size_t)height > SIZE_MAX / (size_t)width ||
        (source_pitch_bytes > 0 && (size_t)height >
             SIZE_MAX / (size_t)source_pitch_bytes))
        return 0;
    count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / sizeof(uint32_t) ||
        !ensure_frame_capacity(presentation, count))
        return 0;
    for (y = 0; y < height; ++y) {
        const uint8_t *source_row = (const uint8_t *)pixels +
                                    (size_t)y * (size_t)source_pitch_bytes;
        memcpy(presentation->frame_copy + (size_t)y * (size_t)width,
               source_row, row_bytes);
    }
    segacd_presentation_ui_render_argb8888(presentation->ui,
                                           presentation->frame_copy, width,
                                           height, width * (int)sizeof(uint32_t));
    if (!ensure_texture(presentation, width, height)) return 0;
    if (SDL_UpdateTexture(presentation->texture, NULL, presentation->frame_copy,
                          width * (int)sizeof(uint32_t)) != 0)
        return 0;
    SDL_RenderSetLogicalSize(presentation->renderer, width, height);
    if (SDL_RenderClear(presentation->renderer) != 0) return 0;
    if (SDL_RenderCopy(presentation->renderer, presentation->texture, NULL,
                       NULL) != 0)
        return 0;
    SDL_RenderPresent(presentation->renderer);
    presentation->width = width;
    presentation->height = height;
    return 1;
}
