#ifndef SEGACD_PRESENTATION_UI_H
#define SEGACD_PRESENTATION_UI_H

#include <stddef.h>
#include <stdint.h>

#include "recomp_runtime_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SegaCDPresentationUI SegaCDPresentationUI;

/* Small frontend-owned setting snapshot. The bridge never persists this itself. */
typedef struct SegaCDPresentationSettings {
    int fullscreen;       /* 0 windowed, 1 borderless, 2 exclusive */
    int window_scale;     /* 1..window_scale_max */
    int linear_filter;    /* boolean */
    int audio_enabled;    /* boolean */
    int volume;           /* 0..100 */
    int view_mode;        /* RecompRuntimeUiViewMode */
} SegaCDPresentationSettings;

typedef struct SegaCDPresentationUICallbacks {
    void *context;

    /* Apply a standard or host-defined setting to the frontend. Return nonzero
       only after accepting it. The bridge commits its local snapshot afterward. */
    int (*apply_setting)(void *context, const char *key, int value);

    /* Optional extra-item accessors; extra rows are omitted without both.
       The host must pass only extras supported by the active frontend/game.
       In particular, only pass widescreen controls when those features work. */
    int (*get_extra_value)(void *context, const RecompRuntimeUiItem *item,
                           int *value_out);
    int (*set_extra_value)(void *context, const RecompRuntimeUiItem *item,
                           int value);
    int (*is_extra_enabled)(void *context, const RecompRuntimeUiItem *item);
    int (*run_extra_action)(void *context, const RecompRuntimeUiItem *item);

    /* Return genuinely supported fixed/adaptive view modes. Native is implicit. */
    unsigned (*supported_view_modes)(void *context);

    /* Called by recomp-ui after an accepted setting change. */
    void (*save_settings)(void *context,
                          const SegaCDPresentationSettings *settings);
    /* Host pauses/resumes game input/simulation as it sees the visibility change. */
    void (*visibility_changed)(void *context, int open);
} SegaCDPresentationUICallbacks;

typedef struct SegaCDPresentationUIConfig {
    SegaCDPresentationSettings *settings; /* caller-owned, required */
    SegaCDPresentationUICallbacks callbacks;
    const RecompRuntimeUiItem *extra_items; /* caller-owned, optional */
    size_t extra_item_count;
    int window_scale_max; /* defaults to 8 */
    const char *subtitle;  /* caller-owned, optional */
} SegaCDPresentationUIConfig;

SegaCDPresentationUI *segacd_presentation_ui_create(
    const SegaCDPresentationUIConfig *config);
void segacd_presentation_ui_destroy(SegaCDPresentationUI *ui);

int segacd_presentation_ui_is_open(const SegaCDPresentationUI *ui);
int segacd_presentation_ui_wants_text_input(const SegaCDPresentationUI *ui);
void segacd_presentation_ui_open(SegaCDPresentationUI *ui);
void segacd_presentation_ui_close(SegaCDPresentationUI *ui);

/* Returns nonzero when the menu consumed this mapped input. While open, all
   mapped inputs are consumed, so the host must not forward game controls. */
int segacd_presentation_ui_handle_input(SegaCDPresentationUI *ui,
                                        RecompRuntimeUiInput input,
                                        int pressed, int repeat);

/* Overlay onto the host-owned little-endian SDL ARGB8888/BGRA frame. */
void segacd_presentation_ui_render_argb8888(SegaCDPresentationUI *ui,
                                           void *pixels, int width,
                                           int height, int pitch_bytes);

#ifdef __cplusplus
}
#endif

#endif
