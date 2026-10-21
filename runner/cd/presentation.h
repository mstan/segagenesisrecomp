#ifndef SEGACD_PRESENTATION_H
#define SEGACD_PRESENTATION_H

#include <stdint.h>

#include <SDL2/SDL.h>

typedef struct SegaCDPresentation SegaCDPresentation;

/* Owns its streaming texture and UI; window and renderer remain host-owned.
   The settings path is explicit so the host can keep all persistent data off C:. */
SegaCDPresentation *segacd_presentation_create(SDL_Window *window,
                                               SDL_Renderer *renderer,
                                               const char *settings_path,
                                               int force_mute);
void segacd_presentation_destroy(SegaCDPresentation *presentation);

/* Call for every SDL event. Returns nonzero when the event belongs to the menu
   or must be withheld from the game while held inputs drain after menu close. */
int segacd_presentation_handle_event(SegaCDPresentation *presentation,
                                     const SDL_Event *event);
int segacd_presentation_is_open(const SegaCDPresentation *presentation);
/* Poll once per frame before sampling game controls. */
int segacd_presentation_blocks_game_input(
    SegaCDPresentation *presentation);
/* Native, fixed 16:9, or the actual drawable window aspect. Rounded to a
   symmetric even guest-pixel width; the native view is the minimum. */
unsigned segacd_presentation_view_width(const SegaCDPresentation *presentation);

/* Copies and overlays the host frame; the machine's source pixels are untouched. */
int segacd_presentation_present(SegaCDPresentation *presentation,
                                const uint32_t *pixels, int width, int height,
                                int source_pitch_bytes);

#endif
