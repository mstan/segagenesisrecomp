#include "presentation_ui.h"

#include <stdlib.h>
#include <string.h>

struct SegaCDPresentationUI {
    RecompRuntimeUi *runtime;
    SegaCDPresentationSettings *settings;
    SegaCDPresentationUICallbacks callbacks;
    const RecompRuntimeUiItem *extra_items;
    RecompRuntimeUiItem *owned_extra_items;
    size_t extra_item_count;
    unsigned supported_view_modes;
};

static int is_extra(const SegaCDPresentationUI *ui,
                    const RecompRuntimeUiItem *item) {
    size_t i;
    for (i = 0; i < ui->extra_item_count; ++i) {
        if (&ui->extra_items[i] == item ||
            (ui->extra_items[i].key && item->key &&
             strcmp(ui->extra_items[i].key, item->key) == 0))
            return 1;
    }
    return 0;
}

static int equal_key(const RecompRuntimeUiItem *item, const char *key) {
    return item && item->key && strcmp(item->key, key) == 0;
}

static int get_value(void *context, const RecompRuntimeUiItem *item,
                     int *value_out) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    SegaCDPresentationSettings *s = ui->settings;
    if (!item || !value_out) return 0;
    if (is_extra(ui, item))
        return ui->callbacks.get_extra_value &&
               ui->callbacks.get_extra_value(ui->callbacks.context, item,
                                             value_out);
    if (equal_key(item, RECOMP_RUNTIME_UI_KEY_FULLSCREEN))
        *value_out = s->fullscreen;
    else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE))
        *value_out = s->window_scale;
    else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER))
        *value_out = s->linear_filter;
    else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_AUDIO))
        *value_out = s->audio_enabled;
    else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_VOLUME))
        *value_out = s->volume;
    else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_VIEW_MODE))
        *value_out = s->view_mode;
    else
        return 0;
    return 1;
}

static int set_value(void *context, const RecompRuntimeUiItem *item, int value) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    SegaCDPresentationSettings *s = ui->settings;
    const char *key;
    int *destination;
    int minimum;
    int maximum;
    if (!item) return 0;
    if (is_extra(ui, item))
        return ui->callbacks.set_extra_value &&
               ui->callbacks.set_extra_value(ui->callbacks.context, item, value);

    if (equal_key(item, RECOMP_RUNTIME_UI_KEY_FULLSCREEN)) {
        key = RECOMP_RUNTIME_UI_KEY_FULLSCREEN;
        destination = &s->fullscreen;
        minimum = 0; maximum = 2;
    } else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE)) {
        key = RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE;
        destination = &s->window_scale;
        minimum = 1; maximum = item->maximum;
    } else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)) {
        key = RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER;
        destination = &s->linear_filter;
        minimum = 0; maximum = 1;
    } else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_AUDIO)) {
        key = RECOMP_RUNTIME_UI_KEY_AUDIO;
        destination = &s->audio_enabled;
        minimum = 0; maximum = 1;
    } else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_VOLUME)) {
        key = RECOMP_RUNTIME_UI_KEY_VOLUME;
        destination = &s->volume;
        minimum = 0; maximum = 100;
    } else if (equal_key(item, RECOMP_RUNTIME_UI_KEY_VIEW_MODE)) {
        key = RECOMP_RUNTIME_UI_KEY_VIEW_MODE;
        destination = &s->view_mode;
        minimum = 0; maximum = 2;
        if (value < minimum || value > maximum ||
            !(ui->supported_view_modes & (1u << value)))
            return 0;
    } else {
        return 0;
    }
    if (value < minimum || value > maximum || !ui->callbacks.apply_setting)
        return 0;
    if (!ui->callbacks.apply_setting(ui->callbacks.context, key, value))
        return 0;
    *destination = value;
    return 1;
}

static int run_action(void *context, const RecompRuntimeUiItem *item) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    if (is_extra(ui, item))
        return ui->callbacks.run_extra_action &&
               ui->callbacks.run_extra_action(ui->callbacks.context, item);
    if (equal_key(item, RECOMP_RUNTIME_UI_KEY_RESUME)) {
        recomp_runtime_ui_close(ui->runtime);
        return 1;
    }
    return 0;
}

static int is_enabled(void *context, const RecompRuntimeUiItem *item) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    if (!is_extra(ui, item)) return 1;
    return !ui->callbacks.is_extra_enabled ||
           ui->callbacks.is_extra_enabled(ui->callbacks.context, item);
}

static void save_settings(void *context) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    if (ui->callbacks.save_settings)
        ui->callbacks.save_settings(ui->callbacks.context, ui->settings);
}

static void visibility_changed(void *context, int open) {
    SegaCDPresentationUI *ui = (SegaCDPresentationUI *)context;
    if (ui->callbacks.visibility_changed)
        ui->callbacks.visibility_changed(ui->callbacks.context, open);
}

