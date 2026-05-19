/*
 * sonic_extras.h — Sonic 1 view onto FrameRecord.game_data.
 *
 * The game-agnostic ring buffer reserves 256 bytes per frame for the
 * game project to fill (see game_extras.h). This header declares the
 * Sonic 1 layout that we cast onto that buffer so other modules
 * (e.g. cmd_server.c's frame_range walker) can decode it.
 */
#pragma once
#include <stdint.h>

#define SONIC_GAME_DATA_VERSION 2

typedef struct {
    uint32_t version;            /* SONIC_GAME_DATA_VERSION */
    uint8_t  game_mode;          /* $FFF600 */
    uint8_t  vblank_flag;        /* $FFF62A */
    uint8_t  joy_held;           /* $FFF602 */
    uint8_t  joy_press;          /* $FFF603 */
    uint16_t scroll_x;           /* $FFF700 */
    uint16_t sonic_x;            /* $FFD008 */
    uint16_t sonic_y;            /* $FFD00C */
    int16_t  sonic_xvel;         /* $FFD010 */
    int16_t  sonic_yvel;         /* $FFD012 */
    int16_t  sonic_inertia;      /* $FFD014 */
    uint8_t  sonic_routine;      /* $FFD024 */
    uint8_t  sonic_status;       /* $FFD022 */
    uint8_t  sonic_angle;        /* $FFD026 */
    uint8_t  sonic_obj_id;       /* $FFD000 */
    uint8_t  _pad;
    /* Internal frame counter — v_vblank_count at $FFFE0C longword. The
     * Sonic 1 VBlank handler does addq.l #1,(v_vblank_count).w once per
     * serviced VBlank (disasm: Vint at $0B64). Suitable as the primary
     * state-marker sync key for compare_runs.py because it progresses
     * at the same rate in both native and oracle regardless of
     * wall-clock execution speed.  NOTE: $FFFE04 is v_framecount (word),
     * only incremented inside the Levels-mode vblank path — do NOT use. */
    uint32_t internal_frame_ctr;  /* $FFFE0C longword (v_vblank_count) */
} SonicGameData;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(SonicGameData) <= 256,
               "SonicGameData must fit in FrameRecord.game_data[256]");
#endif

/* Convenience: read the typed view from a frame's raw game_data. */
static inline const SonicGameData *sonic_extras_view(const uint8_t game_data[256]) {
    return (const SonicGameData *)game_data;
}
