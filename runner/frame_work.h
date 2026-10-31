/* Same-frame pure color preparation. Raw storage is borrowed only between
 * begin and wait; output/configuration are owned until take or the next begin.
 * A rejected/stale result never changes canonical staging or its history. */
#ifndef GENESIS_FRAME_WORK_H
#define GENESIS_FRAME_WORK_H
#include "video/color_lut.h"
typedef struct GenesisFrameWork GenesisFrameWork;
GenesisFrameWork *genesis_frame_work_create(void);
void genesis_frame_work_begin(GenesisFrameWork *,const uint32_t *raw,int stride,
    int width,int height,const ColorLut *lut,uint64_t content_token);
void genesis_frame_work_wait(GenesisFrameWork *);
/* storage_height describes the complete raw/staging extent, including inactive
 * filter history. Only the current active rectangle is committed, once. */
int genesis_frame_work_take(GenesisFrameWork *,const uint32_t *raw,int stride,
    int width,int height,const ColorLut *lut,uint64_t content_token,
    uint32_t *staging,int storage_height);
void genesis_frame_work_destroy(GenesisFrameWork *);
#endif