SegaCDPresentationUI *segacd_presentation_ui_create(
    const SegaCDPresentationUIConfig *config) {
    SegaCDPresentationUI *ui;
    RecompRuntimeUiStandardConfig standard;
    unsigned modes = RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE;
    size_t i, extra_count = 0;
    if (!config || !config->settings || !config->callbacks.apply_setting)
        return NULL;

    ui = (SegaCDPresentationUI *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->settings = config->settings;
    ui->callbacks = config->callbacks;

    if (ui->callbacks.supported_view_modes) {
        ui->supported_view_modes = RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE |
            ui->callbacks.supported_view_modes(ui->callbacks.context);
        modes |= ui->supported_view_modes &
                 (RECOMP_RUNTIME_UI_VIEW_MODE_FIXED_16_9 |
                  RECOMP_RUNTIME_UI_VIEW_MODE_ADAPTIVE);
    } else {
        ui->supported_view_modes = RECOMP_RUNTIME_UI_VIEW_MODE_NATIVE;
    }

    if (config->extra_items && config->extra_item_count &&
        ui->callbacks.get_extra_value && ui->callbacks.set_extra_value) {
        ui->owned_extra_items = (RecompRuntimeUiItem *)calloc(
            config->extra_item_count, sizeof(*ui->owned_extra_items));
        if (!ui->owned_extra_items) {
            free(ui);
            return NULL;
        }
        for (i = 0; i < config->extra_item_count; ++i) {
            const RecompRuntimeUiItem *item = &config->extra_items[i];
            ui->owned_extra_items[extra_count++] = *item;
        }
        ui->extra_items = ui->owned_extra_items;
        ui->extra_item_count = extra_count;
    }

    memset(&standard, 0, sizeof(standard));
    standard.menu.title = "Sonic CD";
    standard.menu.subtitle = config->subtitle;
    standard.menu.theme = "genesis";
    standard.menu.callbacks.context = ui;
    standard.menu.callbacks.get_value = get_value;
    standard.menu.callbacks.set_value = set_value;
    standard.menu.callbacks.run_action = run_action;
    standard.menu.callbacks.is_enabled = is_enabled;
    standard.menu.callbacks.save = save_settings;
    standard.menu.callbacks.visibility_changed = visibility_changed;
    standard.features = RECOMP_RUNTIME_UI_STANDARD_FULLSCREEN |
                        RECOMP_RUNTIME_UI_STANDARD_WINDOW_SCALE |
                        RECOMP_RUNTIME_UI_STANDARD_LINEAR_FILTER |
                        RECOMP_RUNTIME_UI_STANDARD_AUDIO |
                        RECOMP_RUNTIME_UI_STANDARD_VOLUME |
                        RECOMP_RUNTIME_UI_STANDARD_RESUME;
    if (modes & (RECOMP_RUNTIME_UI_VIEW_MODE_FIXED_16_9 |
                 RECOMP_RUNTIME_UI_VIEW_MODE_ADAPTIVE)) {
        standard.features |= RECOMP_RUNTIME_UI_STANDARD_VIEW_MODE;
        standard.view_modes = modes;
    }
    standard.window_scale_max = config->window_scale_max > 0
                                    ? config->window_scale_max : 8;
    standard.extra_items = ui->extra_items;
    standard.extra_item_count = extra_count;
    ui->runtime = recomp_runtime_ui_create_standard(&standard);
    if (!ui->runtime) {
        free(ui->owned_extra_items);
        free(ui);
        return NULL;
    }
    return ui;
}

void segacd_presentation_ui_destroy(SegaCDPresentationUI *ui) {
    if (!ui) return;
    recomp_runtime_ui_destroy(ui->runtime);
    free(ui->owned_extra_items);
    free(ui);
}

int segacd_presentation_ui_is_open(const SegaCDPresentationUI *ui) {
    return ui && recomp_runtime_ui_is_open(ui->runtime);
}

int segacd_presentation_ui_wants_text_input(const SegaCDPresentationUI *ui) {
    return ui && recomp_runtime_ui_wants_text_input(ui->runtime);
}

void segacd_presentation_ui_open(SegaCDPresentationUI *ui) {
    if (ui) recomp_runtime_ui_open(ui->runtime);
}

void segacd_presentation_ui_close(SegaCDPresentationUI *ui) {
    if (ui) recomp_runtime_ui_close(ui->runtime);
}

int segacd_presentation_ui_handle_input(SegaCDPresentationUI *ui,
                                        RecompRuntimeUiInput input,
                                        int pressed, int repeat) {
    return ui ? recomp_runtime_ui_handle_input(ui->runtime, input, pressed,
                                                repeat) : 0;
}

void segacd_presentation_ui_render_argb8888(SegaCDPresentationUI *ui,
                                           void *pixels, int width,
                                           int height, int pitch_bytes) {
    if (ui && pixels)
        recomp_runtime_ui_render_argb8888(ui->runtime, pixels, width, height,
                                           pitch_bytes);
}
