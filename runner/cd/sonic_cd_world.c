#include "sonic_cd_world.h"

#include <limits.h>
#include <string.h>

enum {
    MMD_BASE = 0x200000,
    MMD_HEADER = 0x100,
    MAIN_RAM_LAYOUT = 0xA000,
    MAIN_RAM_BLOCKS = 0x2000,
    MAIN_RAM_LAYOUT_BYTES = 0x800,
    MAIN_RAM_BLOCK_BYTES = 0x2000,
    MAIN_RAM_BG1_CAMERA_Y = 0xF70C,
    MAX_LAYOUTS = 64,
    MAX_WIDTH = 256,
    MAX_HEIGHT = 256,
    MAX_CHUNKS = 0x80,
    CHUNK_BYTES = 0x200,
    CHUNK_GRID_BYTES = 16,
    BLOCK_BYTES = 8
};

typedef struct WorldLayout {
    uint16_t offset;
    uint16_t width;
    uint16_t height;
    const uint8_t *cells;
    uint32_t end_offset;
} WorldLayout;

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

static int span_ok(size_t total, uint64_t offset, uint64_t count) {
    return offset <= total && count <= (uint64_t)total - offset;
}

static unsigned graphics_chunk_id(uint8_t layout_cell);

typedef struct WorldBackgroundSource {
    uint32_t ids_address;
    uint32_t camera_address[4];
    uint32_t camera_copy_address[4];
    uint16_t id_count;
    uint16_t vertical_index_mask;
} WorldBackgroundSource;

typedef struct WorldBackgroundLineScrollSource {
    uint32_t helper_address;
    uint32_t table_address;
} WorldBackgroundLineScrollSource;

static int signature_at(const uint8_t *p, const uint8_t *fixed,
                        const uint8_t *mask, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (mask[i] && p[i] != fixed[i]) return 0;
    }
    return 1;
}

/* Return the unique data reference used by a source instruction signature.
   The masked bytes are the absolute 68k address operand. */
static int find_unique_reference(const uint8_t *data, size_t size,
                                 const uint8_t *fixed, const uint8_t *mask,
                                 size_t sig_len, size_t operand_offset,
                                 uint32_t *address_out) {
    size_t i, end = size < 0x10000 ? size : 0x10000;
    unsigned matches = 0;
    uint32_t found = 0;
    if (!data || !fixed || !mask || !address_out || sig_len > end ||
        operand_offset > sig_len || sig_len - operand_offset < 4) return 0;
    for (i = 0; i <= end - sig_len; ++i) {
        if (!signature_at(data + i, fixed, mask, sig_len)) continue;
        found = be32(data + i + operand_offset);
        if (++matches > 1) return 0;
    }
    if (matches != 1) return 0;
    *address_out = found;
    return 1;
}

static int mmd_offset(const SegaCDWorldMap *map, uint32_t address,
                      size_t count, size_t *offset_out) {
    uint64_t offset;
    if (address < MMD_BASE) return 0;
    offset = (uint64_t)address - MMD_BASE;
    if (!span_ok(map->word_ram_size, offset, count)) return 0;
    *offset_out = (size_t)offset;
    return 1;
}

/* The original DrawLevelBG1 code loads BGCameraSectIDs with an absolute
   LEA, skips its leading offscreen row, and dispatches through a PC-indexed
   CameraSects pointer table. Accept a table only when both code references,
   its four contiguous camera pointers, and every section byte agree. */
