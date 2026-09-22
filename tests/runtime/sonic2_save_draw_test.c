#include "../../sonicthehedgehog2/sonic2_save_draw.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    S2SaveAssets *assets=calloc(1,sizeof *assets);
    assert(assets);
    /* Synthetic donor font and counter art; no ROM required. */
    assets->palette[17]=0x00E;
    for (unsigned glyph=16;glyph<56;++glyph)
        memset(assets->tiles+(0x552+glyph)*32,0x11,32);
    for (unsigned tile=0x49A;tile<=0x4CD;++tile)
        memset(assets->tiles+tile*32,0x11,32);
    S2SaveView empty={0},filled={0};
    filled.selection=1;
    for (unsigned stage=0;stage<S2_CAMPAIGN_STAGES;++stage) {
        filled.data.slots[0]=(S2CampaignSlot){S2_SAVE_ACTIVE,(uint8_t)stage,0,3,0};
        unsigned caption_pixels=0,counter_pixels=0;
        for (int y=0;y<224;++y) {
            uint32_t baseline[320],actual[320];
            s2_save_draw_line(assets,&empty,y,baseline,320);
            s2_save_draw_line(assets,&filled,y,actual,320);
            for (int x=0;x<320;++x) if (actual[x]!=baseline[x]) {
                assert(x>=112 && x<176); /* eight-glyph card-stem width */
                assert((y>=80 && y<88) || (y>=144 && y<184));
                if (y<88) ++caption_pixels; else ++counter_pixels;
            }
        }
        assert(caption_pixels==6*64 && counter_pixels>0); /* ZONE + two digits */
    }
    /* Character-selection arrows must never appear, picked or unpicked. */
    memset(assets->tiles+0x100*32,0x11,4*32);
    assets->frames[15].count=2;
    assets->frames[15].pieces[0]=(S2MenuPiece){-8,-58,0x2100,2,2};
    assets->frames[15].pieces[1]=(S2MenuPiece){-8,2,0x2100,2,2};
    filled.data.slots[0].state=S2_SAVE_COMPLETE;
    for (int picked=0;picked<2;++picked) for (int y=0;y<224;++y) {
        uint32_t quiet[320],arrows[320];
        filled.replay_selected=picked;
        filled.frame=1; s2_save_draw_line(assets,&filled,y,quiet,320);
        filled.frame=17; s2_save_draw_line(assets,&filled,y,arrows,320);
        assert(!memcmp(quiet,arrows,sizeof quiet));
    }
    /* Completed cards keep static until picked; CLEAR occupies the same row. */
    assets->stages_ready=1;
    for (unsigned i=0;i<80*56;++i) assets->stages[19][i]=0xEEE;
    uint32_t unpicked[320],picked[320];
    filled.replay_selected=0; s2_save_draw_line(assets,&filled,16,unpicked,320);
    filled.replay_selected=1; s2_save_draw_line(assets,&filled,16,picked,320);
    assert(unpicked[144]!=picked[144]);
    filled.replay_selected=0; s2_save_draw_line(assets,&filled,80,unpicked,320);
    unsigned count=0;
    for (int x=112;x<176;++x) if (unpicked[x]!=0xFF000000) ++count;
    assert(count==40); /* CLEAR, no ZONE/ACT/FILE text */
    /* All seven emerald frames contribute independent visible pieces. */
    for (unsigned e=0;e<7;++e) {
        assets->frames[16+e].count=1;
        assets->frames[16+e].pieces[0]=(S2MenuPiece){(int16_t)(-28+e*8),-36,0x2100,1,1};
    }
    filled.data.slots[0].emeralds=0; s2_save_draw_line(assets,&filled,100,unpicked,320);
    filled.data.slots[0].emeralds=0x7F; s2_save_draw_line(assets,&filled,100,picked,320);
    for (unsigned e=0;e<7;++e) assert(unpicked[116+e*8]!=picked[116+e*8]);
    free(assets);
    puts("PASS numbered zone cards, CLEAR/static, counters, seven emeralds and no character arrows");
    return 0;
}
