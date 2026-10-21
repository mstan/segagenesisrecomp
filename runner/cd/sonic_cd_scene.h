/* Read-only capture of Sonic CD's original queued-object sprite scene. */
#ifndef SEGACD_SONIC_CD_SCENE_H
#define SEGACD_SONIC_CD_SCENE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SEGACD_SCENE_QUEUE_BASE = 0xAC00,
    SEGACD_SCENE_QUEUE_BYTES = 0x400,
    SEGACD_SCENE_QUEUE_LEVELS = 8,
    SEGACD_SCENE_QUEUE_LEVEL_BYTES = 0x80,
    SEGACD_SCENE_MAX_QUEUE_OBJECTS = 8 * 63,
    SEGACD_SCENE_MAX_PIECES = 8 * 63 * 255,
    SEGACD_SCENE_SAT_ENTRIES = 80,
    SEGACD_SCENE_SAT_BYTES = 80 * 8,
    SEGACD_SCENE_OBJECT_SIZE = 0x40
};

typedef struct {
    const uint8_t *word_ram;
    size_t word_ram_size;
    uint32_t word_ram_base;
    const uint8_t *main_ram;
    size_t main_ram_size;
    uint32_t main_ram_base;
} SegaCDSceneSnapshot;

typedef enum {
    SEGACD_SCENE_OK = 0,
    SEGACD_SCENE_INVALID_ARGUMENT,
    SEGACD_SCENE_INVALID_SNAPSHOT,
    SEGACD_SCENE_MALFORMED_QUEUE,
    SEGACD_SCENE_INVALID_OBJECT_POINTER,
    SEGACD_SCENE_UNRESOLVED_MAPPING,
    SEGACD_SCENE_MALFORMED_MAPPING,
    SEGACD_SCENE_CAPACITY
} SegaCDSceneStatus;

typedef enum {
    SEGACD_SCENE_SLOT_DYNAMIC = 0,
    SEGACD_SCENE_SLOT_PLAYER,
    SEGACD_SCENE_SLOT_PLAYER2,
    SEGACD_SCENE_SLOT_HUD_SCORE,
    SEGACD_SCENE_SLOT_HUD_LIVES,
    SEGACD_SCENE_SLOT_TITLE_CARD,
    SEGACD_SCENE_SLOT_HUD_RINGS,
    SEGACD_SCENE_SLOT_HUD_ICON,
    SEGACD_SCENE_SLOT_RESERVED
} SegaCDSceneSlotKind;

typedef struct {
    /* Coordinates retain signed 16-bit guest positions before SAT X wrapping.
     * World-relative source coordinates remove the normal +128 hardware bias. */
    int16_t x;
    int16_t y;
    int16_t native_sat_x_unwrapped;
    int16_t native_sat_y;
    uint16_t sat_x;       /* original 9-bit X rule, with encoded zero changed to 1 */
    uint16_t sat_y;       /* original guest word, without an extra mask */
    uint16_t tile_attribute;
    uint16_t object_ram_offset;
    uint32_t map_address;
    uint8_t size;
    uint8_t link;
    uint8_t priority;
    uint8_t object_id;
    uint8_t render_flags;
    uint8_t piece_index;
    uint8_t camera_mode;  /* 0 screen, 1 FG, 2 BG, 3 BG3 */
    uint8_t world_relative;
    uint8_t screen_relative;
    uint8_t horizontal_flip;
    uint8_t vertical_flip;
    uint8_t hud_slot;
    uint8_t slot_kind;
    uint8_t native_sat_emitted;
    uint8_t sat_index;    /* 0..79, or FF when native code reaches the cap first */
} SegaCDScenePiece;

typedef struct {
    SegaCDScenePiece *pieces;
    size_t piece_capacity;
    size_t piece_count;           /* all queued mapping pieces, including beyond SAT cap */
    size_t required_piece_capacity;
    size_t queued_object_count;
    uint16_t queue_counts[SEGACD_SCENE_QUEUE_LEVELS];
    uint8_t native_count;
    uint8_t native_sat[SEGACD_SCENE_SAT_BYTES];
    SegaCDSceneStatus status;
} SegaCDScene;

/* Bind caller-owned piece storage. dispose clears the scene descriptor and
 * never frees caller storage. */
void segacd_scene_init(SegaCDScene *scene,SegaCDScenePiece *pieces,
                       size_t piece_capacity);
void segacd_scene_dispose(SegaCDScene *scene);

/* Reads one source-backed DrawObjects queue snapshot. The caller owns the
 * immutable snapshots and output storage. No CPU or memory is modified. On
 * capacity failure, required_piece_capacity is exact and no partial piece
 * list is returned. Mapping addresses must resolve inside the supplied Word
 * RAM or Main RAM snapshot; unsupported external pointers fail explicitly. */
SegaCDSceneStatus segacd_scene_capture(const SegaCDSceneSnapshot *snapshot,
                                      SegaCDScene *scene);

const char *segacd_scene_status_name(SegaCDSceneStatus status);

#ifdef __cplusplus
}
#endif
#endif
