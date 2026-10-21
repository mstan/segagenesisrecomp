#include "sonic_cd_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #expr); exit(1); \
} } while (0)

enum { WORD_BYTES = 0x40000, MAIN_BYTES = 0x10000, OUTPUT_WIDTH = 400 };

static uint8_t word_ram[WORD_BYTES];
static uint8_t main_ram[MAIN_BYTES];
static uint8_t sprite_line[OUTPUT_WIDTH];
static uint32_t pixels[OUTPUT_WIDTH];
static SegaCDWorldMap world;
static GVDP vdp;

static void put_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void set_tile_solid(unsigned tile, uint8_t nibble)
{
    uint8_t packed = (uint8_t)((nibble << 4) | nibble);
    for (unsigned i = 0; i < 32; ++i) vdp.vram[tile * 32u + i] = packed;
}

static void set_tile_ramp(unsigned tile)
{
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned pair = 0; pair < 4; ++pair) {
            unsigned x = pair * 2;
            uint8_t a = (uint8_t)((x + 2 * y) % 14 + 1);
            uint8_t b = (uint8_t)((x + 1 + 2 * y) % 14 + 1);
            vdp.vram[tile * 32u + y * 4u + pair] = (uint8_t)((a << 4) | b);
        }
    }
}

static void set_block(unsigned block, uint16_t tl, uint16_t tr,
                      uint16_t bl, uint16_t br)
{
    uint16_t a[4] = {tl, tr, bl, br};
    for (unsigned i = 0; i < 4; ++i)
        put_be16(main_ram + 0x2000u + block * 8u + i * 2u, a[i]);
}

static void init_synthetic_world(void)
{
    memset(word_ram, 0, sizeof word_ram);
    memset(main_ram, 0, sizeof main_ram);
    memset(&world, 0, sizeof world);

    world.word_ram = word_ram;
    world.word_ram_size = sizeof word_ram;
    world.main_ram = main_ram;
    world.main_ram_size = sizeof main_ram;
    world.valid = 1;
    world.level_chunks_address = 0x200100;
    world.plane_width[SEGACD_WORLD_FOREGROUND] = 16;
    world.plane_width[SEGACD_WORLD_BACKGROUND] = 16;
    world.plane_height[SEGACD_WORLD_FOREGROUND] = 8;
    world.plane_height[SEGACD_WORLD_BACKGROUND] = 8;
    world.plane_valid[SEGACD_WORLD_FOREGROUND] = 1;
    world.plane_valid[SEGACD_WORLD_BACKGROUND] = 1;
    world.block_count = 3;
    world.max_chunk_id = 2;

    /* Synthetic source chunks: FG uses chunk 1 / block 0; BG uses chunk 2 /
       block 1. One BG block at authored pixel (350,50) uses block 2. */
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            main_ram[0xA000u + y * 0x80u + x] = 1;
            main_ram[0xA040u + y * 0x80u + x] = 2;
        }
    }
    for (unsigned i = 0; i < 256; ++i) {
        put_be16(word_ram + 0x100u + i * 2u, 0);
        put_be16(word_ram + 0x300u + i * 2u, 1);
    }
    /* Both source flips are encoded in the chunk word, independently of
       the four block tile attributes. */
    put_be16(word_ram + 0x100u, 0x1800);
    put_be16(word_ram + 0x300u + (3u * 16u + 5u) * 2u, 2);

    set_block(0, 0x2001, 0x2002, 0x2003, 0x2004);
    set_block(1, 0x4002, 0x4002, 0x4002, 0x4002);
    set_block(2, 0x4003, 0x4003, 0x4003, 0x4003);

    /* Standard BG section table: source's offscreen band then a visible
       dynamic-camera section. The actual scene tests reset it to static. */
    world.background_sections_address = 0x200080;
    world.background_section_count = 16;
    world.background_vertical_index_mask = 0xFFF0;
    world.background_source_valid = 1;
    world.background_camera_address[0] = 0x00FFF708;
    world.background_camera_address[1] = 0x00FFF708;
    world.background_camera_address[2] = 0x00FFF710;
    world.background_camera_address[3] = 0x00FFF718;
    world.background_camera_copy_address[0] = 0x00FF192E;
    world.background_camera_copy_address[1] = 0x00FF192E;
    world.background_camera_copy_address[2] = 0x00FF1936;
    world.background_camera_copy_address[3] = 0x00FF193E;
    memset(word_ram + 0x80, 0, 16);
    word_ram[0x80 + 4] = 4;
    put_be16(main_ram + 0x1936, 256);
    put_be16(main_ram + 0x193A, 512);

    memset(&vdp, 0, sizeof vdp);
    vdp.reg[1] = 0x40;  /* display enabled */
    vdp.reg[7] = 0;     /* black backdrop */
    vdp.reg[11] = 3;    /* per-scanline HScroll */
    vdp.reg[12] = 1;    /* H40, normal plane mode */
    vdp.reg[13] = 0x20; /* HScroll table at VRAM $8000 */
    vdp.reg[16] = 0x11; /* 64x64 tiles: 512x512 pixels */
    vdp.vsram[0] = 0;
    vdp.vsram[1] = 0;
    vdp.cram[0x11] = 0x000E; /* red */
    vdp.cram[0x12] = 0x000E;
    vdp.cram[0x13] = 0x000E;
    vdp.cram[0x14] = 0x000E;
    vdp.cram[0x22] = 0x00E0; /* green */
    vdp.cram[0x23] = 0x0E00; /* blue */
    vdp.cram[0x24] = 0x00EE; /* yellow */
    vdp.cram[0x25] = 0x0E0E; /* magenta */
    vdp.cram[0x26] = 0x0EE0; /* cyan */
    for (unsigned i = 0; i < 8; ++i) {
        unsigned line = i * 16u;
        unsigned offset = 0x8000u + line * 4u;
        put_be16(vdp.vram + offset, 0);
        put_be16(vdp.vram + offset + 2, 0);
    }
    for (unsigned tile = 1; tile <= 5; ++tile) set_tile_solid(tile, (uint8_t)tile);
    set_tile_ramp(6);
}

