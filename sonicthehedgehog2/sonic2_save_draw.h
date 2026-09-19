#pragma once
#include "sonic2_campaign.h"
#include "sonic2_save_assets.h"
typedef struct S2SaveView {
    S2CampaignData data;
    unsigned selection, frame; /* 0=No Save, 1..8=files, 9=Delete */
    int cursor, scroll, erase, confirm, read_only;
    int delete_x;
    unsigned delete_frame;
    const char *notice;
} S2SaveView;
/* 320x224 donor composition centered in any output width. Host-only pixels. */
void s2_save_draw_line(const S2SaveAssets *assets,const S2SaveView *view,
                       int line,uint32_t *out,int width);
void s2_save_draw_notice(const S2SaveAssets *assets,const char *message,int line,uint32_t *out,int width);