static int find_background_source(const uint8_t *data, size_t size,
                                  WorldBackgroundSource *out) {
    size_t i, code_end = size < 0x10000 ? size : 0x10000;
    size_t ids_offset = 0, camera_table_offset = 0;
    uint32_t ids_address = 0, camera_addresses[4] = {0, 0, 0, 0};
    uint32_t camera_copy_addresses[4] = {0, 0, 0, 0};
    unsigned lea_matches = 0, table_matches = 0, table_ref_matches = 0;
    unsigned index_matches = 0;
    uint16_t index_mask = 0;
    if (!data || !out || code_end < 32) return 0;

    for (i = 0; i + 14 <= code_end; ++i) {
        uint32_t address;
        int immediate_form;
        if (data[i] != 0x41 || data[i + 1] != 0xF9 ||
            data[i + 6] != 0xD0 || data[i + 7] != 0xFC ||
            data[i + 8] != 0x00 || data[i + 9] != 0x01) continue;
        immediate_form = (data[i + 10] == 0x78 && data[i + 11] == 0xF0) ||
            (data[i + 10] == 0x38 && data[i + 11] == 0x3C &&
             data[i + 12] == 0xFF && data[i + 13] == 0xF0);
        if (!immediate_form) continue;
        address = be32(data + i + 2);
        if (address < MMD_BASE || (uint64_t)address - MMD_BASE >= size) return 0;
        ids_address = address;
        ids_offset = (size_t)(address - MMD_BASE);
        if (++lea_matches > 1) return 0;
    }
    if (lea_matches != 1) return 0;

    for (i = ids_offset + 2; i + 16 <= size && i < ids_offset + 0x400; i += 2) {
        uint32_t p[4];
        size_t n;
        int ids_valid = 1;
        p[0] = be32(data + i);
        p[1] = be32(data + i + 4);
        p[2] = be32(data + i + 8);
        p[3] = be32(data + i + 12);
        if ((p[0] & 0xFFFF0000u) != 0x00FF0000u ||
            p[1] != p[0] || p[2] != p[0] + 8u || p[3] != p[0] + 16u) continue;
        n = i - ids_offset;
        if (!n || n > UINT16_MAX) continue;
        for (size_t j = 0; j < n; ++j) {
            uint8_t id = data[ids_offset + j];
            if (id != 0 && id != 2 && id != 4 && id != 6) {
                ids_valid = 0;
                break;
            }
        }
        if (!ids_valid) continue;
        camera_table_offset = i;
        memcpy(camera_addresses, p, sizeof(camera_addresses));
        ++table_matches;
    }
    if (table_matches != 1) return 0;

    /* DrawBGBlockRow's source lookup is a brief PC-indexed MOVEA.L through
       BGCameraSects. Its displacement must resolve to the pointer table just
       identified after BGCameraSectIDs. */
    for (i = 0; i + 4 <= code_end; ++i) {
        uint16_t extension;
        int displacement;
        int64_t target;
        if (data[i] != 0x26 || data[i + 1] != 0x7B) continue;
        extension = be16(data + i + 2);
        if ((extension & 0xFF00) != 0) continue;
        displacement = (int8_t)(extension & 0x00FF);
        target = (int64_t)i + 2 + displacement;
        if (target == (int64_t)camera_table_offset &&
            ++table_ref_matches > 1) return 0;
    }
    if (table_ref_matches != 1) return 0;

    /* The standard row lookup is MOVE.W $FFF70C,D0; ADD.W D4,D0;
       ANDI.W #mask,D0; ASR.W #4,D0; MOVE.B (A0,D0.W),D0; EXT.W D0;
       ADD.W D0,D0; MOVEA.L d8(PC,D0.W),A3. Record the mask from this exact
       source instruction sequence instead of assuming a global wrap rule. */
    for (i = 0; i + 24 <= code_end; ++i) {
        uint16_t mask, extension;
        int displacement;
        int64_t target;
        uint32_t pointers[4];
        size_t target_offset;
        if (data[i] != 0x30 || data[i + 1] != 0x38 ||
            data[i + 2] != 0xF7 || data[i + 3] != 0x0C ||
            data[i + 4] != 0xD0 || data[i + 5] != 0x44 ||
            data[i + 6] != 0x02 || data[i + 7] != 0x40 ||
            data[i + 10] != 0xE8 || data[i + 11] != 0x40 ||
            data[i + 12] != 0x10 || data[i + 13] != 0x30 ||
            data[i + 14] != 0x00 || data[i + 15] != 0x00 ||
            data[i + 16] != 0x48 || data[i + 17] != 0x80 ||
            data[i + 18] != 0xD0 || data[i + 19] != 0x40 ||
            data[i + 20] != 0x26 || data[i + 21] != 0x7B) continue;
        mask = be16(data + i + 8);
        extension = be16(data + i + 22);
        /* Brief PC-indexed D0.W, no scale; source row sizes are 16 pixels. */
        if ((mask & 0x000F) != 0 || (extension & 0xFF00) != 0) continue;
        displacement = (int8_t)(extension & 0x00FF);
        /* For d8(PC,D0.W), the 68000 PC base is the extension-byte address
           (the word immediately following opcode $267B). Resolve that exact
           local .CameraSects copy-pointer table and require the four source
           camera copies it names. */
        target = (int64_t)i + 22 + displacement;
        if (target < 0 || (uint64_t)target + 16 > size) continue;
        target_offset = (size_t)target;
        for (unsigned k = 0; k < 4; ++k)
            pointers[k] = be32(data + target_offset + k * 4);
        if ((pointers[0] & 0xFFFF0000u) != 0x00FF0000u ||
            pointers[1] != pointers[0] ||
            pointers[2] != pointers[0] + 8u ||
            pointers[3] != pointers[0] + 16u) continue;
        index_mask = mask;
        memcpy(camera_copy_addresses, pointers, sizeof(camera_copy_addresses));
        if (++index_matches > 1) return 0;
    }
    if (index_matches != 1) return 0;

    out->ids_address = ids_address;
    out->id_count = (uint16_t)(camera_table_offset - (size_t)(ids_address - MMD_BASE));
    out->vertical_index_mask = index_mask;
    memcpy(out->camera_address, camera_addresses, sizeof(camera_addresses));
    memcpy(out->camera_copy_address, camera_copy_addresses,
           sizeof(camera_copy_addresses));
    return out->id_count != 0;
}

