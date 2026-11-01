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

static int16_t owned_scale(int16_t sample,int32_t numerator,unsigned shift)
{
    if(!numerator)return 0;
    if((uint32_t)numerator==(1u<<shift))return sample;
    /* Even a directly restored uint16 field gives numerator<=16383. */
    int32_t product=(int32_t)sample*numerator;
    uint32_t magnitude=product<0?(uint32_t)(-product):(uint32_t)product;
    int32_t value=(int32_t)((magnitude+(1u<<(shift-1u)))>>shift);
    if(product<0)value=-value;
    return (int16_t)(value>32767?32767:value<-32768?-32768:value);
}
SegaCDCddaResult segacd_cdda_advance_owned(SegaCDCdda *s,uint64_t target,
    SegaCDCddaSample sink,void *user,uint64_t *frames_emitted,const SegaCDAudioOwnedSink *owned)
{
    if(frames_emitted)*frames_emitted=0;
    if(!s||!sink||!owned||owned->legacy!=sink||owned->legacy_user!=user||
       !owned->begin||!owned->commit||target<s->clock_now||s->phase>=SEGACD_CDDA_CLOCK||
       s->status&(SEGACD_CDDA_STATUS_DEEMPHASIS_UNSUPPORTED|SEGACD_CDDA_STATUS_DOUBLE_SPEED_UNSUPPORTED)||
       s->fifo_count>SEGACD_CDDA_FIFO_SECTORS||s->fifo_head>=SEGACD_CDDA_FIFO_SECTORS||
       s->fifo_frame>=SEGACD_CDDA_SECTOR_FRAMES)
        return segacd_cdda_advance_to(s,target,sink,user,frames_emitted);
    uint64_t delta=target-s->clock_now;
    if(delta>(UINT64_MAX-s->phase)/SEGACD_CDDA_RATE)
        return segacd_cdda_advance_to(s,target,sink,user,frames_emitted);
    uint64_t total=s->phase+delta*SEGACD_CDDA_RATE,ticks=total/SEGACD_CDDA_CLOCK;
    int data=s->transport==SEGACD_CDDA_PLAYING&&s->source_anchored;
    uint64_t available=s->fifo_count?(uint64_t)s->fifo_count*SEGACD_CDDA_SECTOR_FRAMES-s->fifo_frame:0;
    uint64_t emitted=data&&ticks>available?available:ticks;
    if(!emitted||emitted>UINT32_MAX)
        return segacd_cdda_advance_to(s,target,sink,user,frames_emitted);
    uint64_t first_delta=(SEGACD_CDDA_CLOCK-s->phase+SEGACD_CDDA_RATE-1u)/SEGACD_CDDA_RATE;
    uint64_t first=s->clock_now+first_delta;
    uint64_t last=s->clock_now+(emitted*SEGACD_CDDA_CLOCK-s->phase+SEGACD_CDDA_RATE-1u)/SEGACD_CDDA_RATE;
    SegaCDAudioWriteGrant grant;
    if(!owned->begin(owned->user,first,last,(size_t)emitted,&grant)||!grant.frame||grant.count!=emitted)
        return segacd_cdda_advance_to(s,target,sink,user,frames_emitted);
    uint64_t stamp=first;
    uint32_t phase=(uint32_t)(s->phase+first_delta*SEGACD_CDDA_RATE-SEGACD_CDDA_CLOCK);
    size_t done=0;
    while(done<(size_t)emitted) {
        size_t count=(size_t)emitted-done;
        unsigned field=s->fader.current_field,target_field=s->fader.target_field;
        int32_t numerator=0;unsigned shift=8;
        if(data) {
            unsigned remaining=SEGACD_CDDA_SECTOR_FRAMES-s->fifo_frame;
            if(count>remaining)count=remaining;
            numerator=(int32_t)(field<4u?field:field>>2);shift=field<4u?10u:8u;
            if(field!=target_field) {
                unsigned distance=field<target_field?target_field-field:field-target_field;
                unsigned plateau=field<4u?1u:field<target_field?4u-(field&3u):(field&3u)+1u;
                if(distance>=plateau&&count>plateau)count=plateau;
            }
        }
        const int16_t (*input)[2]=data?&s->fifo[s->fifo_head][s->fifo_frame]:NULL;
        for(size_t i=0;i<count;++i) {
            SegaCDAudioFrame *out=segacd_audio_grant_frame(&grant,done+i);
            out->subclock=stamp;
            out->left=data?owned_scale(input[i][0],numerator,shift):0;
            out->right=data?owned_scale(input[i][1],numerator,shift):0;
            /* After the first sample the rational phase is below RATE.
             * Replace repeated divides with the exact283/284-clock recurrence. */
            unsigned step=SEGACD_CDDA_CLOCK/SEGACD_CDDA_RATE;
            if(phase<SEGACD_CDDA_CLOCK%SEGACD_CDDA_RATE) {
                phase+=SEGACD_CDDA_RATE-SEGACD_CDDA_CLOCK%SEGACD_CDDA_RATE;++step;
            } else phase-=SEGACD_CDDA_CLOCK%SEGACD_CDDA_RATE;
            stamp+=step;
        }
        if(field<target_field) {
            unsigned distance=target_field-field;
            field+=count<distance?(unsigned)count:distance;
        } else if(field>target_field) {
            unsigned distance=field-target_field;
            field-=count<distance?(unsigned)count:distance;
        }
        s->fader.current_field=(uint16_t)field;
        if(data) {
            s->fifo_frame=(uint16_t)(s->fifo_frame+count);
            if(s->fifo_frame==SEGACD_CDDA_SECTOR_FRAMES) {
                s->fifo_frame=0;s->fifo_head=(uint8_t)((s->fifo_head+1u)%SEGACD_CDDA_FIFO_SECTORS);--s->fifo_count;
            }
        }
        done+=count;
    }
    s->clock_now=target;s->phase=total%SEGACD_CDDA_CLOCK;
    s->total_sample_ticks+=ticks;s->delivered_frames+=emitted;
    if(!data)s->silence_frames+=ticks;
    int underflow=ticks>emitted;
    if(underflow){s->underflow_frames+=ticks-emitted;s->status|=SEGACD_CDDA_STATUS_UNDERFLOW;}
    owned->commit(owned->user,&grant);
    if(frames_emitted)*frames_emitted=emitted;
    return underflow?SEGACD_CDDA_UNDERFLOW:SEGACD_CDDA_OK;
}