static void make_render(SegaCDRender *render, SegaCDScene *scene,
                        unsigned width)
{
    render->world = &world;
    render->scene = scene;
    render->width = width;
    render->sprite_line = sprite_line;
    render->sprite_line_bytes = sizeof sprite_line;
}

static void empty_scene(SegaCDScene *scene)
{
    memset(scene, 0, sizeof *scene);
    scene->status = SEGACD_SCENE_OK;
}

static int render_line(SegaCDRender *render, unsigned line)
{
    memset(pixels, 0xA5, sizeof pixels);
    memset(sprite_line, 0x5A, sizeof sprite_line);
    return segacd_render_scanline(render, &vdp, line, pixels, render->width);
}

static void test_world_block_flips(void)
{
    uint16_t attr = 0;
    CHECK(segacd_world_pattern_at_native(&world, SEGACD_WORLD_FOREGROUND,
                                        0, 0, &attr) == SEGACD_WORLD_OK);
    CHECK(attr == (uint16_t)(0x2004 | 0x0800 | 0x1000));
    CHECK(segacd_world_pattern_at_native(&world, SEGACD_WORLD_FOREGROUND,
                                        8, 0, &attr) == SEGACD_WORLD_OK);
    CHECK(attr == (uint16_t)(0x2003 | 0x0800 | 0x1000));
    CHECK(segacd_world_pattern_at_native(&world, SEGACD_WORLD_FOREGROUND,
                                        0, 8, &attr) == SEGACD_WORLD_OK);
    CHECK(attr == (uint16_t)(0x2002 | 0x0800 | 0x1000));
    CHECK(segacd_world_pattern_at_native(&world, SEGACD_WORLD_FOREGROUND,
                                        8, 8, &attr) == SEGACD_WORLD_OK);
    CHECK(attr == (uint16_t)(0x2001 | 0x0800 | 0x1000));
}

static void test_standard_and_ttz_band_bindings(void)
{
    SegaCDWorldBackgroundBand band;
    word_ram[0x80 + 4] = 4;
    CHECK(segacd_world_background_band_at(&world, 50, &band) == SEGACD_WORLD_OK);
    CHECK(band.mode == SEGACD_WORLD_BG_SECTIONS);
    CHECK(band.section_index == 4 && band.section_id == 4 && !band.static_x);
    CHECK(band.camera_address == 0x00FF1936 && band.camera_x == 256 &&
          band.camera_y == 512);

    word_ram[0x80 + 4] = 0;
    CHECK(segacd_world_background_band_at(&world, 50, &band) == SEGACD_WORLD_OK);
    CHECK(band.mode == SEGACD_WORLD_BG_SECTIONS && band.static_x &&
          band.section_id == 0);

    world.background_source_valid = 0;
    world.background_line_scroll_valid = 1;
    world.background_line_scroll_helper_address = 0x202DAC;
    world.background_line_scroll_table_address = 0x23F71C;
    put_be16(main_ram + 0xF704, 64);
    CHECK(segacd_world_background_band_at(&world, 50, &band) == SEGACD_WORLD_OK);
    CHECK(band.mode == SEGACD_WORLD_BG_LINE_SCROLL && band.static_x &&
          band.section_index == 50 && band.camera_address == 0x00FFF704 &&
          band.camera_y_copy_offset == 0xF704 && band.camera_y == 64 &&
          band.line_scroll_table_address == 0x23F71C);
    world.background_source_valid = 1;
    world.background_line_scroll_valid = 0;
}