/* TTZ uses a distinct original renderer. Its owned code loads an A3 helper,
   an A4 byte table, then reads the live $F7D8 phase. Keep it explicitly
   separate from section-table bands; callers still use the live VDP's
   per-line HScroll values for horizontal addressing. */
static int find_background_line_scroll_source(
    const uint8_t *data, size_t size, WorldBackgroundLineScrollSource *out) {
    size_t end = size < 0x10000 ? size : 0x10000;
    size_t i;
    unsigned matches = 0;
    WorldBackgroundLineScrollSource found = {0, 0};
    if (!data || !out || end < 16) return 0;
    for (i = 0; i <= end - 16; ++i) {
        uint32_t helper, table;
        uint64_t helper_offset, table_offset;
        if (data[i] != 0x47 || data[i + 1] != 0xF9 ||
            data[i + 6] != 0x49 || data[i + 7] != 0xF9 ||
            data[i + 12] != 0x16 || data[i + 13] != 0x38 ||
            data[i + 14] != 0xF7 || data[i + 15] != 0xD8) continue;
        helper = be32(data + i + 2);
        table = be32(data + i + 8);
        /* The helper is source-owned code; the table is byte-indexed by an
           8-bit accumulator in the original routine. Require the complete
           256-byte source extent to be present in this MMD. */
        if (helper < MMD_BASE || table < MMD_BASE) continue;
        helper_offset = (uint64_t)helper - MMD_BASE;
        table_offset = (uint64_t)table - MMD_BASE;
        if (!span_ok(size, helper_offset, 2) ||
            !span_ok(size, table_offset, 256)) continue;
        found.helper_address = helper;
        found.table_address = table;
        if (++matches > 1) return 0;
    }
    if (matches != 1) return 0;
    *out = found;
    return 1;
}

static int layout_is_valid(const SegaCDWorldMap *map, size_t offset,
                           WorldLayout *out) {
    unsigned width, height;
    uint64_t cells, end;
    if (!span_ok(map->word_ram_size, offset, 2)) return 0;
    width = (unsigned)map->word_ram[offset] + 1;
    height = (unsigned)map->word_ram[offset + 1] + 1;
    if (!width || width > MAX_WIDTH || !height || height > MAX_HEIGHT) return 0;
    cells = (uint64_t)width * height;
    end = (uint64_t)offset + 2 + cells;
    if (end > map->word_ram_size) return 0;
    out->width = (uint16_t)width;
    out->height = (uint16_t)height;
    out->cells = map->word_ram + offset + 2;
    out->end_offset = (uint32_t)end;
    return 1;
}

