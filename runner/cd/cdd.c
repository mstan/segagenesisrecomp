/* Motor/TOC progression and seek pre-roll adapted from ares Mega-CD CDD, commit
 * 9408cb43d4948fc3ea6e152a307a34348df3fe04, ares/md/mcd/cdd.cpp and mcd.hpp.
 * Copyright (c) 2004-2025 ares team, Near et al.
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * Modifications: explicit C state, existing packet/media scheduler integration.
 * Two 75-Hz motor/TOC steps are a functional profile, not physical spin-up
 * or lead-in scan timing qualification. See docs/SEGA_CD_REUSE.md. */
#include "cdd.h"
#include <stdlib.h>
#include <string.h>
enum { CLOCK=12500000,HOCK=4,DTS=1 };
enum { STOP=0,PLAY=1,SEEK=2,SCAN=3,PAUSE=4,OPEN=5,CHECKSUM=6,ERROR=7,READ_TOC=9,TRACKING=10,NO_DISC=11,LEADOUT=12 };
/* TrackSkip has no publicly available Sega MCU algorithm. This functional
 * profile treats each count unit as one outward/inward spiral turn. ECMA-130
 * §8.6 places the user data zone at radii 25..58 mm; §11.3 gives 1.6 um
 * nominal pitch and §11.4 allows 1.20..1.40 m/s scan velocity. The model
 * uses the midpoint 1.30 m/s and integrates sectors/turn over a spiral with
 * constant nominal track pitch. Functional profile movement is 10,000 radial
 * turns/second (16 mm/s at the nominal 1.6 um pitch), chosen so BIOS TrackSkip
 * requests complete within game command timeouts. This is a deterministic
 * usability profile, not measured Sega drive timing. For discs longer than
 * the nominal radius range, the squared-radius mapping extrapolates so every
 * represented LBA remains reachable. ECMA geometry and this time scale do not
 * establish the original drive's motor speed or seek behavior. */