static void test_priority_palette_and_margin(void)
{
    SegaCDScenePiece piece;
    SegaCDScene scene;
    SegaCDRender render;
    empty_scene(&scene);
    memset(&piece, 0, sizeof piece);
    piece.x = 100;
    piece.y = 50;
    piece.size = 0;
    piece.tile_attribute = 0x4003;
    scene.pieces = &piece;
    scene.piece_capacity = 1;
    scene.piece_count = 1;
    make_render(&render, &scene, OUTPUT_WIDTH);

    /* Plane A high wins over high Plane B and a normal sprite. */
    set_block(0, 0xA001, 0xA002, 0xA003, 0xA004);
    CHECK(render_line(&render, 50));
    CHECK(pixels[140] == 0xFFFF0000u);

    /* High Plane B wins over normal Plane A and a normal sprite. */
    set_block(0, 0x2001, 0x2002, 0x2003, 0x2004);
    set_block(1, 0xC002, 0xC002, 0xC002, 0xC002);
    CHECK(render_line(&render, 50));
    CHECK(pixels[140] == 0xFF00FF00u);

    /* With both planes low, a normal opaque sprite wins. */
    set_block(1, 0x4002, 0x4002, 0x4002, 0x4002);
    CHECK(render_line(&render, 50));
    CHECK(pixels[140] == 0xFF0000FFu);

    /* A high-priority sprite wins over high-priority planes. */
    set_block(0, 0xA001, 0xA002, 0xA003, 0xA004);
    set_block(1, 0xC002, 0xC002, 0xC002, 0xC002);
    piece.tile_attribute = 0xC003;
    CHECK(render_line(&render, 50));
    CHECK(pixels[140] == 0xFF0000FFu);

    /* A source-authored block at x=350 is visible in the right margin
       (output x=390 in a 400-pixel frame with a 40-pixel native offset). */
    empty_scene(&scene);
    piece.tile_attribute = 0;
    set_block(0, 0x2001, 0x2001, 0x2001, 0x2001);
    set_tile_solid(1, 0); /* transparent foreground */
    set_block(1, 0x4002, 0x4002, 0x4002, 0x4002);
    CHECK(render_line(&render, 50));
    CHECK(pixels[390] == 0xFF0000FFu);

    /* Plane A is opaque again; changing its CRAM entry between scanlines
       changes the DAC result without rebuilding world or scene state. */
    set_tile_solid(1, 1);
    set_block(0, 0xA001, 0xA001, 0xA001, 0xA001);
    CHECK(render_line(&render, 60));
    CHECK(pixels[140] == 0xFFFF0000u);
    vdp.cram[0x11] = 0x0E00;
    CHECK(render_line(&render, 61));
    CHECK(pixels[140] == 0xFF0000FFu);
}

static void test_sprite_layout_and_hud_margin(void)
{
    SegaCDScenePiece pieces[3];
    SegaCDScene scene;
    SegaCDRender render;
    empty_scene(&scene);
    memset(pieces, 0, sizeof pieces);
    scene.pieces = pieces;
    scene.piece_capacity = 3;
    scene.piece_count = 3;
    pieces[0].x = 64;
    pieces[0].y = 40;
    pieces[0].size = 5; /* 2x2 cells, Genesis SAT column-major tile order */
    pieces[0].tile_attribute = 0x4003;
    pieces[1].x = 0;
    pieces[1].y = 100;
    pieces[1].size = 0;
    pieces[1].tile_attribute = 0x4003;
    pieces[1].screen_relative = 1;
    pieces[1].hud_slot = 1;
    pieces[2].x = 0;
    pieces[2].y = 120;
    pieces[2].size = 0;
    pieces[2].tile_attribute = 0x4004;
    pieces[2].screen_relative = 1;
    set_tile_solid(1, 0);
    set_tile_solid(2, 0);
    make_render(&render, &scene, OUTPUT_WIDTH);
    CHECK(render_line(&render, 40));
    CHECK(pixels[104] == 0xFF0000FFu); /* top-left: tile 3 */
    CHECK(pixels[112] == 0xFFFF00FFu); /* top-right: tile 5 */
    CHECK(render_line(&render, 48));
    CHECK(pixels[104] == 0xFFFFFF00u); /* bottom-left: tile 4 */
    CHECK(pixels[116] == 0xFFFF00FFu); /* bottom-right: ramp tile 6 */
    CHECK(render_line(&render, 100));
    CHECK(pixels[0] == 0xFF0000FFu); /* HUD stays at the left edge */
    CHECK(render_line(&render, 120));
    CHECK(pixels[0] != 0xFFFFFF00u && pixels[40] == 0xFFFFFF00u);
    /* The same screen-relative origin is moved by the native margin for a
       title card, while HUD presentation remains pinned to the left edge. */
}