static int load_layouts(SegaCDWorldMap *map, size_t table_offset,
                        WorldLayout layouts[MAX_LAYOUTS], unsigned *count_out,
                        uint16_t offsets[MAX_LAYOUTS]) {
    unsigned count = 0, i, min_offset = UINT_MAX;
    WorldLayout test;
    /* The source table is a list of word offsets ending at its first target. */
    do {
        size_t entry = table_offset + (size_t)count * 2;
        uint16_t value;
        if (count >= MAX_LAYOUTS || !span_ok(map->word_ram_size, entry, 2)) return 0;
        value = be16(map->word_ram + entry);
        offsets[count++] = value;
        if (value < min_offset) min_offset = value;
    } while ((size_t)count * 2 < min_offset);
    if (count < 2 || (size_t)count * 2 > min_offset) return 0;
    for (i = 0; i < count; ++i) {
        size_t target = table_offset + offsets[i];
        unsigned j;
        if (!layout_is_valid(map, target, &test)) return 0;
        for (j = 0; j < i; ++j) {
            if (offsets[j] == offsets[i]) break;
        }
        if (j < i) {
            layouts[i] = layouts[j];
            layouts[i].offset = offsets[i];
            continue;
        }
        layouts[i] = test;
        layouts[i].offset = offsets[i];
    }
    *count_out = count;
    return 1;
}

static int active_plane_layout(const SegaCDWorldMap *map,
                               const WorldLayout layouts[MAX_LAYOUTS],
                               unsigned count, unsigned plane,
                               uint8_t *index_out) {
    size_t live_offset = MAIN_RAM_LAYOUT + (plane ? 0x40 : 0);
    unsigned i, found = MAX_LAYOUTS;
    if (!span_ok(map->main_ram_size, live_offset, MAIN_RAM_LAYOUT_BYTES - (plane ? 0x40 : 0))) return 0;
    for (i = 0; i < count; ++i) {
        unsigned y;
        int equal = 1;
        /* Sonic 1-compatible trailing table entries include several 1x1
           sentinel maps. They are not loaded as a playfield plane and often
           match a cleared live buffer's leading zero byte. */
        if ((layouts[i].width == 1 && layouts[i].height == 1) ||
            layouts[i].width > 64 || layouts[i].height > 16) continue;
        for (y = 0; y < layouts[i].height; ++y) {
            size_t live_row = live_offset + (size_t)y * 0x80;
            size_t src_row = (size_t)y * layouts[i].width;
            if (!span_ok(map->main_ram_size, live_row, layouts[i].width) ||
                memcmp(map->main_ram + live_row, layouts[i].cells + src_row,
                       layouts[i].width) != 0) {
                equal = 0;
                break;
            }
        }
        if (!equal) continue;
        if (found != MAX_LAYOUTS) {
            const WorldLayout *a = &layouts[found], *b = &layouts[i];
            if (a->width != b->width || a->height != b->height ||
                memcmp(a->cells, b->cells, (size_t)a->width * a->height) != 0) return 0;
        } else {
            found = i;
        }
    }
    if (found == MAX_LAYOUTS) return 0;
    *index_out = (uint8_t)found;
    return 1;
}

