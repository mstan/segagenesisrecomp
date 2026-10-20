#include "cdd.h"
#include <stdlib.h>
#include <string.h>
enum { CLOCK=12500000,HOCK=4,DTS=1 };
enum { PLAY=1,SEEK=2,SCAN=3,PAUSE=4,OPEN=5,CHECKSUM=6,ERROR=7,STOP=9,NO_DISC=11,LEADOUT=12 };
static uint8_t checksum(const uint8_t *p){unsigned n=0;for(unsigned i=0;i<9;++i)n+=p[i];return (uint8_t)(~n&15u);}
static void digits(uint8_t*p,unsigned n){p[0]=(uint8_t)((n/10)%10);p[1]=(uint8_t)(n%10);}
static void msf(uint8_t*p,int32_t frames)
{
    if(frames<0)frames=-frames;
    digits(p,(unsigned)frames/(60*75));digits(p+2,((unsigned)frames/75)%60);digits(p+4,(unsigned)frames%75);
}
static unsigned track_at(const SegaCDDrive *d)
{
    unsigned n=0;while(n+1<d->toc.count && d->lba>=d->toc.track[n+1].index00)++n;return n;
}
static void reply(SegaCDDrive*d,unsigned override)
{
    memset(d->rx,0,sizeof d->rx);d->rx[0]=(uint8_t)(override?override:d->status);d->rx[1]=d->report;
    /* The reset/status exchange precedes drive operations. Original US1.10
     * BIOS resident $18F6 requires a neutral, checksum-valid packet before
     * requesting TOC; an already-stopped/MSF reply makes it reset forever. */
    if(d->reset_handshake && !override){memset(d->rx,0,sizeof d->rx);d->rx[9]=15;return;}
    if(d->toc.count && !d->tray_open) {
        unsigned n=track_at(d);
        switch(d->report) {
        case 0:msf(d->rx+2,d->lba+150);d->rx[8]=(uint8_t)(d->toc.track[n].data?4:0);break;
        case 1:msf(d->rx+2,d->lba-d->toc.track[n].start);d->rx[8]=(uint8_t)(d->toc.track[n].data?4:0);break;
        case 2:digits(d->rx+2,n+1);break;
        case 3:msf(d->rx+2,d->toc.leadout+150);break;
        case 4:digits(d->rx+2,1);digits(d->rx+4,d->toc.count);break;
        case 5:
            n=d->query_track-1u;
            if(n<d->toc.count){msf(d->rx+2,d->toc.track[n].start+150);if(d->toc.track[n].data)d->rx[6]|=8;d->rx[8]=d->query_track%10u;}
            break;
        default:break;
        }
    }
    d->rx[9]=checksum(d->rx);
}
void segacd_cdd_reset(SegaCDDrive*d)
{
    SegaCDTOC toc=d->toc;uint64_t cycles=d->cycles;
    memset(d,0,sizeof *d);d->toc=toc;d->cycles=cycles;
    d->reset_handshake=1;d->status=d->toc.count?STOP:NO_DISC;reply(d,0);
}
void segacd_cdd_init(SegaCDDrive*d){memset(d,0,sizeof *d);segacd_cdd_reset(d);}
int segacd_cdd_mount(SegaCDDrive*d,const SegaCDTOC*toc)
{
    if(!d || !toc || !toc->count || toc->count>99 || toc->leadout<=0 || toc->leadout>100*60*75-151 ||
       toc->track[0].start!=0 || toc->track[toc->count-1].end!=toc->leadout)return 0;
    for(unsigned n=0;n<toc->count;++n) {
        const SegaCDTrack*t=&toc->track[n];
        if(t->index00 < -150 || t->index00>t->start || t->start>=t->end || t->end>toc->leadout || t->data>1 ||
           (n && (t->index00!=toc->track[n-1].end || t->start<=toc->track[n-1].start)))return 0;
    }
    d->toc=*toc;d->lba=0;d->status=d->tray_open?OPEN:STOP;
    d->scan=0;d->seek_deadline=d->command_deadline=0;d->command_pending=0;
    d->control&=(uint8_t)~DTS;d->report=d->query_track=d->unsupported_command=0;
    reply(d,0);return 1;
}
void segacd_cdd_unmount(SegaCDDrive*d){memset(&d->toc,0,sizeof d->toc);d->status=d->tray_open?OPEN:NO_DISC;d->scan=0;d->seek_deadline=0;reply(d,0);}
static int destination(const uint8_t*p,int32_t*lba)
{
    for(unsigned n=2;n<8;++n)if(p[n]>9)return 0;
    unsigned minute=p[2]*10u+p[3],second=p[4]*10u+p[5],frame=p[6]*10u+p[7];
    if(second>=60 || frame>=75)return 0;
    *lba=(int32_t)((minute*60+second)*75+frame)-150;return 1;
}
static void execute(SegaCDDrive*d)
{
    const uint8_t*p=d->pending;unsigned command=p[0];
    if(checksum(p)!=p[9]){reply(d,CHECKSUM);return;}
    if(p[1]){reply(d,ERROR);return;}
    if(command==0){reply(d,0);return;}
    d->reset_handshake=0;
    if(command==12){d->tray_open=0;d->status=d->toc.count?STOP:NO_DISC;reply(d,0);return;}
    if(command==13){d->tray_open=1;d->status=OPEN;d->seek_deadline=0;d->scan=0;reply(d,0);return;}
    if(d->tray_open || !d->toc.count){reply(d,0);return;}
    switch(command) {
    case 1:
        d->status=STOP;d->scan=0;d->seek_deadline=0;d->report=0;reply(d,0);d->rx[0]=0;d->rx[9]=checksum(d->rx);return;
    case 2:
        if(p[3]>5 || (p[3]==5 && (p[4]>9 || p[5]>9 || p[4]*10u+p[5]<1 || p[4]*10u+p[5]>d->toc.count))){reply(d,ERROR);return;}
        d->report=p[3];d->query_track=(uint8_t)(p[4]*10u+p[5]);reply(d,0);return;
    case 3:case 4: {
        int32_t target;if(!destination(p,&target) || target>=d->toc.leadout){reply(d,ERROR);return;}
        /* Explicit emulated mechanical delay; this provisional profile must
         * be measured/qualified, never replaced with host-speed loading. */
        unsigned distance=(unsigned)abs(target-d->lba);
        d->seek_target=target;d->after_seek=(uint8_t)(command==3?PLAY:PAUSE);
        d->seek_deadline=d->cycles+CLOCK/10+(uint64_t)distance*CLOCK/(75u*1000u);
        d->status=SEEK;d->scan=0;d->report=2;reply(d,0);return;
    }
    case 6:d->status=PAUSE;d->scan=0;d->seek_deadline=0;reply(d,0);return;
    case 7:d->status=PLAY;d->scan=0;d->seek_deadline=0;d->report=2;reply(d,0);return;
    case 8:case 9:d->status=SCAN;d->scan=(int8_t)(command==8?10:-10);d->seek_deadline=0;reply(d,0);return;
    default:d->unsupported_command=(uint8_t)command;reply(d,ERROR);return;
    }
}
uint8_t segacd_cdd_read(const SegaCDDrive*d,unsigned r)
{
    if(r==0x34)return (uint8_t)(d->fader_deadline?128:0);
    if(r==0x35)return 0;
    if(r==0x36)return (uint8_t)(d->status==PLAY && d->toc.count && !d->toc.track[track_at(d)].data?0:1);
    if(r==0x37)return d->control;
    if(r>=0x38 && r<0x42)return d->rx[r-0x38];
    if(r>=0x42 && r<0x4C)return d->tx[r-0x42];
    return 0;
}
void segacd_cdd_write(SegaCDDrive*d,unsigned r,uint8_t v)
{
    if(r==0x34){d->fader=(uint16_t)((d->fader&255u)|((unsigned)v<<8));return;}
    if(r==0x35){d->fader=(uint16_t)((d->fader&0xFF00u)|v);d->fader_deadline=d->cycles+CLOCK/10000;return;}
    if(r==0x36 || (r>=0x38 && r<0x42))return;
    if(r==0x37) {
        unsigned old=d->control;
        if((old&HOCK) && !(v&HOCK)){d->command_pending=0;d->command_deadline=0;}
        if(v&DTS){d->command_pending=0;d->command_deadline=0;}
        d->control=(uint8_t)((old&DTS)|(v&HOCK));
        if(!d->command_pending)d->control&=(uint8_t)~DTS;
        if(!(old&HOCK) && (v&HOCK)){d->frame_origin=d->cycles;d->frames=1;}
        return;
    }
    if(r>=0x42 && r<0x4C) {
        d->tx[r-0x42]=v&15u;
        if(r==0x4B && (d->control&HOCK)) {
            memcpy(d->pending,d->tx,sizeof d->pending);d->command_pending=1;
            d->control|=DTS;d->command_deadline=d->cycles+CLOCK/10000;
        }
    }
}
static uint64_t frame_time(const SegaCDDrive*d)
{return d->frame_origin+(d->frames/75u)*CLOCK+((d->frames%75u)*CLOCK+74u)/75u;}
uint64_t segacd_cdd_next_event(const SegaCDDrive*d)
{
    uint64_t next=(d->control&HOCK)?frame_time(d):UINT64_MAX;
    if(d->command_deadline && d->command_deadline<next)next=d->command_deadline;
    if(d->seek_deadline && d->seek_deadline<next)next=d->seek_deadline;
    if(d->fader_deadline && d->fader_deadline<next)next=d->fader_deadline;
    return next;
}
unsigned segacd_cdd_advance(SegaCDDrive*d,uint32_t clocks,SegaCDSectorSink sink,void*user)
{
    uint64_t until=d->cycles+clocks;unsigned irq=0;
    for(;;) {
        uint64_t next=segacd_cdd_next_event(d);
        if(next>until)break;
        d->cycles=next;
        if(d->command_deadline==next){d->command_pending=0;d->command_deadline=0;d->control&=(uint8_t)~DTS;execute(d);}
        if(d->seek_deadline==next){d->seek_deadline=0;d->lba=d->seek_target;d->status=d->after_seek;}
        if(d->fader_deadline==next)d->fader_deadline=0;
        if((d->control&HOCK) && frame_time(d)==next) {
            ++d->frames;
            if(d->status==PLAY) {
                if(d->lba>=d->toc.leadout)d->status=LEADOUT;
                else {if(d->lba>=0 && sink)sink(user,d->lba);++d->lba;}
            } else if(d->status==SCAN) {
                d->lba+=d->scan;if(d->lba<0)d->lba=0;if(d->lba>=d->toc.leadout){d->lba=d->toc.leadout;d->status=LEADOUT;}
            }
            reply(d,0);irq=1;
        }
    }
    d->cycles=until;return irq;
}
