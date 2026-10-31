#ifndef GENESIS_LOCAL_STATES_H
#define GENESIS_LOCAL_STATES_H

#include <stdint.h>

/* Local-only rewind and the visual browser for the existing nine save files.
 * All calls are on the runner thread, at guest frame boundaries. */
void genesis_local_states_configure(const char *save_dir, int enabled,
                                    int depth, int interval);
void genesis_local_states_shutdown(void);
void genesis_local_states_clear(void);
void genesis_local_states_note_frame(const uint32_t *pixels, int stride,
                                     int width, int height);
void genesis_local_states_slot_saved(int slot);
int  genesis_local_states_open_rewind(void);
int  genesis_local_states_open_menu(void);
int  genesis_local_states_is_open(void); /* 0, 1 rewind, 2 menu */
void genesis_local_states_move(int direction); /* rewind: -1 older; menu: signed slot delta */
void genesis_local_states_cancel(void);
/* Rewind restores a snapshot; menu loads its selected slot. Returns success. */
int  genesis_local_states_accept(void);
/* Save the selected menu slot; returns success. */
int  genesis_local_states_save(void);
/* ARGB overlay, 640x448. The rewind overlay has transparent pixels. */
int  genesis_local_states_overlay(const uint32_t **pixels, int *width, int *height);
int  genesis_local_states_selected_slot(void);

#endif