int segacd_world_map_init(SegaCDWorldMap *map,
                          const uint8_t *word_ram, size_t word_ram_size,
                          const uint8_t *main_ram, size_t main_ram_size) {
    static const uint8_t level_data_sig[] = {
        0x70,0x00,0x45,0xF9,0,0,0,0,0x2F,0x0A,0x58,0x8A,0x22,0x1A,
        0x02,0x81,0x00,0x3F,0xFF,0xFF
    };
    static const uint8_t level_data_mask[] = {
        1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1
    };
    static const uint8_t layouts_sig[] = {
        0x70,0x00,0xD0,0x41,0x43,0xF9,0,0,0,0,0x30,0x31,0x00,0x00,
        0x43,0xF1,0x00,0x00
    };
    static const uint8_t layouts_mask[] = {
        1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1
    };
    WorldLayout layouts[MAX_LAYOUTS];
    uint16_t layout_offsets[MAX_LAYOUTS];
    unsigned layout_count, i;
    size_t level_data_offset, layouts_offset;
    uint32_t level_data_address, layouts_address;
    uint32_t chunks_address, blocks_address;
    uint64_t max_chunk = 0;
    WorldBackgroundSource background_source;
    WorldBackgroundLineScrollSource line_scroll_source;
    int plane;

    if (!map) return 0;
    memset(map, 0, sizeof(*map));
    if (!word_ram || !main_ram || word_ram_size < MMD_HEADER ||
        main_ram_size < MAIN_RAM_LAYOUT + MAIN_RAM_LAYOUT_BYTES ||
        main_ram_size < MAIN_RAM_BLOCKS + MAIN_RAM_BLOCK_BYTES) return 0;
    map->word_ram = word_ram;
    map->word_ram_size = word_ram_size;
    map->main_ram = main_ram;
    map->main_ram_size = main_ram_size;
    if (be32(word_ram + 2) != 0 || be32(word_ram + 8) != MMD_BASE + MMD_HEADER) return 0;
    if (!find_unique_reference(word_ram, word_ram_size, level_data_sig,
                               level_data_mask, sizeof(level_data_sig), 4,
                               &level_data_address) ||
        !find_unique_reference(word_ram, word_ram_size, layouts_sig,
                               layouts_mask, sizeof(layouts_sig), 6,
                               &layouts_address)) return 0;
    if (!mmd_offset(map, level_data_address, 16, &level_data_offset) ||
        !mmd_offset(map, layouts_address, 4, &layouts_offset)) return 0;
    chunks_address = be32(word_ram + level_data_offset + 8) & 0x3FFFFF;
    blocks_address = be32(word_ram + level_data_offset + 4) & 0x3FFFFF;
    if ((chunks_address & 0xFFFF) != 0 || chunks_address < MMD_BASE) return 0;
    {
        size_t blocks_offset;
        uint16_t tile_count;
        if (!mmd_offset(map, blocks_address, 3, &blocks_offset)) return 0;
        tile_count = be16(word_ram + blocks_offset) & 0x7FFF;
        if (!tile_count || (uint32_t)tile_count * 32 > MAIN_RAM_BLOCK_BYTES) return 0;
        map->block_count = (uint16_t)(tile_count * 4);
    }
    map->level_chunks_address = chunks_address;
    map->level_layouts_address = layouts_address;
    if (!load_layouts(map, layouts_offset, layouts, &layout_count, layout_offsets)) return 0;

    /* GetBlockDataAbsXY first checks raw zero, then computes graphics chunk
       ((raw - 1) & 0x7f) + 1. Thus raw $80 names chunk 128, while $81 and $ff
       name chunks 1 and 127 with the loop marker set. */
    for (i = 0; i < layout_count; ++i) {
        unsigned j, n = (unsigned)layouts[i].width * layouts[i].height;
        for (j = 0; j < n; ++j) {
            unsigned id = graphics_chunk_id(layouts[i].cells[j]);
            if (id > max_chunk) max_chunk = id;
        }
    }
    if (max_chunk > MAX_CHUNKS) return 0;
    if (max_chunk && !mmd_offset(map, chunks_address,
                                 (size_t)max_chunk * CHUNK_BYTES, &level_data_offset)) return 0;
    map->max_chunk_id = (uint8_t)max_chunk;
    for (plane = 0; plane < 2; ++plane) {
        uint8_t layout_index;
        size_t live_offset = MAIN_RAM_LAYOUT + (plane ? 0x40 : 0);
        const WorldLayout *layout;
        if (!active_plane_layout(map, layouts, layout_count, (unsigned)plane,
                                 &layout_index)) return 0;
        layout = &layouts[layout_index];
        map->plane_layout_index[plane] = layout_index;
        map->plane_width[plane] = (uint8_t)layout->width;
        map->plane_height[plane] = (uint8_t)layout->height;
        map->plane_valid[plane] = 1;
        /* Each plane uses a 128-byte stride; require every row to fit its
           documented live levelLayout allocation. */
        if (!span_ok(map->main_ram_size, live_offset,
                     (size_t)(layout->height - 1) * 0x80 + layout->width)) return 0;
    }
    if (find_background_source(word_ram, word_ram_size, &background_source)) {
        map->background_sections_address = background_source.ids_address;
        memcpy(map->background_camera_address, background_source.camera_address,
               sizeof(map->background_camera_address));
        memcpy(map->background_camera_copy_address,
               background_source.camera_copy_address,
               sizeof(map->background_camera_copy_address));
        map->background_section_count = background_source.id_count;
        map->background_vertical_index_mask = background_source.vertical_index_mask;
        map->background_source_valid = 1;
    } else if (find_background_line_scroll_source(word_ram, word_ram_size,
                                                  &line_scroll_source)) {
        map->background_line_scroll_helper_address =
            line_scroll_source.helper_address;
        map->background_line_scroll_table_address =
            line_scroll_source.table_address;
        map->background_line_scroll_valid = 1;
    }
    map->valid = 1;
    return 1;
}

