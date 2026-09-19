#include "sonic2_options.h"
#include "sonic2_party.h"
#include "sonic2_resources.h"
#include "sonic2_runtime.h"
#include "sonic2_save_menu.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include <stdio.h>
#include <string.h>
#if GENESIS_HAS_RECOMP_NET
#include "netplay/genesis_netplay.h"
#endif

static unsigned s_cursor;
static int s_ready;
static const char *s_notice;

void s2_options_load(const char *settings_path)
{
    s2_party_load(settings_path);
    s2_resources_load(settings_path);
    if (!s2_resource_verified(S2_RESOURCE_SK)) s2_party.s3k_enabled = 0;
    if (!s2_resource_save_assets()) s2_party.save_menu_enabled=0;
    s2_save_menu_load(settings_path);
    s2_runtime_load();
    s2_roster_validate(&s2_party.roster);
}

int s2_options_netplay_allowed(void)
{
    const S2Roster *r = &s2_party.roster;
    return !s2_party.save_menu_enabled && r->slots == 2 && !strcmp(r->character[0], "sonic") &&
        !strcmp(r->character[1], "tails");
}

int s2_options_hook(uint32_t pc)
{
#if GENESIS_HAS_RECOMP_NET
    /* A vanilla lobby retains the native menu on both peers. Offline settings
     * must never become an unsynchronized live roster during a match. */
    if (genesis_netplay_active()) return 0;
#endif
    /* REV01 audited routine boundaries. Replacing an RTS-style routine leaves
     * the synthetic return slot to the generated BSR/JSR caller, as native
     * generated RTS does. No manual stack pop, no patched ROM bytes. */
    if (pc == 0x3CF6) { /* TitleScreen_CheckIfChose2P, D0=selection */
        if ((uint8_t)g_cpu.D[0] == 1 && !s2_roster_vs_ready(&s2_party.roster)) {
            g_ram[0xF600] = 0x24;
            g_ram[0xFF8C] = 0;
            s_notice = "VS NEEDS PLAYER 1 AND PLAYER 2";
            return 1;
        }
        return 0;
    }
    if (g_ram[0xF600] != 0x24) return 0;
    switch (pc) {
    case 0x8FCC: /* initialization still owns art, background, palette and music */
        s_cursor = 0;
        s_ready = 0;
        s2_roster_validate(&s2_party.roster);
        return 0;
    case 0x9186: case 0x91F8: /* replace three-row box drawing only */
        return 1;
    case 0x90E0: {
        s_ready = 1;
        unsigned press = g_ram[0xF605] | g_ram[0xF607];
        if ((press & 3) == 1) s_cursor = (s_cursor + 5) % 6;
        if ((press & 3) == 2) s_cursor = (s_cursor + 1) % 6;
        int direction = (press & 12) == 4 ? -1 : (press & 12) == 8 ? 1 : 0;
        if (direction) {
            s_notice = NULL;
            if (!s_cursor) {
                unsigned slots = s2_party.roster.slots;
                slots = direction > 0 ? slots % 4 + 1 : (slots + 2) % 4 + 1;
                s2_roster_slots(&s2_party.roster, slots);
            } else if (s_cursor <= 4) {
                s2_roster_cycle(&s2_party.roster, s_cursor - 1, direction);
            }
        }
        /* B anywhere, or A/C on BACK: reuse the native Start exit branch. */
        if ((press & 16) || (s_cursor == 5 && (press & 96))) g_ram[0xF605] |= 128;
        return 1;
    }
    case 0x909A:
        s2_roster_validate(&s2_party.roster);
        if (!s2_party_save()) {
            s_notice = "SETTINGS SAVE FAILED";
            recomp_tail_call(0x9060);
        } else {
            s_ready = 0;
            s_notice = NULL;
            g_ram[0xF600] = 4; /* native TitleScreen, not Sega or a level */
        }
        return 1;
    default: return 0;
    }
}

/* Sample the actual font already loaded by MenuScreen. A=30, digits=16,
 * space=0, matching s2disasm's menu charset. Colors are host UI accents;
 * the background/palette animation is still the original Options screen. */
static unsigned glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned)(30 + c - 'A');
    if (c >= '0' && c <= '9') return (unsigned)(16 + c - '0');
    return 0;
}
static void text_row(const GVDP *v, int line, uint32_t *out, int width,
                     int x, int y, const char *text, uint32_t color)
{
    if (line < y || line >= y + 8) return;
    for (; *text; ++text, x += 8) {
        unsigned tile = glyph(*text);
        if (!tile) continue;
        for (int px = 0; px < 8; ++px) {
            if (x + px < 0 || x + px >= width) continue;
            unsigned byte = v->vram[tile * 32 + (line - y) * 4 + px / 2];
            unsigned nibble = px & 1 ? byte & 15 : byte >> 4;
            if (nibble) {
                uint32_t ink = genesis_dac_cram_to_argb(v->cram[nibble], GENESIS_DAC_NORMAL);
                unsigned light = (((ink >> 16) & 255) + ((ink >> 8) & 255) + (ink & 255)) / 3;
                /* The native font includes opaque black outlines/background.
                 * Tint its bright ink, not every nonzero palette index. */
                if (light > 32) out[x + px] = color;
            }
        }
    }
}
void s2_options_overlay(const GVDP *v, int line, uint32_t *out, int width)
{
#if GENESIS_HAS_RECOMP_NET
    if (genesis_netplay_active()) return;
#endif
    s2_runtime_overlay(v, line, out, width);
    if (s2_save_menu_overlay(line,out,width)) return;
    if (g_ram[0xF600] != 0x24 || !s_ready) return;
    int left = (width - 288) / 2;
    if (line >= 16 && line < 208) {
        for (int x = left; x < left + 288; ++x) if (x >= 0 && x < width) {
            int border = line == 16 || line == 207 || x == left || x == left + 287;
            out[x] = border ? 0xFF708ADCu : 0xFF101A42u;
        }
    }
    text_row(v,line,out,width,left+104,28,"OPTIONS",0xFFFFD85Cu);
    for (unsigned row = 0; row < 6; ++row) {
        int y = 52 + row * 22;
        int disabled = row >= 1 && row <= 4 && row > s2_party.roster.slots;
        uint32_t color = disabled ? 0xFF6C7390u : row == s_cursor ? 0xFFFFD85Cu : 0xFFF4F5FFu;
        if (row == s_cursor && line >= y && line < y + 8) {
            int size = 4 - (line < y + 4 ? y + 3 - line : line - y - 4);
            for (int x = 0; x <= size; ++x) if (left + 12 + x >= 0 && left + 12 + x < width)
                out[left + 12 + x] = color;
        }
        char label[32], value[40];
        value[0] = 0;
        if (!row) { strcpy(label,"PLAYERS"); snprintf(value,sizeof value,"%u",s2_party.roster.slots); }
        else if (row <= 4) {
            snprintf(label,sizeof label,"PLAYER %u",row);
            const S2Character *c = s2_character_find(s2_party.roster.character[row-1]);
            snprintf(value,sizeof value,"%s",c ? c->label : "NONE");
        } else strcpy(label,"BACK");
        text_row(v,line,out,width,left+28,y,label,color);
        text_row(v,line,out,width,left+176,y,value,color);
    }
    const char *hint = s_notice ? s_notice : "D PAD SELECT    START SAVE";
    text_row(v,line,out,width,(width-(int)strlen(hint)*8)/2,188,hint,0xFFB5C5F5u);
}
