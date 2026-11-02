#include "cdda.h"
#include <string.h>

static void clear_queue(SegaCDCdda *s)
{
    s->fifo_head=0;s->fifo_count=0;s->fifo_frame=0;s->source_anchored=0;
}

void segacd_cdda_reset(SegaCDCdda *s,uint64_t now,uint16_t fader_raw)
{
    if(!s)return;
    memset(s,0,sizeof *s);s->clock_now=now;s->epoch_origin=now;s->transport=SEGACD_CDDA_STOPPED;
    s->fader_raw_target=fader_raw;segacd_fader_reset(&s->fader,fader_raw);
    (void)segacd_cdda_set_modes(s,(fader_raw&0x000Cu)!=0,(fader_raw&0x0002u)!=0);
}
void segacd_cdda_play(SegaCDCdda *s){if(s&&s->transport!=SEGACD_CDDA_PLAYING){clear_queue(s);s->transport=SEGACD_CDDA_PLAYING;}}
void segacd_cdda_pause(SegaCDCdda *s){if(s){clear_queue(s);s->transport=SEGACD_CDDA_PAUSED;}}
void segacd_cdda_stop(SegaCDCdda *s){if(s){clear_queue(s);s->transport=SEGACD_CDDA_STOPPED;}}
void segacd_cdda_seek(SegaCDCdda *s){if(s)clear_queue(s);}
void segacd_cdda_clear_for_data(SegaCDCdda *s){if(s)clear_queue(s);}

SegaCDCddaResult segacd_cdda_set_modes(SegaCDCdda *s,int deemphasis,int double_speed)
{
    if(!s)return SEGACD_CDDA_INVALID;
    s->deemphasis=(uint8_t)!!deemphasis;s->double_speed=(uint8_t)!!double_speed;
    if(deemphasis)s->status|=SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED;
    if(double_speed)s->status|=SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED;
    return (deemphasis||double_speed)?SEGACD_CDDA_UNSUPPORTED:SEGACD_CDDA_OK;
}
SegaCDCddaResult segacd_cdda_set_fader_target_raw(SegaCDCdda *s,uint16_t raw)
{
    if(!s)return SEGACD_CDDA_INVALID;
    SegaCDCddaResult modes=segacd_cdda_set_modes(s,(raw&0x000Cu)!=0,(raw&0x0002u)!=0);
    if(modes!=SEGACD_CDDA_OK)return modes;
    s->fader_raw_target=raw;segacd_fader_set_target(&s->fader,raw);
    return SEGACD_CDDA_OK;
}

static int16_t load_le_s16(const uint8_t *p)
{
    uint16_t u=(uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
    int32_t value=u<0x8000u?(int32_t)u:(int32_t)u-0x10000;
    return (int16_t)value;
}
static void emit_tick(SegaCDCdda *s,uint64_t at,SegaCDCddaSample sink,void *user,uint64_t *emitted)
{
    int16_t left=0,right=0;
    ++s->total_sample_ticks;
    if(s->transport==SEGACD_CDDA_PLAYING && s->source_anchored) {
        if(!s->fifo_count) {s->status|=SEGACD_CDDA_STATUS_UNDERFLOW;++s->underflow_frames;return;}
        left=s->fifo[s->fifo_head][s->fifo_frame][0];right=s->fifo[s->fifo_head][s->fifo_frame][1];
        if(++s->fifo_frame==SEGACD_CDDA_SECTOR_FRAMES) {
            s->fifo_frame=0;s->fifo_head=(uint8_t)((s->fifo_head+1u)%SEGACD_CDDA_FIFO_SECTORS);--s->fifo_count;
        }
    } else ++s->silence_frames;
    segacd_fader_tick(&s->fader,&left,&right);
    if(sink)sink(user,at,left,right);
    ++*emitted;++s->delivered_frames;
}

SegaCDCddaResult segacd_cdda_advance_to(SegaCDCdda *s,uint64_t target,SegaCDCddaSample sink,void *user,uint64_t *frames_emitted)
{
    uint64_t emitted=0;int underflow=0;
    if(frames_emitted)*frames_emitted=0;
    if(!s||target<s->clock_now)return SEGACD_CDDA_INVALID;
    if(s->status&(SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED|SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED))return SEGACD_CDDA_UNSUPPORTED;
    while(s->clock_now<target) {
        uint64_t need=(SEGACD_CDDA_CLOCK-s->phase+SEGACD_CDDA_RATE-1u)/SEGACD_CDDA_RATE;
        uint64_t remaining=target-s->clock_now;
        if(need>remaining) {s->phase+=remaining*SEGACD_CDDA_RATE;s->clock_now=target;break;}
        s->clock_now+=need;s->phase=s->phase+need*SEGACD_CDDA_RATE-SEGACD_CDDA_CLOCK;
        uint64_t previous=s->underflow_frames;emit_tick(s,s->clock_now,sink,user,&emitted);
        if(s->underflow_frames!=previous)underflow=1;
    }
    if(frames_emitted)*frames_emitted=emitted;
    return underflow?SEGACD_CDDA_UNDERFLOW:SEGACD_CDDA_OK;
}

SegaCDCddaResult segacd_cdda_push_raw_sector(SegaCDCdda *s,uint64_t stamp,int32_t lba,const uint8_t raw[2352])
{
    if(!s||!raw||stamp!=s->clock_now)return SEGACD_CDDA_INVALID;
    if(s->status&(SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED|SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED))return SEGACD_CDDA_UNSUPPORTED;
    if(s->transport!=SEGACD_CDDA_PLAYING)return SEGACD_CDDA_NOT_PLAYING;
    if(s->fifo_count==SEGACD_CDDA_FIFO_SECTORS) {s->status|=SEGACD_CDDA_STATUS_OVERFLOW;return SEGACD_CDDA_OVERFLOW;}
    if(!s->source_anchored) {
        if(stamp!=s->clock_now)return SEGACD_CDDA_INVALID;
        s->phase=0;s->source_anchored=1;
    }
    uint8_t slot=(uint8_t)((s->fifo_head+s->fifo_count)%SEGACD_CDDA_FIFO_SECTORS);
    for(unsigned n=0;n<SEGACD_CDDA_SECTOR_FRAMES;++n) {
        s->fifo[slot][n][0]=load_le_s16(raw+n*4u);
        s->fifo[slot][n][1]=load_le_s16(raw+n*4u+2u);
    }
    s->fifo_lba[slot]=lba;++s->fifo_count;++s->sectors_accepted;
    return SEGACD_CDDA_OK;
}