SegaCDWorldResult segacd_world_background_band_at(
    const SegaCDWorldMap *map, int32_t screen_y,
    SegaCDWorldBackgroundBand *band_out) {
    uint16_t camera_y, row_y, section_index;
    uint8_t section_id;
    unsigned camera_index;
    uint32_t camera_address;
    size_t section_offset, camera_x_offset, camera_y_offset;
    SegaCDWorldBackgroundBand result;

    if (!map || !map->valid || !band_out ||
        !span_ok(map->main_ram_size, MAIN_RAM_BG1_CAMERA_Y, 2))
        return SEGACD_WORLD_INVALID;
    if (map->background_line_scroll_valid) {
        memset(&result, 0, sizeof(result));
        result.mode = SEGACD_WORLD_BG_LINE_SCROLL;
        result.section_index = (uint16_t)screen_y;
        result.vertical_index_mask = 0x07FF;
        result.section_id = 0;
        result.static_x = 1;
        result.camera_address = 0x00FF0000u + 0xF704u;
        result.camera_y_copy_offset = 0xF704;
        result.camera_y = be16(map->main_ram + 0xF704);
        result.line_scroll_table_address =
            map->background_line_scroll_table_address;
        *band_out = result;
        return SEGACD_WORLD_OK;
    }
    if (!map->background_source_valid || !map->background_section_count ||
        !map->background_vertical_index_mask)
        return SEGACD_WORLD_UNAVAILABLE;
    if (!mmd_offset(map, map->background_sections_address,
                    map->background_section_count, &section_offset))
        return SEGACD_WORLD_UNAVAILABLE;

    camera_y = be16(map->main_ram + MAIN_RAM_BG1_CAMERA_Y);
    row_y = (uint16_t)(camera_y + (uint16_t)screen_y);
    /* DrawLevelBG1 increments A0 once before the visible row loop. The
       leading table entry is the offscreen band, so visible row zero uses 1. */
    section_index = (uint16_t)(1u + ((row_y & map->background_vertical_index_mask) >> 4));
    if (section_index >= map->background_section_count)
        return SEGACD_WORLD_UNAVAILABLE;
    section_id = map->word_ram[section_offset + section_index];
    if (section_id != 0 && section_id != 2 && section_id != 4 && section_id != 6)
        return SEGACD_WORLD_UNAVAILABLE;

    camera_index = (unsigned)section_id >> 1;
    camera_address = map->background_camera_copy_address[camera_index];
    if (camera_address < 0x00FF0000u || camera_address >= 0x01000000u)
        return SEGACD_WORLD_UNAVAILABLE;
    camera_x_offset = (size_t)(camera_address - 0x00FF0000u);
    camera_y_offset = camera_x_offset + 4;
    if (!span_ok(map->main_ram_size, camera_x_offset, 4) ||
        !span_ok(map->main_ram_size, camera_y_offset, 4))
        return SEGACD_WORLD_UNAVAILABLE;

    memset(&result, 0, sizeof(result));
    result.mode = SEGACD_WORLD_BG_SECTIONS;
    result.section_index = section_index;
    result.vertical_index_mask = map->background_vertical_index_mask;
    result.section_id = section_id;
    result.static_x = (uint8_t)(section_id == 0);
    result.camera_index = (uint8_t)camera_index;
    result.camera_address = camera_address;
    result.camera_x_copy_offset = (uint16_t)camera_x_offset;
    result.camera_y_copy_offset = (uint16_t)camera_y_offset;
    result.camera_x = be16(map->main_ram + camera_x_offset);
    result.camera_y = be16(map->main_ram + camera_y_offset);
    *band_out = result;
    return SEGACD_WORLD_OK;
}