static void test_scene_queue_past_native_sat(void)
{
    static uint8_t scene_word_ram[WORD_BYTES];
    static uint8_t scene_main_ram[MAIN_BYTES];
    SegaCDScenePiece pieces[81];
    SegaCDScene scene;
    SegaCDSceneSnapshot snapshot;
    SegaCDRender render;

    memset(scene_word_ram, 0, sizeof scene_word_ram);
    memset(scene_main_ram, 0, sizeof scene_main_ram);
    for (unsigned i = 0; i < 81; ++i) {
        unsigned object = 0xD800u + i * SEGACD_SCENE_OBJECT_SIZE;
        unsigned queue = 0xAC00u + (i < 63 ? 0u : 0x80u);
        unsigned slot = i < 63 ? i : i - 63;
        uint8_t *obj = scene_main_ram + object;
        uint32_t mapping = 0x200100u + i * 5u;
        uint8_t *piece_data = scene_word_ram + (mapping - 0x200000u);
        put_be16(scene_main_ram + queue, (uint16_t)((i < 63 ? 63 : 18) * 2));
        put_be16(scene_main_ram + queue + 2u + slot * 2u, (uint16_t)object);
        obj[0] = 1;
        obj[1] = 0x20; /* static one-piece mapping, screen relative */
        put_be16(obj + 2, (uint16_t)(0x4000u | (i == 80 ? 4u : 3u)));
        put_be32(obj + 4, mapping);
        put_be16(obj + 8, (uint16_t)(i == 80 ? 424 : 128));
        put_be16(obj + 10, 144); /* screen y 16 after hardware bias */
        piece_data[0] = 0;
        piece_data[1] = 0;
        put_be16(piece_data + 2, 0);
        piece_data[4] = 0;
    }
    snapshot.word_ram = scene_word_ram;
    snapshot.word_ram_size = sizeof scene_word_ram;
    snapshot.word_ram_base = 0x200000;
    snapshot.main_ram = scene_main_ram;
    snapshot.main_ram_size = sizeof scene_main_ram;
    snapshot.main_ram_base = 0xFF0000;
    segacd_scene_init(&scene, pieces, 81);
    CHECK(segacd_scene_capture(&snapshot, &scene) == SEGACD_SCENE_OK);
    CHECK(scene.piece_count == 81 && scene.native_count == 80);
    CHECK(scene.pieces[79].native_sat_emitted && !scene.pieces[80].native_sat_emitted);
    CHECK(scene.pieces[80].x == 296 && scene.pieces[80].y == 16);

    make_render(&render, &scene, OUTPUT_WIDTH);
    CHECK(render_line(&render, 16));
    CHECK(pixels[336] == 0xFFFFFF00u); /* queued piece 81 appears at x=296+40 */
}

static void test_early_rejection_preserves_buffers(void)
{
    SegaCDScene scene;
    SegaCDRender render;
    empty_scene(&scene);
    make_render(&render, &scene, OUTPUT_WIDTH);
    memset(pixels, 0x3C, sizeof pixels);
    memset(sprite_line, 0xC3, sizeof sprite_line);
    vdp.reg[12] = 0; /* outside qualified H40 mode */
    CHECK(!segacd_render_scanline(&render, &vdp, 20, pixels, OUTPUT_WIDTH));
    for (unsigned i = 0; i < OUTPUT_WIDTH; ++i) {
        CHECK(pixels[i] == 0x3C3C3C3Cu);
        CHECK(sprite_line[i] == 0xC3);
    }
    vdp.reg[12] = 1;
}

int main(void)
{
    init_synthetic_world();
    test_world_block_flips();
    test_standard_and_ttz_band_bindings();
    test_priority_palette_and_margin();
    test_sprite_layout_and_hud_margin();
    test_scene_queue_past_native_sat();
    test_early_rejection_preserves_buffers();
    puts("sonic cd render synthetic tests passed");
    return 0;
}
