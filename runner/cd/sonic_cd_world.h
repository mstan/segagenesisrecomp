#ifndef SEGACD_SONIC_CD_WORLD_H
#define SEGACD_SONIC_CD_WORLD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only view over one original Sonic CD level MMD and corresponding
   main-CPU RAM snapshot. word_ram is byte-for-byte mapped from $200000; the
   caller owns both snapshots and their lifetime. main_ram is mapped from
   $FF0000. No guest state is changed. */
typedef struct SegaCDWorldMap {
    const uint8_t *word_ram;
    size_t word_ram_size;
    const uint8_t *main_ram;
    size_t main_ram_size;
    uint32_t level_chunks_address;
    uint32_t level_layouts_address;
    uint32_t background_sections_address;
    uint32_t background_camera_address[4];
    uint32_t background_camera_copy_address[4];
    uint32_t background_line_scroll_helper_address;
    uint32_t background_line_scroll_table_address;
    uint16_t background_section_count;
    uint16_t background_vertical_index_mask;
    uint8_t background_source_valid;
    uint8_t background_line_scroll_valid;
    uint8_t plane_width[2];
    uint8_t plane_height[2];
    uint8_t plane_layout_index[2];
    uint8_t plane_valid[2];
    uint16_t block_count;
    uint8_t max_chunk_id;
    uint8_t valid;
} SegaCDWorldMap;

typedef enum SegaCDWorldPlane {
    SEGACD_WORLD_FOREGROUND = 0,
    SEGACD_WORLD_BACKGROUND = 1
} SegaCDWorldPlane;

typedef enum SegaCDWorldBackgroundMode {
    SEGACD_WORLD_BG_UNAVAILABLE = 0,
    SEGACD_WORLD_BG_SECTIONS = 1,
    SEGACD_WORLD_BG_LINE_SCROLL = 2
} SegaCDWorldBackgroundMode;

typedef enum SegaCDWorldResult {
    SEGACD_WORLD_OK = 0,
    SEGACD_WORLD_EMPTY = 1,
    SEGACD_WORLD_OUTSIDE = 2,
    SEGACD_WORLD_INVALID = 3,
    SEGACD_WORLD_UNAVAILABLE = 4
} SegaCDWorldResult;

/* Source-authored BG band binding. camera copy offsets are relative to the
   caller's $FF0000 main-RAM snapshot. section_id 0 uses static horizontal
   source placement; IDs 2/4/6 select the corresponding camera structure. */
typedef struct SegaCDWorldBackgroundBand {
    SegaCDWorldBackgroundMode mode;
    uint16_t section_index;
    uint16_t vertical_index_mask;
    uint8_t section_id;
    uint8_t static_x;
    uint8_t camera_index;
    uint8_t reserved;
    uint32_t camera_address;
    uint32_t line_scroll_table_address;
    uint16_t camera_x_copy_offset;
    uint16_t camera_y_copy_offset;
    uint16_t camera_x;
    uint16_t camera_y;
} SegaCDWorldBackgroundBand;

typedef struct SegaCDWorldBlock {
    uint16_t word;       /* original chunk word, including flips/solidity */
    uint16_t pattern[4]; /* TL,TR,BL,BR in world orientation */
    uint16_t block_id;
    uint8_t chunk_id;    /* graphics chunk ID decoded from raw layout byte */
    uint8_t loop_marker; /* source bit 7; rendering uses the low 7 bits */
    uint8_t x_flip;
    uint8_t y_flip;
} SegaCDWorldBlock;

/* Validate MMD signatures/tables and bind the source-owned level geometry to
   the live main-RAM layout at $FFA000 (FG) and $FFA040 (BG). This reader
   intentionally does not interpret zone-specific collision loop alternates. */
int segacd_world_map_init(SegaCDWorldMap *map,
                          const uint8_t *word_ram, size_t word_ram_size,
                          const uint8_t *main_ram, size_t main_ram_size);

/* Resolve the source-authored BG band for a scanline. Standard stages use the
   live BG1 camera Y at $FFF70C and the original section table. TTZ's qualified
   line-scroll mode returns a per-line binding with static plane-X addressing
   and the source helper/table addresses. Returns UNAVAILABLE when references
   are not uniquely proven. */
SegaCDWorldResult segacd_world_background_band_at(
    const SegaCDWorldMap *map, int32_t screen_y,
    SegaCDWorldBackgroundBand *band_out);

/* Resolve a signed world pixel coordinate using the active layout RAM,
   original 256x256-pixel chunks and 16x16-pixel blocks. Coordinates outside
   the source layout are reported as OUTSIDE; no speculative wrap is applied. */
SegaCDWorldResult segacd_world_block_at(const SegaCDWorldMap *map,
                                         SegaCDWorldPlane plane,
                                         int32_t world_x, int32_t world_y,
                                         SegaCDWorldBlock *block_out);

/* Resolve the same lookup with the original GetBlockDataAbsXY address masks:
   X wraps at 0x8000 pixels and Y at 0x0800 pixels. The bounded API above
   remains suitable for callers that want outside-map coordinates rejected. */
SegaCDWorldResult segacd_world_block_at_native(const SegaCDWorldMap *map,
                                                SegaCDWorldPlane plane,
                                                int32_t world_x, int32_t world_y,
                                                SegaCDWorldBlock *block_out);

/* Select the 8x8 VDP tile word for one pixel, respecting chunk-word block
   flips. VDP attribute flip bits remain encoded in the returned tile word. */
SegaCDWorldResult segacd_world_pattern_at(const SegaCDWorldMap *map,
                                           SegaCDWorldPlane plane,
                                           int32_t world_x, int32_t world_y,
                                           uint16_t *pattern_out);

SegaCDWorldResult segacd_world_pattern_at_native(const SegaCDWorldMap *map,
                                                  SegaCDWorldPlane plane,
                                                  int32_t world_x, int32_t world_y,
                                                  uint16_t *pattern_out);

/* The game's line-scroll RAM is at $FFCC00; each 4-byte line contains the
   raw big-endian plane-A word followed by plane-B. Return the 16-bit VDP
   scroll word without applying any presentation coordinate convention. */
int segacd_world_hscroll_word(const SegaCDWorldMap *map, unsigned scanline,
                              SegaCDWorldPlane plane, uint16_t *word_out);

#ifdef __cplusplus
}
#endif

#endif