static int floor_div_256(int32_t value, int32_t *quotient, unsigned *remainder) {
    int64_t v = value;
    int64_t q = v / 256;
    int64_t r = v % 256;
    if (r < 0) { r += 256; --q; }
    if (q < INT32_MIN || q > INT32_MAX) return 0;
    *quotient = (int32_t)q;
    *remainder = (unsigned)r;
    return 1;
}

static unsigned graphics_chunk_id(uint8_t layout_cell) {
    if (layout_cell == 0) return 0;
    return (unsigned)(((layout_cell - 1u) & 0x7Fu) + 1u);
}

static SegaCDWorldResult world_block_at(const SegaCDWorldMap *map,
                                         SegaCDWorldPlane plane,
                                         int32_t world_x, int32_t world_y,
                                         SegaCDWorldBlock *block_out,
                                         int native_addressing) {
    int32_t chunk_x, chunk_y;
    unsigned in_x, in_y;
    size_t layout_offset, chunk_offset, block_offset;
    unsigned chunk_cell, chunk_id, block_x, block_y, word_index;
    uint16_t word, block_id;
    const uint8_t *chunk;
    SegaCDWorldBlock result;
    if (!map || !map->valid || !block_out ||
        (plane != SEGACD_WORLD_FOREGROUND && plane != SEGACD_WORLD_BACKGROUND)) return SEGACD_WORLD_INVALID;
    if (native_addressing) {
        world_x = (int32_t)((uint32_t)world_x & 0x7FFFu);
        world_y = (int32_t)((uint32_t)world_y & 0x07FFu);
    }
    if (!floor_div_256(world_x, &chunk_x, &in_x) ||
        !floor_div_256(world_y, &chunk_y, &in_y)) return SEGACD_WORLD_OUTSIDE;
    if (chunk_x < 0 || chunk_y < 0 ||
        (native_addressing ? (chunk_x >= 128 || chunk_y >= 8) :
         (chunk_x >= map->plane_width[plane] || chunk_y >= map->plane_height[plane]))) return SEGACD_WORLD_OUTSIDE;
    layout_offset = MAIN_RAM_LAYOUT + (plane ? 0x40 : 0) + (size_t)chunk_y * 0x80 + (unsigned)chunk_x;
    if (!span_ok(map->main_ram_size, layout_offset, 1)) return SEGACD_WORLD_INVALID;
    chunk_cell = map->main_ram[layout_offset];
    chunk_id = graphics_chunk_id((uint8_t)chunk_cell);
    if (chunk_id == 0) return SEGACD_WORLD_EMPTY;
    if (chunk_id > map->max_chunk_id || chunk_id > MAX_CHUNKS) return SEGACD_WORLD_INVALID;
    if (!mmd_offset(map, map->level_chunks_address,
                    (size_t)chunk_id * CHUNK_BYTES, &chunk_offset)) return SEGACD_WORLD_INVALID;
    chunk_offset += (size_t)(chunk_id - 1) * CHUNK_BYTES;
    if (!span_ok(map->word_ram_size, chunk_offset, CHUNK_BYTES)) return SEGACD_WORLD_INVALID;
    chunk = map->word_ram + chunk_offset;
    block_x = in_x >> 4;
    block_y = in_y >> 4;
    word_index = block_y * CHUNK_GRID_BYTES + block_x;
    word = be16(chunk + word_index * 2);
    block_id = word & 0x03FF;
    block_offset = MAIN_RAM_BLOCKS + (size_t)block_id * BLOCK_BYTES;
    if (block_id >= map->block_count ||
        !span_ok(map->main_ram_size, block_offset, BLOCK_BYTES)) return SEGACD_WORLD_INVALID;
    memset(&result, 0, sizeof(result));
    result.word = word;
    result.block_id = block_id;
    result.chunk_id = (uint8_t)chunk_id;
    result.loop_marker = (uint8_t)(chunk_cell >> 7);
    result.x_flip = (uint8_t)((word >> 11) & 1);
    result.y_flip = (uint8_t)((word >> 12) & 1);
    {
        unsigned q;
        for (q = 0; q < 4; ++q) {
            unsigned src_x = (q & 1) ^ result.x_flip;
            unsigned src_y = ((q >> 1) & 1) ^ result.y_flip;
            uint16_t attr = be16(map->main_ram + block_offset + (src_y * 2 + src_x) * 2);
            if (result.x_flip) attr ^= 0x0800;
            if (result.y_flip) attr ^= 0x1000;
            result.pattern[q] = attr;
        }
    }
    *block_out = result;
    return SEGACD_WORLD_OK;
}