/* Radius is stored in tenths of a micrometre so the 1.6 um pitch is exact. */
enum { SKIP_INNER_RADIUS_TENTH_UM=250000,SKIP_PITCH_TENTH_UM=16,SKIP_TURNS_PER_SECOND=10000 };
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
    if(d->report_valid && d->toc.count && !d->tray_open) {
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
static uint64_t isqrt64(uint64_t n)
{
    uint64_t bit=(uint64_t)1<<62,result=0;
    while(bit>n)bit>>=2;
    while(bit) {
        if(n>=result+bit){n-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return result;
}
static uint64_t iceil_sqrt64(uint64_t n)
{
    uint64_t root=isqrt64(n);
    return root*root==n?root:root+1u;
}
static int32_t radius_to_lba(const SegaCDDrive*d,uint64_t radius_sq)
{
    const uint64_t inner_sq=(uint64_t)SKIP_INNER_RADIUS_TENTH_UM*SKIP_INNER_RADIUS_TENTH_UM;
    int64_t delta;
    int64_t lba;
    /* LBA = (r^2-r0^2) * pi * 75 / (v * pitch), with pi=355/113. */
    if(radius_sq>=inner_sq)delta=(int64_t)(radius_sq-inner_sq);
    else delta=-(int64_t)(inner_sq-radius_sq);
    lba=(delta*355*75)/((int64_t)13000000*SKIP_PITCH_TENTH_UM*113);
    if(lba<d->toc.track[0].index00)lba=d->toc.track[0].index00;
    if(lba>=d->toc.leadout)lba=d->toc.leadout-1;
    return (int32_t)lba;
}
static uint64_t lba_to_radius_sq(const SegaCDDrive*d,int32_t lba)
{
    const uint64_t inner_sq=(uint64_t)SKIP_INNER_RADIUS_TENTH_UM*SKIP_INNER_RADIUS_TENTH_UM;
    int64_t delta;
    if(lba<d->toc.track[0].index00)lba=d->toc.track[0].index00;
    if(lba>=d->toc.leadout)lba=d->toc.leadout-1;
    delta=((int64_t)lba*13000000*SKIP_PITCH_TENTH_UM*113)/(355*75);
    if(delta<0 && (uint64_t)(-delta)>inner_sq)return 0;
    if(delta<0)return inner_sq-(uint64_t)(-delta);
    return inner_sq+(uint64_t)delta;
}
static int32_t bounded_radius_to_lba(const SegaCDDrive*d,uint64_t radius)
{
    uint64_t min_radius=isqrt64(lba_to_radius_sq(d,d->toc.track[0].index00));
    uint64_t max_radius=iceil_sqrt64(lba_to_radius_sq(d,d->toc.leadout-1));
    if(radius<=min_radius)return d->toc.track[0].index00;
    if(radius>=max_radius)return d->toc.leadout-1;
    return radius_to_lba(d,radius*radius);
}
static uint64_t track_skip_deadline(const SegaCDDrive*d)
{
    uint64_t ticks=(uint64_t)d->track_skip_step+1u;
    return d->track_skip_origin_cycle+
        (ticks*(uint64_t)CLOCK+SKIP_TURNS_PER_SECOND-1u)/SKIP_TURNS_PER_SECOND;
}
static void cancel_track_skip(SegaCDDrive*d)
{
    d->track_skip_active=0;d->track_skip_deadline=0;d->track_skip_count=0;d->track_skip_step=0;
}
static void track_skip_step(SegaCDDrive*d)
{
    ++d->track_skip_step;
    uint64_t radius_sq=lba_to_radius_sq(d,d->track_skip_origin_lba);
    uint64_t radius=isqrt64(radius_sq);
    uint64_t min_radius=isqrt64(lba_to_radius_sq(d,d->toc.track[0].index00));
    uint64_t max_radius=iceil_sqrt64(lba_to_radius_sq(d,d->toc.leadout-1));
    uint64_t moved=(uint64_t)d->track_skip_step*SKIP_PITCH_TENTH_UM;
    if(d->track_skip_inward)radius=radius>moved?radius-moved:min_radius;
    else radius+=moved;
    if(radius<min_radius)radius=min_radius;
    if(radius>max_radius)radius=max_radius;
    d->lba=bounded_radius_to_lba(d,radius);
    if(d->track_skip_step>=d->track_skip_count || d->lba==d->track_skip_target_lba) {
        d->lba=d->track_skip_target_lba;cancel_track_skip(d);d->status=PAUSE;
    } else d->track_skip_deadline=track_skip_deadline(d);
    /* Mechanical motion does not replace an already completed command's
     * response packet. The next command/status poll latches a fresh reply. */
}
void segacd_cdd_reset(SegaCDDrive*d)
{
    SegaCDTOC toc=d->toc;uint64_t cycles=d->cycles;
    memset(d,0,sizeof *d);d->toc=toc;d->cycles=cycles;
    /* Initialized motor state remains stopped until a TOC/drive command.
     * An ordinary status-zero packet naturally has complement checksum F. */
    d->status=STOP;reply(d,0);
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
    d->toc=*toc;d->lba=0;d->status=d->tray_open?OPEN:STOP;d->motor_started=0;
    d->scan=0;d->seek_deadline=d->command_deadline=0;d->command_pending=0;cancel_track_skip(d);
    d->control&=(uint8_t)~DTS;d->report=d->report_valid=d->query_track=d->unsupported_command=0;
    reply(d,0);return 1;
}
void segacd_cdd_unmount(SegaCDDrive*d){memset(&d->toc,0,sizeof d->toc);d->status=d->tray_open?OPEN:NO_DISC;d->motor_started=0;d->scan=0;d->seek_deadline=0;cancel_track_skip(d);reply(d,0);}
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
    if(command==12){cancel_track_skip(d);d->tray_open=0;d->status=d->toc.count?STOP:NO_DISC;d->motor_started=1;reply(d,0);return;}
    if(command==13){cancel_track_skip(d);d->tray_open=1;d->status=OPEN;d->motor_started=0;d->seek_deadline=0;d->scan=0;reply(d,0);return;}
    if(d->tray_open || !d->toc.count){if(!d->tray_open)d->status=NO_DISC;reply(d,0);return;}
    switch(command) {
    case 1:
        cancel_track_skip(d);d->status=STOP;d->motor_started=1;d->scan=0;d->seek_deadline=0;d->report=d->report_valid=0;reply(d,0);return;
    case 2:
        if(p[3]>5 || (p[3]==5 && (p[4]>9 || p[5]>9 || p[4]*10u+p[5]<1 || p[4]*10u+p[5]>d->toc.count))){reply(d,ERROR);return;}
        /* TOCT starts lead-in/TOC acquisition from the initialized state;
         * status polls alone do not spin up the motor. CDD controller RE:
         * https://gendev.spritesmind.net/forum/viewtopic.php?p=35482 . */
        if(p[3]==4 && d->status==STOP){d->motor_started=1;d->status=READ_TOC;}
        d->report=p[3];d->report_valid=1;d->query_track=(uint8_t)(p[4]*10u+p[5]);reply(d,0);return;
    case 3:case 4: {
        int32_t target;if(!destination(p,&target) || target>=d->toc.leadout){reply(d,ERROR);return;}
        /* Explicit emulated mechanical delay; this provisional profile must
         * be measured/qualified, never replaced with host-speed loading. */
        /* The drive begins delivering the stream before the requested
         * address, allowing decoder setup/correction to settle. Match the
         * pinned ares SeekPlay/SeekPause three-sector pre-roll profile.
         * Lead-in before the represented first pregap has no raw image data. */
        target-=3;
        if(target<d->toc.track[0].index00)target=d->toc.track[0].index00;
        unsigned distance=(unsigned)abs(target-d->lba);
        cancel_track_skip(d);
        d->seek_target=target;d->after_seek=(uint8_t)(command==3?PLAY:PAUSE);
        d->seek_deadline=d->cycles+CLOCK/10+(uint64_t)distance*CLOCK/(75u*1000u);
        d->status=SEEK;d->scan=0;d->report=2;d->report_valid=1;reply(d,0);return;
    }
    case 6:cancel_track_skip(d);d->status=PAUSE;d->scan=0;d->seek_deadline=0;reply(d,0);return;
    case 7:cancel_track_skip(d);d->status=PLAY;d->scan=0;d->seek_deadline=0;d->report=2;d->report_valid=1;reply(d,0);return;
    case 8:case 9:cancel_track_skip(d);d->status=SCAN;d->scan=(int8_t)(command==8?10:-10);d->seek_deadline=0;reply(d,0);return;
    case 10: {
        if(d->status!=PLAY && d->status!=PAUSE && d->status!=LEADOUT){reply(d,ERROR);return;}
        if(d->report==4 || d->report==5)d->report=0;
        uint32_t count=((uint32_t)p[4]<<12)|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<4)|p[7];
        cancel_track_skip(d);d->scan=0;d->seek_deadline=0;
        d->track_skip_inward=(uint8_t)(p[2]!=0 || p[3]!=0);d->track_skip_origin_lba=d->lba;
        d->track_skip_count=count;d->track_skip_step=0;d->track_skip_origin_cycle=d->cycles;
        if(!count) {
            d->track_skip_target_lba=d->lba;d->status=PAUSE;reply(d,0);return;
        }
        uint64_t radius=isqrt64(lba_to_radius_sq(d,d->lba));
        uint64_t min_radius=isqrt64(lba_to_radius_sq(d,d->toc.track[0].index00));
        uint64_t max_radius=iceil_sqrt64(lba_to_radius_sq(d,d->toc.leadout-1));
        uint64_t delta=(uint64_t)count*SKIP_PITCH_TENTH_UM;
        if(d->track_skip_inward)radius=radius>delta?radius-delta:min_radius;
        else radius+=delta;
        if(radius<min_radius)radius=min_radius;
        if(radius>max_radius)radius=max_radius;
        d->track_skip_target_lba=bounded_radius_to_lba(d,radius);
        if(!count || d->track_skip_target_lba==d->lba) {
            d->lba=d->track_skip_target_lba;d->status=PAUSE;reply(d,0);return;
        }
        d->track_skip_active=1;d->track_skip_deadline=track_skip_deadline(d);d->status=TRACKING;reply(d,0);return;
    }
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
    if(d->track_skip_deadline && d->track_skip_deadline<next)next=d->track_skip_deadline;
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
        if(d->track_skip_active && d->track_skip_deadline==next)track_skip_step(d);
        if(d->fader_deadline==next)d->fader_deadline=0;
        if((d->control&HOCK) && frame_time(d)==next) {
            ++d->frames;
            if(d->status==STOP && d->motor_started) {
                d->status=d->toc.count && !d->tray_open?READ_TOC:NO_DISC;
                d->lba=0;
            } else if(d->status==READ_TOC) {
                d->status=PAUSE;d->lba=0;
            } else if(d->status==PLAY) {
                if(d->lba>=d->toc.leadout)d->status=LEADOUT;
                else {if(sink)sink(user,d->lba);++d->lba;}
            } else if(d->status==SCAN) {
                d->lba+=d->scan;if(d->lba<0)d->lba=0;if(d->lba>=d->toc.leadout){d->lba=d->toc.leadout;d->status=LEADOUT;}
            }
            /* Keep the completed command response stable until the host's
             * next packet. In particular, the STOP acknowledgement (zero)
             * must survive the following motor/TOC tick and IRQ delivery.
             * The pinned ares CDD similarly updates status[] in process(),
             * independently of mechanical state changes in clock(). */
            irq=1;
        }
    }
    d->cycles=until;return irq;
}