SegaCDWorldResult segacd_world_block_at(const SegaCDWorldMap *map,
                                         SegaCDWorldPlane plane,
                                         int32_t world_x, int32_t world_y,
                                         SegaCDWorldBlock *block_out) {
    return world_block_at(map, plane, world_x, world_y, block_out, 0);
}

SegaCDWorldResult segacd_world_block_at_native(const SegaCDWorldMap *map,
                                                SegaCDWorldPlane plane,
                                                int32_t world_x, int32_t world_y,
                                                SegaCDWorldBlock *block_out) {
    return world_block_at(map, plane, world_x, world_y, block_out, 1);
}

static SegaCDWorldResult world_pattern_at(const SegaCDWorldMap *map,
                                           SegaCDWorldPlane plane,
                                           int32_t world_x, int32_t world_y,
                                           uint16_t *pattern_out,
                                           int native_addressing) {
    SegaCDWorldBlock block;
    SegaCDWorldResult result;
    unsigned in_x, in_y, qx, qy;
    int32_t ignored;
    if (!pattern_out) return SEGACD_WORLD_INVALID;
    if (native_addressing) {
        world_x = (int32_t)((uint32_t)world_x & 0x7FFFu);
        world_y = (int32_t)((uint32_t)world_y & 0x07FFu);
    }
    result = world_block_at(map, plane, world_x, world_y, &block, native_addressing);
    if (result != SEGACD_WORLD_OK) return result;
    if (!floor_div_256(world_x, &ignored, &in_x) ||
        !floor_div_256(world_y, &ignored, &in_y)) return SEGACD_WORLD_OUTSIDE;
    qx = (in_x >> 3) & 1;
    qy = (in_y >> 3) & 1;
    *pattern_out = block.pattern[qy * 2 + qx];
    return SEGACD_WORLD_OK;
}

SegaCDWorldResult segacd_world_pattern_at(const SegaCDWorldMap *map,
                                           SegaCDWorldPlane plane,
                                           int32_t world_x, int32_t world_y,
                                           uint16_t *pattern_out) {
    return world_pattern_at(map, plane, world_x, world_y, pattern_out, 0);
}

SegaCDWorldResult segacd_world_pattern_at_native(const SegaCDWorldMap *map,
                                                  SegaCDWorldPlane plane,
                                                  int32_t world_x, int32_t world_y,
                                                  uint16_t *pattern_out) {
    return world_pattern_at(map, plane, world_x, world_y, pattern_out, 1);
}

int segacd_world_hscroll_word(const SegaCDWorldMap *map, unsigned scanline,
                              SegaCDWorldPlane plane, uint16_t *word_out) {
    size_t offset;
    if (!map || !map->valid || !word_out || scanline >= 224 ||
        (plane != SEGACD_WORLD_FOREGROUND && plane != SEGACD_WORLD_BACKGROUND)) return 0;
    offset = 0xCC00 + (size_t)scanline * 4 + (plane ? 2 : 0);
    if (!span_ok(map->main_ram_size, offset, 2)) return 0;
    *word_out = be16(map->main_ram + offset);
    return 1;
}
