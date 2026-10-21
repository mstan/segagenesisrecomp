/* Private-input native dual-program bring-up. Real main bus/VDP/CD memories
 * and coordinator; no Z80/audio rendering yet.
 * Missing devices/code halt loudly. This diagnostic is never a game release. */
#ifndef SEGACD_BIOS_PROBE_NO_CATALOGS
#include "segacd_bios.h"
#include "segacd_main_ram.h"
#include "segacd_sub.h"
#include "segacd_bios_extra_modules.h"
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
#include "segacd_sub_patch.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
#include "segacd_boot_main.h"
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
#include "segacd_boot_sub.h"
#endif
#endif
#include "segacd_coordinator.h"
#include "genesis_bus.h"
#include "genesis_dac.h"
#include "disc_image.h"
#include "m68k_floor.h"
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
#include "z80_native.h"
#include "sonic_cd_z80_step.h"
static SegaCDZ80 sound_cpu;
static uint64_t z80_callback_clock;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext,g_sndwatch[0x2000];
uint32_t g_rom_ext_size,g_audio_cycle_counter,g_68k_stamp_rebase;
unsigned long g_snd_frame;unsigned g_snd_pcz;
static SegaCD cd;static SegaCDCoordinator coordinator;static GenesisBus genesis;static GVDP vdp;
static uint64_t line_number,audio_events,dma_stalls;
static unsigned irq4,irq6,frame_width=320;
static uint32_t pixels[240][GVDP_MAX_WIDTH];
static const char *dump_directory;
static uint64_t start_frame=UINT64_MAX;
static uint64_t pad_reads,pressed_pad_reads;
#define PROBE_MAX_SCRIPT_EVENTS 64u
#define PROBE_MAX_FLOOR_PC_LIMIT 4096u
#define PROBE_SUB_VECTOR_BASE 0x5F28u
#define PROBE_SUB_VECTOR_BYTES 0x60u
#define PROBE_MAX_VECTOR_CHANGE_EVENTS 2048u
typedef struct { uint64_t frame,duration;uint16_t mask; } ProbePadEvent;
typedef struct {
    uint64_t time_before,time_after,native_before[2],native_after[2],floor_before[2],floor_after[2];
    uint32_t frame_before,frame_after,main_pc_before,main_pc_after,sub_pc_before,sub_pc_after;
    uint8_t memory_mode_before,memory_mode_after;
    M68kStepResult status;
} ProbeVectorChangeStep;
static ProbePadEvent pad_events[PROBE_MAX_SCRIPT_EVENTS];
static unsigned pad_event_count;
static unsigned floor_pc_limit=16,floor_pc_epoch;
static uint64_t floor_pc_count[2];
static uint8_t floor_pc_seen[2][0x100000];
static int pad_events_override_start;
static uint32_t snapshot_frames[PROBE_MAX_SCRIPT_EVENTS];
static unsigned snapshot_frame_count;
static int custom_snapshot_frames;
static void dump_frame_named(const char *,const char *);
static void dump(const char *,const char *,const void *,size_t);
static void capture_main_pc_state(uint32_t,uint16_t,uint64_t);
static int capture_audio,capture_error;
static int export_floor_pc_roots,pc_roots_error;
static int capture_sub_vector_changes,sub_vector_capture_error;
static uint32_t sub_vector_change_count;
static FILE *sub_vector_change_log;
static int main_pc_capture_enabled,main_pc_capture_matched;
static uint32_t main_pc_capture_target,main_pc_capture_observed;
static uint16_t main_pc_capture_opcode;
static uint64_t main_pc_capture_time;
static FILE *pcm_samples_file,*pcm_index_file,*cdda_raw_file,*cdda_index_file;
static FILE *cdda_samples_file,*cdda_samples_index_file;
static uint64_t cdda_sample_count;
static uint64_t pcm_epoch_subclock,pcm_event_count,cdda_event_count;
static uint64_t fader_event_count,peripheral_ready_events;
static uint64_t pcm_last_sample_index;
static unsigned pcm_epoch_id;
static int previous_fader_busy,previous_peripheral_ready;
static int parse_uint_span(const char *begin,const char *end,unsigned base,uint64_t *out)
{
    uint64_t value=0;
    if(begin==end)return 0;
    for(const char*p=begin;p<end;++p) {
        unsigned digit;
        if(*p>='0'&&*p<='9')digit=(unsigned)(*p-'0');
        else if(base==16&&*p>='a'&&*p<='f')digit=(unsigned)(*p-'a'+10);
        else if(base==16&&*p>='A'&&*p<='F')digit=(unsigned)(*p-'A'+10);
        else return 0;
        if(digit>=base || value>(UINT64_MAX-digit)/base)return 0;
        value=value*base+digit;
    }
    *out=value;return 1;
}
static int parse_decimal_span(const char *begin,const char *end,uint64_t *out)
{ return parse_uint_span(begin,end,10,out); }
static int parse_main_pc_target(const char *text,uint32_t *out)
{
    const char *begin=text,*end;
    unsigned base=10;
    uint64_t value;
    if(!text||!*text||*text=='+'||*text=='-')return 0;
    end=text+strlen(text);
    if(end-begin>=2&&begin[0]=='0'&&(begin[1]=='x'||begin[1]=='X')){base=16;begin+=2;}
    else if(end-begin>1&&begin[0]=='0')base=8;
    if(!parse_uint_span(begin,end,base,&value)||value>0xFFFFFFu||(value&1u))return 0;
    *out=(uint32_t)value;return 1;
}
static int parse_mask_span(const char *begin,const char *end,uint16_t *out)
{
    unsigned base=10;uint64_t value;
    if(end-begin>=2&&begin[0]=='0'&&(begin[1]=='x'||begin[1]=='X')){base=16;begin+=2;}
    if(!parse_uint_span(begin,end,base,&value)||value>0xFFFu)return 0;
    *out=(uint16_t)value;return 1;
}
static int parse_pad_events(const char *text)
{
    const char *cursor=text;
    if(!text||!*text)return 0;
    while(*cursor) {
        if(pad_event_count>=PROBE_MAX_SCRIPT_EVENTS)return 0;
        const char *end=strchr(cursor,',');if(!end)end=cursor+strlen(cursor);
        const char *c1=memchr(cursor,':',(size_t)(end-cursor));
        if(!c1)return 0;
        const char *c2=memchr(c1+1,':',(size_t)(end-c1-1));
        if(!c2||memchr(c2+1,':',(size_t)(end-c2-1)))return 0;
        uint64_t frame,duration;uint16_t mask;
        if(!parse_decimal_span(cursor,c1,&frame)||!parse_decimal_span(c1+1,c2,&duration)||
           !parse_mask_span(c2+1,end,&mask)||!duration||frame>UINT64_MAX-duration)return 0;
        pad_events[pad_event_count++]=(ProbePadEvent){frame,duration,mask};
        if(!*end)break;
        cursor=end+1;if(!*cursor)return 0;
    }
    return pad_event_count!=0;
}
static int parse_snapshot_frames(const char *text)
{
    const char *cursor=text;
    if(!text||!*text)return 0;
    custom_snapshot_frames=1;
    while(*cursor) {
        if(snapshot_frame_count>=PROBE_MAX_SCRIPT_EVENTS)return 0;
        const char *end=strchr(cursor,',');if(!end)end=cursor+strlen(cursor);
        uint64_t frame;
        if(!parse_decimal_span(cursor,end,&frame)||frame>UINT32_MAX)return 0;
        for(unsigned n=0;n<snapshot_frame_count;++n)if(snapshot_frames[n]==(uint32_t)frame)return 0;
        snapshot_frames[snapshot_frame_count++]=(uint32_t)frame;
        if(!*end)break;
        cursor=end+1;if(!*cursor)return 0;
    }
    return snapshot_frame_count!=0;
}
static int parse_probe_inputs(void)
{
    const char *events=getenv("SEGACD_PROBE_PAD_EVENTS");
    const char *override=getenv("SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START");
    const char *snapshots=getenv("SEGACD_PROBE_SNAPSHOT_FRAMES");
    const char *audio=getenv("SEGACD_PROBE_CAPTURE_AUDIO");
    const char *pc_limit=getenv("SEGACD_PROBE_FLOOR_PC_LIMIT");
    const char *main_pc=getenv("SEGACD_PROBE_CAPTURE_MAIN_PC");
    const char *pc_roots=getenv("SEGACD_PROBE_PC_ROOTS");
    const char *vector_changes=getenv("SEGACD_PROBE_VECTOR_CHANGES");
    if(events&&!parse_pad_events(events)){fprintf(stderr,"invalid SEGACD_PROBE_PAD_EVENTS (expected up to 64 frame:duration:12-bit-mask events)\n");return 0;}
    if(override) {
        if(!events||strcmp(override,"1")){fprintf(stderr,"SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START requires pad events and exact value 1\n");return 0;}
        pad_events_override_start=1;
    }
    if(snapshots&&!parse_snapshot_frames(snapshots)){fprintf(stderr,"invalid SEGACD_PROBE_SNAPSHOT_FRAMES (expected up to 64 unique decimal frames <= UINT32_MAX)\n");return 0;}
    if(audio){if(strcmp(audio,"1")){fprintf(stderr,"SEGACD_PROBE_CAPTURE_AUDIO accepts only 1\n");return 0;}capture_audio=1;}
    if(main_pc) {
        if(!parse_main_pc_target(main_pc,&main_pc_capture_target)) {
            fprintf(stderr,"SEGACD_PROBE_CAPTURE_MAIN_PC must be a base-0 even address <= 0xFFFFFF\n");return 0;
        }
        main_pc_capture_enabled=1;
    }
    if(pc_roots) {
        if(strcmp(pc_roots,"1")){fprintf(stderr,"SEGACD_PROBE_PC_ROOTS accepts only 1\n");return 0;}
        export_floor_pc_roots=1;
    }
    if(vector_changes) {
        if(strcmp(vector_changes,"1")){fprintf(stderr,"SEGACD_PROBE_VECTOR_CHANGES accepts only 1\n");return 0;}
        capture_sub_vector_changes=1;
    }
    if(pc_limit) {
        uint64_t value;
        if(!parse_decimal_span(pc_limit,pc_limit+strlen(pc_limit),&value)||!value||value>PROBE_MAX_FLOOR_PC_LIMIT) {
            fprintf(stderr,"SEGACD_PROBE_FLOOR_PC_LIMIT must be decimal 1..4096\n");return 0;
        }
        floor_pc_limit=(unsigned)value;
    }
    return 1;
}
static int open_sub_vector_change_log(void)
{
    if(!capture_sub_vector_changes)return 1;
    char path[4096];
    int length=snprintf(path,sizeof path,"%s/sub-vector-changes.jsonl",dump_directory);
    if(length<0||(size_t)length>=sizeof path) {
        fprintf(stderr,"Sub vector change capture failed: output path too long\n");
        sub_vector_capture_error=1;return 0;
    }
    sub_vector_change_log=fopen(path,"wb");
    if(!sub_vector_change_log) {
        fprintf(stderr,"Sub vector change capture failed: cannot open %s\n",path);
        sub_vector_capture_error=1;return 0;
    }
    return 1;
}
static int record_sub_vector_change(const uint8_t before[PROBE_SUB_VECTOR_BYTES],
                                    const uint8_t after[PROBE_SUB_VECTOR_BYTES],
                                    const ProbeVectorChangeStep *step)
{
    if(!capture_sub_vector_changes||!memcmp(before,after,PROBE_SUB_VECTOR_BYTES))return 1;
    if(sub_vector_change_count>=PROBE_MAX_VECTOR_CHANGE_EVENTS) {
        if(sub_vector_change_log) {
            (void)fprintf(sub_vector_change_log,"{\"error\":\"event_limit_exceeded\",\"limit\":%u}\n",PROBE_MAX_VECTOR_CHANGE_EVENTS);
            (void)fflush(sub_vector_change_log);
        }
        fprintf(stderr,"Sub vector change capture failed: event limit %u exceeded\n",PROBE_MAX_VECTOR_CHANGE_EVENTS);
        sub_vector_capture_error=1;return 0;
    }
    char name[80],path[4096];
    int name_length=snprintf(name,sizeof name,"sub-vector-changes-%06u.bin",sub_vector_change_count);
    int path_length=snprintf(path,sizeof path,"%s/%s",dump_directory,name);
    if(name_length<0||(size_t)name_length>=sizeof name||path_length<0||(size_t)path_length>=sizeof path) {
        fprintf(stderr,"Sub vector change capture failed: snapshot path too long\n");
        sub_vector_capture_error=1;return 0;
    }
    FILE *snapshot=fopen(path,"wb");
    if(!snapshot) {
        fprintf(stderr,"Sub vector change capture failed: cannot open %s\n",path);
        sub_vector_capture_error=1;return 0;
    }
    int ok=fwrite(after,1,PROBE_SUB_VECTOR_BYTES,snapshot)==PROBE_SUB_VECTOR_BYTES;
    if(fclose(snapshot))ok=0;
    if(!ok) {
        fprintf(stderr,"Sub vector change capture failed while writing %s\n",path);
        if(sub_vector_change_log) {
            (void)fprintf(sub_vector_change_log,"{\"event\":%u,\"error\":\"snapshot_write_failed\",\"file\":\"%s\"}\n",sub_vector_change_count,name);
            (void)fflush(sub_vector_change_log);
        }
        sub_vector_capture_error=1;return 0;
    }
    if(!sub_vector_change_log) {
        fprintf(stderr,"Sub vector change capture failed: event log is not open\n");
        sub_vector_capture_error=1;return 0;
    }
    if(fprintf(sub_vector_change_log,
        "{\"event\":%u,\"file\":\"%s\",\"sub_clock_before\":%llu,\"sub_clock_after\":%llu,"
        "\"frame_before\":%u,\"frame_after\":%u,\"memory_mode_before\":\"%02X\",\"memory_mode_after\":\"%02X\","
        "\"main_pc_before\":\"%06X\",\"main_pc_after\":\"%06X\",\"sub_pc_before\":\"%06X\",\"sub_pc_after\":\"%06X\","
        "\"native_retired_main\":%llu,\"native_retired_sub\":%llu,\"floor_retired_main\":%llu,\"floor_retired_sub\":%llu,"
        "\"step_status\":%u,\"writer_attribution\":\"not inferred; this record is a before/after diff for one coordinator step\",\"changes\":[",
        sub_vector_change_count,name,(unsigned long long)step->time_before,(unsigned long long)step->time_after,
        step->frame_before,step->frame_after,step->memory_mode_before,step->memory_mode_after,
        step->main_pc_before,step->main_pc_after,step->sub_pc_before,step->sub_pc_after,
        (unsigned long long)(step->native_after[0]-step->native_before[0]),
        (unsigned long long)(step->native_after[1]-step->native_before[1]),
        (unsigned long long)(step->floor_after[0]-step->floor_before[0]),
        (unsigned long long)(step->floor_after[1]-step->floor_before[1]),(unsigned)step->status)<0)ok=0;
    int first=1;
    for(unsigned offset=0;offset<PROBE_SUB_VECTOR_BYTES&&ok;++offset)if(before[offset]!=after[offset]) {
        if(fprintf(sub_vector_change_log,"%s{\"offset\":%u,\"before\":%u,\"after\":%u}",
                   first?"":",",offset,before[offset],after[offset])<0)ok=0;
        first=0;
    }
    if(ok&&fprintf(sub_vector_change_log,"]}\n")<0)ok=0;
    if(ok&&fflush(sub_vector_change_log))ok=0;
    if(!ok) {
        fprintf(stderr,"Sub vector change capture failed while writing event log\n");
        sub_vector_capture_error=1;return 0;
    }
    ++sub_vector_change_count;
    return 1;
}
static int close_sub_vector_change_log(void)
{
    if(!sub_vector_change_log)return !sub_vector_capture_error;
    int ok=fclose(sub_vector_change_log)==0;
    sub_vector_change_log=NULL;
    if(!ok) {
        fprintf(stderr,"Sub vector change capture failed closing event log\n");
        sub_vector_capture_error=1;
    }
    return ok&&!sub_vector_capture_error;
}
static uint16_t scripted_pad(uint64_t frame)
{
    uint16_t mask=0;
    for(unsigned n=0;n<pad_event_count;++n) {
        const ProbePadEvent *e=&pad_events[n];
        if(frame>=e->frame&&frame-e->frame<e->duration)mask|=e->mask;
    }
    if(!pad_events_override_start&&frame>=start_frame&&frame-start_frame<10)mask|=GPAD_START;
    return mask;
}
static int snapshot_requested(uint32_t frame)
{
    if(!custom_snapshot_frames)return frame==60||frame==120||frame==180||frame==240||frame==288||frame==360||frame==600||frame==900;
    for(unsigned n=0;n<snapshot_frame_count;++n)if(snapshot_frames[n]==frame)return 1;
    return 0;
}
static void record_floor_pc(unsigned cpu,uint32_t pc,uint64_t time,uint16_t opcode)
{
    uint32_t normalized=pc&0xFFFFFEu,index=normalized>>1;
    uint8_t bit=(uint8_t)(1u<<(index&7u));uint8_t *slot=&floor_pc_seen[cpu][index>>3];
    if(!(*slot&bit)) {
        *slot|=bit;uint64_t unique=++floor_pc_count[cpu];
        if(unique<=floor_pc_limit) {
            if(floor_pc_limit==16)printf("Floor PC cpu=%u pc=%06X time=%llu\n",cpu,normalized,(unsigned long long)time);
            else printf("Floor PC epoch=%u unique=%llu cpu=%u pc=%06X time=%llu opcode=%04X\n",floor_pc_epoch,(unsigned long long)unique,cpu,normalized,(unsigned long long)time,opcode);
        }
    }
}
static int write_floor_pc_roots_file(unsigned epoch,unsigned cpu,uint64_t *written_out)
{
    char name[80],path[4096];const char *cpu_name=cpu?"sub":"main";
    int name_size=snprintf(name,sizeof name,"floor-pc-roots-epoch-%03u-%s.txt",epoch,cpu_name);
    int path_size=snprintf(path,sizeof path,"%s/%s",dump_directory,name);
    if(name_size<0||(size_t)name_size>=sizeof name||path_size<0||(size_t)path_size>=sizeof path) {
        fprintf(stderr,"PC-root export failed: output path too long\n");return 0;
    }
    FILE *file=fopen(path,"wb");
    if(!file){fprintf(stderr,"PC-root export failed: cannot open %s\n",path);return 0;}
    uint64_t written=0;int ok=1;
    for(size_t byte=0;byte<sizeof floor_pc_seen[cpu]&&ok;++byte) {
        uint8_t bits=floor_pc_seen[cpu][byte];
        for(unsigned bit=0;bit<8;++bit)if(bits&(uint8_t)(1u<<bit)) {
            uint32_t pc=(uint32_t)(((byte*8u+(size_t)bit)<<1));
            if(fprintf(file,"0x%06X\n",pc)<0){ok=0;break;}
            ++written;
        }
    }
    if(fclose(file))ok=0;
    if(!ok||written!=floor_pc_count[cpu]) {
        fprintf(stderr,"PC-root export failed while writing %s\n",path);return 0;
    }
    *written_out=written;return 1;
}
static int write_floor_pc_roots(unsigned epoch,uint32_t frame,uint64_t time)
{
    if(!export_floor_pc_roots)return 1;
    uint64_t counts[2]={0,0};
    if(!write_floor_pc_roots_file(epoch,0,&counts[0])||
       !write_floor_pc_roots_file(epoch,1,&counts[1]))return 0;
    char name[80],path[4096];
    int name_size=snprintf(name,sizeof name,"floor-pc-roots-epoch-%03u.json",epoch);
    int path_size=snprintf(path,sizeof path,"%s/%s",dump_directory,name);
    if(name_size<0||(size_t)name_size>=sizeof name||path_size<0||(size_t)path_size>=sizeof path) {
        fprintf(stderr,"PC-root export failed: metadata path too long\n");return 0;
    }
    FILE *file=fopen(path,"wb");
    if(!file){fprintf(stderr,"PC-root export failed: cannot open %s\n",path);return 0;}
    int ok=fprintf(file,
        "{\n  \"schema\":\"SegaCDProbeFloorPCRoots1\",\n  \"epoch\":%u,\n  \"frame\":%u,\n  \"sub_clock\":%llu,\n  \"main_count\":%llu,\n  \"sub_count\":%llu,\n  \"main_file\":\"floor-pc-roots-epoch-%03u-main.txt\",\n  \"sub_file\":\"floor-pc-roots-epoch-%03u-sub.txt\",\n  \"scope\":\"Unique even PC addresses observed when this CPU retired exactly one floor operation in a coordinator step; native operations and ambiguous multi-retirement steps for that CPU, including nested WAIT, are omitted. This is not complete execution coverage.\"\n}\n",
        epoch,frame,(unsigned long long)time,(unsigned long long)counts[0],
        (unsigned long long)counts[1],epoch,epoch)>=0;
    if(fclose(file))ok=0;
    if(!ok){fprintf(stderr,"PC-root export failed while writing %s\n",path);return 0;}
    printf("Floor PC roots exported epoch=%u frame=%u time=%llu main=%llu sub=%llu\n",
        epoch,frame,(unsigned long long)time,(unsigned long long)counts[0],
        (unsigned long long)counts[1]);
    return 1;
}
static void reset_floor_pc_epoch(uint32_t frame,uint64_t time)
{
    if(!custom_snapshot_frames&&!export_floor_pc_roots)return;
    if(!write_floor_pc_roots(floor_pc_epoch,frame,time)){pc_roots_error=1;return;}
    printf("Floor PC epoch boundary frame=%u time=%llu prior_unique=%llu/%llu next_epoch=%u\n",frame,
        (unsigned long long)time,(unsigned long long)floor_pc_count[0],(unsigned long long)floor_pc_count[1],floor_pc_epoch+1);
    memset(floor_pc_seen,0,sizeof floor_pc_seen);floor_pc_count[0]=floor_pc_count[1]=0;++floor_pc_epoch;
}
static int input_selftest(void)
{
    const char *request=getenv("SEGACD_PROBE_INPUT_SELFTEST");
    if(!request)return 0;
    if(strcmp(request,"1")){fprintf(stderr,"SEGACD_PROBE_INPUT_SELFTEST accepts only 1\n");return -1;}
    for(unsigned n=0;n<pad_event_count;++n) {
        const ProbePadEvent *e=&pad_events[n];
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)e->frame,scripted_pad(e->frame));
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)(e->frame+e->duration-1),scripted_pad(e->frame+e->duration-1));
        printf("pad-boundary event=%u frame=%llu mask=%03X\n",n,(unsigned long long)(e->frame+e->duration),scripted_pad(e->frame+e->duration));
    }
    if(start_frame!=UINT64_MAX) {
        printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)start_frame,scripted_pad(start_frame));
        if(start_frame<=UINT64_MAX-9)printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)(start_frame+9),scripted_pad(start_frame+9));
        if(start_frame<=UINT64_MAX-10)printf("start-pulse frame=%llu mask=%03X\n",(unsigned long long)(start_frame+10),scripted_pad(start_frame+10));
    }
    if(custom_snapshot_frames) {
        printf("snapshot-frames count=%u",snapshot_frame_count);
        for(unsigned n=0;n<snapshot_frame_count;++n)printf(" %u",snapshot_frames[n]);
        printf("\n");
    }
    printf("floor-pc-limit=%u initial-epoch=0\n",floor_pc_limit);
    printf("main-pc-capture enabled=%u target=%06X matched=0\n",main_pc_capture_enabled,main_pc_capture_target);
    if(export_floor_pc_roots) {
        record_floor_pc(0,0x00FF1000u,0,0x4E75u);
        record_floor_pc(0,0x00123456u,0,0x4E71u);
        record_floor_pc(0,0x00FF1000u,0,0x4E75u);
        record_floor_pc(1,0x000005E8u,0,0x4E71u);
        record_floor_pc(1,0x0000A120u,0,0x4E75u);
        if(!write_floor_pc_roots(0,42,123456u))return -1;
    }
    if(capture_sub_vector_changes) {
        uint8_t before[PROBE_SUB_VECTOR_BYTES]={0},after[PROBE_SUB_VECTOR_BYTES];
        for(unsigned n=0;n<PROBE_SUB_VECTOR_BYTES;++n)after[n]=(uint8_t)(n*17u+3u);
        ProbeVectorChangeStep step={0};
        step.time_before=100;step.time_after=117;step.frame_before=1;step.frame_after=2;
        step.memory_mode_before=1;step.memory_mode_after=2;
        step.main_pc_before=0xFF1234;step.main_pc_after=0xFF1236;
        step.sub_pc_before=0x005F76;step.sub_pc_after=0x00000660;step.status=M68K_STEP_OK;
        step.native_before[0]=4;step.native_after[0]=5;step.floor_before[1]=7;step.floor_after[1]=8;
        if(!open_sub_vector_change_log()||!record_sub_vector_change(before,after,&step)||!close_sub_vector_change_log())return -1;
        printf("Sub vector change capture selftest wrote one 96-byte event\n");
    }
    return 1;
}
static void observe_input(uint32_t a,unsigned value)
{
    if((a&0xFFFFFEu)!=0xA10002u)return;
    ++pad_reads;if(genesis.pad[0]&GPAD_START) {
        if(pressed_pad_reads++<32)printf("P1 read frame=%llu address=%06X raw=%02X TH=%u pad=%04X pc=%06X\n",(unsigned long long)(line_number/262),a,value,!!(genesis.io_data[0]&64u),genesis.pad[0],coordinator.cpu[0].state.instruction_pc);
    }
}
static int media_read(void *image,int32_t lba,uint8_t raw[2352])
{return segacd_image_read(image,lba,raw);}
static void pcm_capture(void *unused,int16_t left,int16_t right)
{
    (void)unused;if(!capture_audio||capture_error)return;
    uint16_t l=(uint16_t)left,r=(uint16_t)right;
    const uint8_t bytes[4]={(uint8_t)l,(uint8_t)(l>>8),(uint8_t)r,(uint8_t)(r>>8)};
    uint64_t index=cd.pcm.samples;
    if(index<pcm_last_sample_index) {
        ++pcm_epoch_id;pcm_epoch_subclock=coordinator.device_time;pcm_last_sample_index=0;
        printf("PCM reset observed epoch=%u subclock_estimate=%llu\n",pcm_epoch_id,(unsigned long long)pcm_epoch_subclock);
    }
    if(fwrite(bytes,1,sizeof bytes,pcm_samples_file)!=sizeof bytes ||
       fprintf(pcm_index_file,"%u,%llu,%llu,%d,%d\n",pcm_epoch_id,(unsigned long long)index,
           (unsigned long long)(pcm_epoch_subclock+index*384u),(int)left,(int)right)<0) {
        fprintf(stderr,"audio capture: RF5C164 sample write failed\n");capture_error=1;return;
    }
    pcm_last_sample_index=index;++pcm_event_count;
}
static void cdda_capture(void *unused,uint64_t subclock,int32_t lba,const uint8_t raw[2352])
{
    (void)unused;if(!capture_audio||capture_error)return;
    if(fwrite(raw,1,2352,cdda_raw_file)!=2352 ||
       fprintf(cdda_index_file,"%llu,%llu,%d,%llu,2352\n",(unsigned long long)cdda_event_count,
           (unsigned long long)subclock,lba,(unsigned long long)(cdda_event_count*2352u))<0) {
        fprintf(stderr,"audio capture: CDDA sector write failed at LBA %d\n",lba);capture_error=1;return;
    }
    ++cdda_event_count;
}
static void cdda_sample_capture(void *unused,uint64_t subclock,int16_t left,int16_t right)
{
    (void)unused;if(!capture_audio||capture_error)return;
    uint16_t l=(uint16_t)left,r=(uint16_t)right;
    const uint8_t bytes[4]={(uint8_t)l,(uint8_t)(l>>8),(uint8_t)r,(uint8_t)(r>>8)};
    if(fwrite(bytes,1,sizeof bytes,cdda_samples_file)!=sizeof bytes ||
       fprintf(cdda_samples_index_file,"%llu,%llu,%d,%d,%04X,%04X,%u\n",
           (unsigned long long)cdda_sample_count,(unsigned long long)subclock,(int)left,(int)right,
           segacd_fader_current_raw(&cd.cdda.fader),cd.cdda.fader_raw_target,cd.cdda.source_anchored)<0) {
        fprintf(stderr,"audio capture: post-fader CDDA sample write failed\n");capture_error=1;return;
    }
    ++cdda_sample_count;
}
static int capture_open(const char *directory)
{
    char path[4096];
#define OPEN_CAPTURE(field,name,mode) do { \
    int n=snprintf(path,sizeof path,"%s/%s",directory,name); \
    if(n<0||(size_t)n>=sizeof path || !(field=fopen(path,mode))) { \
        fprintf(stderr,"audio capture: cannot open %s\n",name);return 0; \
    } \
} while(0)
    OPEN_CAPTURE(pcm_samples_file,"rf5c164-samples.s16le","wb");
    OPEN_CAPTURE(pcm_index_file,"rf5c164-sample-index.csv","w");
    OPEN_CAPTURE(cdda_raw_file,"cdda-sectors-2352.bin","wb");
    OPEN_CAPTURE(cdda_index_file,"cdda-sectors.csv","w");
    OPEN_CAPTURE(cdda_samples_file,"cdda-dac-samples.s16le","wb");
    OPEN_CAPTURE(cdda_samples_index_file,"cdda-dac-sample-index.csv","w");
#undef OPEN_CAPTURE
    fputs("pcm_epoch,sample_index,estimated_subclock,left_s16,right_s16\n",pcm_index_file);
    fputs("event,subclock,lba,raw_file_offset,raw_bytes\n",cdda_index_file);
    fputs("event,subclock,left_s16,right_s16,fader_after_sample_raw,target_raw,source_anchored\n",cdda_samples_index_file);
    if(ferror(pcm_index_file)||ferror(cdda_index_file)||ferror(cdda_samples_index_file)){fprintf(stderr,"audio capture: CSV header write failed\n");return 0;}
    return 1;
}
static int capture_close(void)
{
    FILE **files[]={&pcm_samples_file,&pcm_index_file,&cdda_raw_file,&cdda_index_file,&cdda_samples_file,&cdda_samples_index_file};int ok=1;
    for(unsigned n=0;n<sizeof files/sizeof *files;++n)if(*files[n]){if(fclose(*files[n]))ok=0;*files[n]=NULL;}
    if(!ok)fprintf(stderr,"audio capture: close/flush failed\n");
    return ok;
}
static void observe_fader_and_ready(uint64_t step_time,uint16_t old_word,uint64_t old_deadline)
{
    uint16_t word=cd.drive.fader;uint64_t deadline=cd.drive.fader_deadline;
    int busy=deadline!=0;
    if(word!=old_word || deadline!=old_deadline) {
        uint64_t event=++fader_event_count;
        if(event<=256)printf("FADER event=%llu subclock=%llu last_cpu=%u main_pc=%06X sub_pc=%06X word=%04X->%04X word_same=%u deadline=%llu->%llu busy=%u->%u ready=%u\n",
            (unsigned long long)event,(unsigned long long)step_time,coordinator.last_cpu,
            coordinator.cpu[0].state.instruction_pc&0xFFFFFFu,coordinator.cpu[1].state.instruction_pc&0xFFFFFFu,old_word,word,word==old_word,
            (unsigned long long)old_deadline,(unsigned long long)deadline,old_deadline!=0,busy,!busy);
    }
    if(busy!=previous_fader_busy) {
        if(fader_event_count<=256) {
            if(busy)printf("FADER busy-start subclock=%llu word=%04X deadline=%llu main_pc=%06X sub_pc=%06X\n",
                (unsigned long long)step_time,word,(unsigned long long)deadline,
                coordinator.cpu[0].state.instruction_pc&0xFFFFFFu,coordinator.cpu[1].state.instruction_pc&0xFFFFFFu);
            else printf("FADER ready subclock=%llu word=%04X deadline=%llu main_pc=%06X sub_pc=%06X\n",
                (unsigned long long)step_time,word,(unsigned long long)deadline,
                coordinator.cpu[0].state.instruction_pc&0xFFFFFFu,coordinator.cpu[1].state.instruction_pc&0xFFFFFFu);
        }
        previous_fader_busy=busy;
    }
    int ready=cd.sub_cycles>=cd.peripheral_ready;
    if(ready && !previous_peripheral_ready) {
        if(++peripheral_ready_events<=256)printf("SUB peripheral-ready event=%llu subclock=%llu deadline=%llu main_pc=%06X sub_pc=%06X\n",
            (unsigned long long)peripheral_ready_events,(unsigned long long)step_time,(unsigned long long)cd.peripheral_ready,
            coordinator.cpu[0].state.instruction_pc&0xFFFFFFu,coordinator.cpu[1].state.instruction_pc&0xFFFFFFu);
    }
    previous_peripheral_ready=ready;
}
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
static uint64_t sub_to_master(uint64_t clock)
{return (clock/12500000u)*53693175u+(clock%12500000u)*53693175u/12500000u;}
static void z80_stamp(uint64_t clock)
{z80_callback_clock=clock;g_snd_frame=(unsigned long)(sub_to_master(clock)/(262u*3420u));}
uint32_t machine_z80_stamp(void){return (uint32_t)(sub_to_master(z80_callback_clock)%(262u*3420u));}
uint32_t machine_z80_pc(void){return g_z80.pc;}
static uint8_t z80_local_read(void*u,uint16_t a,uint64_t clock)
{(void)u;z80_stamp(clock);return gbus_z80_read(&genesis,a);}
static void z80_local_write(void*u,uint16_t a,uint8_t v,uint64_t clock)
{(void)u;z80_stamp(clock);gbus_z80_write(&genesis,a,v);}
static uint16_t z80_bank(void*u){(void)u;return (uint16_t)genesis.z80_bank;}
static uint64_t sound_clock_limit(void*u)
{
    (void)u;
    if(sound_cpu.in_step)return segacd_z80_bus_time(&sound_cpu);
    return genesis.z80_reset_off&&!genesis.z80_busreq?sound_cpu.time:UINT64_MAX;
}
static void host_bus_stamp(void)
{
    uint64_t clock=coordinator.external_active?segacd_z80_bus_time(&sound_cpu):coordinator.time[0];
    uint64_t master=sub_to_master(clock);
    g_snd_frame=(unsigned long)(master/(262u*3420u));
    g_audio_cycle_counter=0;g_68k_stamp_rebase=(uint32_t)(master%(262u*3420u));
}
#else
uint32_t machine_z80_stamp(void){return (uint32_t)((line_number%262)*3420);}
uint32_t machine_z80_pc(void){return 0;}
static void host_bus_stamp(void){}
#endif
void snd_trace_68k_write(uint16_t o,uint8_t a,uint8_t b,int req,int reset){(void)o;(void)a;(void)b;(void)req;(void)reset;}
void snd_trace_z80(uint16_t o,uint8_t v,int w){(void)o;(void)v;(void)w;}
void snd_trace_busreq(const char*s,int v){(void)s;(void)v;}
void audio_event_push(uint32_t stamp,uint8_t port,uint8_t value){(void)stamp;(void)port;(void)value;++audio_events;}
void glue_charge_68k_stall(uint32_t cycles){dma_stalls+=cycles;segacd_coordinator_main_stall(&coordinator,cycles);}
static M68kStepBusResult read8(void*u,uint32_t a,uint8_t*v){(void)u;host_bus_stamp();*v=gbus_read8(&genesis,a);observe_input(a,*v);return M68K_BUS_OK;}
static M68kStepBusResult read16(void*u,uint32_t a,uint16_t*v){(void)u;host_bus_stamp();*v=gbus_read16(&genesis,a);observe_input(a,*v);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v){(void)u;host_bus_stamp();gbus_write8(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v){(void)u;host_bus_stamp();gbus_write16(&genesis,a,v);return M68K_BUS_OK;}
static M68kStepBusResult fetch(void*u,uint32_t a,uint16_t*v){(void)u;if(a<0xFF0000)return M68K_BUS_FAULT;*v=gbus_peek16(&genesis,a);return M68K_BUS_OK;}
static unsigned ack(void*u,unsigned level){(void)u;if(level==6)irq6=0;if(level==4)irq4=0;return M68K_STEP_AUTOVECTOR;}
static uint16_t dma_read(void*u,uint32_t a){(void)u;return segacd_coordinator_vdp_read16(&coordinator,a);}
static uint64_t line_tick(uint64_t line)
{
    uint64_t master=line*3420;
    return (master/53693175u)*12500000u+((master%53693175u)*12500000u+53693174u)/53693175u;
}
static uint64_t next_host_event(void*u){(void)u;return line_tick(line_number);}
static void advance_host(void*u,uint64_t now)
{
    (void)u;
    while(line_tick(line_number)<=now) {
        unsigned line=(unsigned)(line_number%262);
        if(!line) {
            uint64_t frame=line_number/262;
            genesis.pad[0]=scripted_pad(frame);
            if(frame<=UINT32_MAX&&snapshot_requested((uint32_t)frame)) {
                char name[64];snprintf(name,sizeof name,"frame-%04u.ppm",(unsigned)frame);
                dump_frame_named(dump_directory,name);
                if((!custom_snapshot_frames&&(frame==360||frame==900))||custom_snapshot_frames) {
                    snprintf(name,sizeof name,"frame-%04u-main-ram.bin",(unsigned)frame);dump(dump_directory,name,g_ram,sizeof g_ram);
                    snprintf(name,sizeof name,"frame-%04u-sub-prg.bin",(unsigned)frame);dump(dump_directory,name,cd.prg,sizeof cd.prg);
                    if(custom_snapshot_frames) {
                        snprintf(name,sizeof name,"frame-%04u-word-ram.bin",(unsigned)frame);dump(dump_directory,name,cd.word,sizeof cd.word);
                        snprintf(name,sizeof name,"frame-%04u-vdp-registers.bin",(unsigned)frame);dump(dump_directory,name,vdp.reg,sizeof vdp.reg);
                        snprintf(name,sizeof name,"frame-%04u-cram-host-u16.bin",(unsigned)frame);dump(dump_directory,name,vdp.cram,sizeof vdp.cram);
                        snprintf(name,sizeof name,"frame-%04u-vsram-host-u16.bin",(unsigned)frame);dump(dump_directory,name,vdp.vsram,sizeof vdp.vsram);
                        snprintf(name,sizeof name,"frame-%04u-z80-ram.bin",(unsigned)frame);dump(dump_directory,name,genesis.z80_ram,sizeof genesis.z80_ram);
                    }
                    printf("snapshot frame=%llu time=%llu main=%06X sub=%06X drive=%u sectors=%llu flags=%02X/%02X memory_mode=%02X fader=%04X fader_busy=%u fader_deadline=%llu peripheral_ready=%llu\n",
                        (unsigned long long)frame,(unsigned long long)now,coordinator.cpu[0].state.regs.PC,coordinator.cpu[1].state.regs.PC,
                        cd.drive.status,(unsigned long long)cd.media_sectors,cd.flags[0],cd.flags[1],cd.memory_mode,cd.drive.fader,
                        cd.drive.fader_deadline!=0,(unsigned long long)cd.drive.fader_deadline,(unsigned long long)cd.peripheral_ready);
                    reset_floor_pc_epoch((uint32_t)frame,now);
                }
            }
        }
        if(line_number && (line+261)%262<240) {
            unsigned row=(line+261)%262;uint8_t indices[GVDP_MAX_WIDTH];
            int width=gvdp_render_scanline(&vdp,(int)row,indices);
            if(width>0 && width<=GVDP_MAX_WIDTH) {
                frame_width=(unsigned)width;
                for(int x=0;x<width;++x){unsigned index=indices[x];pixels[row][x]=index<192?genesis_dac_cram_to_argb(vdp.cram[index%64],(int)(index/64)):0xFF000000u;}
            }
        }
        unsigned interrupts=gvdp_begin_scanline(&vdp,(int)line);
        if(interrupts&GVDP_IRQ_VBLANK){irq6=1;
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
            segacd_z80_irq(&sound_cpu);
#endif
        }
        if(interrupts&GVDP_IRQ_HBLANK)irq4=1;
        g_snd_frame=(unsigned long)(line_number/262);++line_number;
    }
    segacd_coordinator_main_irq(&coordinator,irq6?6u:irq4?4u:0u);
}
static void dump(const char*dir,const char*name,const void*data,size_t bytes)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",dir,name);
    if(n<0 || (size_t)n>=sizeof path){fprintf(stderr,"dump path too long\n");exit(4);}
    FILE*f=fopen(path,"wb");if(!f || fwrite(data,1,bytes,f)!=bytes || fclose(f)){fprintf(stderr,"dump failed: %s\n",name);exit(4);}
}
static void capture_main_pc_state(uint32_t pc,uint16_t opcode,uint64_t time)
{
    char name[80];
    snprintf(name,sizeof name,"main-pc-%06X-main-ram.bin",pc);dump(dump_directory,name,g_ram,sizeof g_ram);
    snprintf(name,sizeof name,"main-pc-%06X-sub-prg.bin",pc);dump(dump_directory,name,cd.prg,sizeof cd.prg);
    snprintf(name,sizeof name,"main-pc-%06X-word-ram.bin",pc);dump(dump_directory,name,cd.word,sizeof cd.word);
    snprintf(name,sizeof name,"main-pc-%06X-vdp-registers.bin",pc);dump(dump_directory,name,vdp.reg,sizeof vdp.reg);
    snprintf(name,sizeof name,"main-pc-%06X-cram-host-u16.bin",pc);dump(dump_directory,name,vdp.cram,sizeof vdp.cram);
    snprintf(name,sizeof name,"main-pc-%06X-vsram-host-u16.bin",pc);dump(dump_directory,name,vdp.vsram,sizeof vdp.vsram);
    snprintf(name,sizeof name,"main-pc-%06X-z80-ram.bin",pc);dump(dump_directory,name,genesis.z80_ram,sizeof genesis.z80_ram);
    main_pc_capture_matched=1;main_pc_capture_observed=pc;main_pc_capture_opcode=opcode;main_pc_capture_time=time;
    printf("Main PC state capture matched target=%06X observed=%06X opcode=%04X time=%llu memory_mode=%02X main_pc=%06X sub_pc=%06X (one unambiguous Main retirement)\n",
        main_pc_capture_target,pc,opcode,(unsigned long long)time,cd.memory_mode,
        coordinator.cpu[0].state.regs.PC&0xFFFFFFu,coordinator.cpu[1].state.regs.PC&0xFFFFFFu);
}
static void dump_frame_named(const char*dir,const char*name)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",dir,name);if(n<0 || (size_t)n>=sizeof path)exit(4);
    FILE*f=fopen(path,"wb");if(!f)exit(4);unsigned height=(unsigned)gvdp_screen_height(&vdp);if(height>240)height=240;
    fprintf(f,"P6\n%u %u\n255\n",frame_width,height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<frame_width;++x){uint32_t p=pixels[y][x];uint8_t rgb[3]={(uint8_t)(p>>16),(uint8_t)(p>>8),(uint8_t)p};if(fwrite(rgb,1,3,f)!=3)exit(4);}
    if(fclose(f))exit(4);
}
static int read_exact_input(const char*path,void*data,size_t bytes)
{
    FILE*f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=0;
    return ok;
}
int main(int argc,char**argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc<4 || argc>6){fprintf(stderr,"usage: segacd_bios_dual_probe BIOS SUB_CLOCK_DEADLINE PRIVATE_DUMP_DIRECTORY [IMAGE.sgcd [START_FRAME]]\n");return 2;}
    dump_directory=argv[3];if(argc==6)start_frame=strtoull(argv[5],NULL,0);
    if(!parse_probe_inputs())return 2;
    int input_test=input_selftest();if(input_test<0)return 2;if(input_test>0)return 0;
    if(!open_sub_vector_change_log())return 2;
    for(unsigned n=0;n<pad_event_count;++n)printf("P1 scripted event frame=%llu duration=%llu buttons=%03X\n",(unsigned long long)pad_events[n].frame,(unsigned long long)pad_events[n].duration,pad_events[n].mask);
    uint32_t watch_word=UINT32_MAX;uint64_t watch_after=0;
    const char *watch=getenv("SEGACD_PROBE_WATCH_WORD_BYTE");
    if(watch) {
        char *end=NULL;unsigned long value=strtoul(watch,&end,0);
        if(!*watch || *end || value>=SEGACD_WORD_BYTES)return 2;
        watch_word=(uint32_t)value;
        const char *after=getenv("SEGACD_PROBE_WATCH_AFTER");
        if(after)watch_after=strtoull(after,NULL,0);
    }
    if(!read_exact_input(argv[1],g_rom,SEGACD_BIOS_BYTES))return 2;
    if(!segacd_init(&cd,g_rom,SEGACD_BIOS_BYTES))return 2;
    const char*bram=getenv("SEGACD_PROBE_BRAM_IMAGE");
    if(bram) {
        if(!read_exact_input(bram,cd.backup,sizeof cd.backup)) {
            fprintf(stderr,"backup RAM input must be exactly %u readable bytes\n",(unsigned)sizeof cd.backup);return 2;
        }
        printf("Loaded persisted backup RAM (%u bytes); no guest formatting/progression inferred.\n",(unsigned)sizeof cd.backup);
    }
    gvdp_init(&vdp);gbus_init(&genesis,&vdp);vdp.bus_read=dma_read;
    M68kStepBus host={read8,read16,write8,write16,fetch,0,ack,0};if(!segacd_coordinator_init(&coordinator,&cd,&host,NULL))return 2;
    printf("FADER initial subclock=%llu word=%04X deadline=%llu busy=%u ready=%u; SUB peripheral_ready=%llu memory_mode=%02X main_pc=%06X sub_pc=%06X\n",
        (unsigned long long)cd.drive.cycles,cd.drive.fader,(unsigned long long)cd.drive.fader_deadline,
        cd.drive.fader_deadline!=0,!cd.drive.fader_deadline,(unsigned long long)cd.peripheral_ready,cd.memory_mode,
        coordinator.cpu[0].state.instruction_pc&0xFFFFFFu,coordinator.cpu[1].state.instruction_pc&0xFFFFFFu);
    previous_fader_busy=cd.drive.fader_deadline!=0;
    previous_peripheral_ready=cd.sub_cycles>=cd.peripheral_ready;
    SegaCDImage *image=NULL;
    if(argc>=5) {
        printf("Validating complete disc payload before mounting...\n");
        char error[256];image=segacd_image_open(argv[4],error,sizeof error);
        if(!image){fprintf(stderr,"media: %s\n",error);return 2;}
        printf("Disc payload verified; starting original programs.\n");
        if(!segacd_cdd_mount(&cd.drive,segacd_image_toc(image))){segacd_image_close(image);return 2;}
        segacd_coordinator_media(&coordinator,media_read,image,capture_audio?cdda_capture:NULL,NULL);
    }
#ifndef SEGACD_BIOS_PROBE_NO_CATALOGS
    const M68kStepModule*main_modules[]={&segacd_bios_module,&segacd_main_ram_module
#ifdef SEGACD_BIOS_PROBE_BOOT_MAIN
        ,&segacd_boot_main_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_MAIN_MODULES
    };
    const M68kStepModule*sub_modules[]={&segacd_sub_module
#ifdef SEGACD_BIOS_PROBE_SUB_PATCH
        ,&segacd_sub_patch_module
#endif
#ifdef SEGACD_BIOS_PROBE_BOOT_SUB
        ,&segacd_boot_sub_module
#endif
        SEGACD_BIOS_PROBE_EXTRA_SUB_MODULES
    };
    int floor_only=getenv("SEGACD_PROBE_FLOOR_ONLY") && !strcmp(getenv("SEGACD_PROBE_FLOOR_ONLY"),"1");
    if(!floor_only && (!segacd_coordinator_modules(&coordinator,SEGACD_MAIN,main_modules,sizeof main_modules/sizeof *main_modules) || !segacd_coordinator_modules(&coordinator,SEGACD_SUB,sub_modules,sizeof sub_modules/sizeof *sub_modules)))return 2;
#else
    int floor_only=1;
#endif
    int require_native=getenv("SEGACD_PROBE_REQUIRE_NATIVE") && !strcmp(getenv("SEGACD_PROBE_REQUIRE_NATIVE"),"1");
    if(!require_native && !segacd_coordinator_floor(&coordinator,segacd_m68k_floor_step))return 2;
    segacd_coordinator_host_clock(&coordinator,advance_host,next_host_event);
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
    const SegaCDZ80Bus sound_bus={z80_local_read,z80_local_write,z80_bank};
    if(!segacd_z80_init(&sound_cpu,&coordinator,&sound_bus,NULL,sonic_cd_z80_step))return 2;
    segacd_coordinator_device_limit(&coordinator,sound_clock_limit);
    printf("Native generated Z80 enabled; strict misses, no interpreter.\n");
#endif
    if(capture_audio) {
        if(!capture_open(dump_directory)){(void)capture_close();return 2;}
        segacd_coordinator_pcm_sink(&coordinator,pcm_capture,NULL);
        segacd_coordinator_cdda_sink(&coordinator,cdda_sample_capture,NULL);
        printf("Audio observation enabled: RF5C164 raw S16 LE stereo at 12500000/384 Hz, raw CDDA sectors and post-fader 44100 Hz CDDA frames; no host mix/output.\n");
    }
    uint64_t deadline=strtoull(argv[2],NULL,0),steps=0;
    struct {uint32_t main,sub;uint64_t time;} ring[64]={{0}};M68kStepResult status=M68K_STEP_OK;
    unsigned mask_before=cd.irq_mask,wp_before=cd.write_protect,control_before=cd.drive.control,event_count=0,commands=0,queries=0;
    uint64_t cdc_before=0;unsigned transfer_before=0,transfers=0;
    uint64_t command_deadline_before=0;
    uint64_t observed_boot_native[2]={0},observed_boot_floor[2]={0};
    uint32_t first_boot_pc[2]={0};
    uint32_t selfloop_pc[2][8]={{0}};unsigned selfloop_count[2]={0};
    unsigned watch_events=0;uint8_t watch_before=0;
    while(coordinator.device_time<deadline && steps<100000000u) {
        ring[steps%64].main=coordinator.cpu[0].state.regs.PC;ring[steps%64].sub=coordinator.cpu[1].state.regs.PC;ring[steps%64].time=coordinator.device_time;
        g_audio_cycle_counter=(uint32_t)coordinator.cpu[0].state.cycles;
        uint64_t native_before[2]={coordinator.native_steps[0],coordinator.native_steps[1]};
        uint64_t floor_before[2]={coordinator.floor_steps[0],coordinator.floor_steps[1]};
        uint8_t sub_vectors_before[PROBE_SUB_VECTOR_BYTES];
        ProbeVectorChangeStep vector_step={0};
        if(capture_sub_vector_changes) {
            memcpy(sub_vectors_before,cd.prg+PROBE_SUB_VECTOR_BASE,sizeof sub_vectors_before);
            vector_step.time_before=coordinator.device_time;
            vector_step.frame_before=(uint32_t)(line_number/262u);
            vector_step.memory_mode_before=cd.memory_mode;
            vector_step.main_pc_before=coordinator.cpu[0].state.regs.PC&0xFFFFFFu;
            vector_step.sub_pc_before=coordinator.cpu[1].state.regs.PC&0xFFFFFFu;
            vector_step.native_before[0]=native_before[0];vector_step.native_before[1]=native_before[1];
            vector_step.floor_before[0]=floor_before[0];vector_step.floor_before[1]=floor_before[1];
        }
        uint16_t fader_before=cd.drive.fader;uint64_t fader_deadline_before=cd.drive.fader_deadline;
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
        if(genesis.z80_reset_pending){segacd_z80_reset(&sound_cpu);genesis.z80_reset_pending=0;}
        if(!genesis.z80_reset_off||genesis.z80_busreq) {
            if(sound_cpu.time<=coordinator.device_time)(void)segacd_z80_hold_until(&sound_cpu,coordinator.device_time);
            status=segacd_coordinator_step(&coordinator);
        }else if(sound_cpu.time<=coordinator.device_time) {
            SegaCDZ80Result zr=segacd_z80_step(&sound_cpu);
            status=zr==SEGACD_Z80_OK?M68K_STEP_OK:sound_cpu.bus_result;
            if(zr!=SEGACD_Z80_OK)fprintf(stderr,"Z80 stopped result=%u bus=%u pc=%04X miss=%04X time=%llu retired=%llu\n",
                zr,sound_cpu.bus_result,sound_cpu.state.pc,sound_cpu.miss_pc,
                (unsigned long long)sound_cpu.time,(unsigned long long)sound_cpu.retired);
        }else status=segacd_coordinator_step(&coordinator);
#else
        status=segacd_coordinator_step(&coordinator);
#endif
        ++steps;
        if(capture_sub_vector_changes) {
            vector_step.time_after=coordinator.device_time;
            vector_step.frame_after=(uint32_t)(line_number/262u);
            vector_step.memory_mode_after=cd.memory_mode;
            vector_step.main_pc_after=coordinator.cpu[0].state.regs.PC&0xFFFFFFu;
            vector_step.sub_pc_after=coordinator.cpu[1].state.regs.PC&0xFFFFFFu;
            vector_step.native_after[0]=coordinator.native_steps[0];vector_step.native_after[1]=coordinator.native_steps[1];
            vector_step.floor_after[0]=coordinator.floor_steps[0];vector_step.floor_after[1]=coordinator.floor_steps[1];
            vector_step.status=status;
            if(memcmp(sub_vectors_before,cd.prg+PROBE_SUB_VECTOR_BASE,sizeof sub_vectors_before)&&
               !record_sub_vector_change(sub_vectors_before,cd.prg+PROBE_SUB_VECTOR_BASE,&vector_step)) {
                status=M68K_STEP_BUS_ERROR;break;
            }
        }
        /* Match only a successfully retired Main instruction in this outer
         * scheduler step. Capture before any subsequent owner can execute. */
        if(main_pc_capture_enabled&&!main_pc_capture_matched&&status==M68K_STEP_OK&&
           coordinator.last_cpu==0&&
           coordinator.native_steps[0]-native_before[0]+coordinator.floor_steps[0]-floor_before[0]==1&&
           coordinator.native_steps[1]-native_before[1]+coordinator.floor_steps[1]-floor_before[1]==0) {
            uint32_t retired_pc=coordinator.cpu[0].state.instruction_pc&0xFFFFFFu;
            if(retired_pc==main_pc_capture_target)
                capture_main_pc_state(retired_pc,coordinator.cpu[0].state.fault_opcode,coordinator.device_time);
        }
        observe_fader_and_ready(coordinator.device_time,fader_before,fader_deadline_before);
        if(capture_error||pc_roots_error||sub_vector_capture_error){status=M68K_STEP_BUS_ERROR;break;}
        if(watch_word!=UINT32_MAX && coordinator.device_time>=watch_after && watch_events<1024 &&
           (!watch_events || cd.word[watch_word]!=watch_before)) {
            printf("Word RAM watch time=%llu offset=%05X old=%02X new=%02X mode=%02X cpu=%u main=%06X sub=%06X D0=%08X/%08X D1=%08X/%08X D2=%08X/%08X\n",(unsigned long long)coordinator.device_time,watch_word,watch_before,cd.word[watch_word],cd.memory_mode,coordinator.last_cpu,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc,coordinator.cpu[0].state.regs.D[0],coordinator.cpu[1].state.regs.D[0],coordinator.cpu[0].state.regs.D[1],coordinator.cpu[1].state.regs.D[1],coordinator.cpu[0].state.regs.D[2],coordinator.cpu[1].state.regs.D[2]);
            uint32_t base=watch_word&~7u;printf("Word RAM watch bytes base=%05X value=",base);
            for(unsigned n=0;n<8;++n)printf("%02X",cd.word[base+n]);
            printf("\n");
            watch_before=cd.word[watch_word];++watch_events;
        }
        for(unsigned which=0;which<2;++which) {
            uint64_t native_delta=coordinator.native_steps[which]-native_before[which];
            uint64_t floor_delta=coordinator.floor_steps[which]-floor_before[which];
            /* A WAIT can retire several nested operations. Attribute only a
             * single retirement, whose instruction PC is unambiguous here. */
            if(native_delta+floor_delta!=1)continue;
            uint32_t pc=coordinator.cpu[which].state.instruction_pc&0xFFFFFFu;
            M68kStepState *s=&coordinator.cpu[which].state;
            if((s->regs.PC&0xFFFFFFu)==pc && (s->fault_opcode==0x60FEu || s->fault_opcode==0x6000u) && selfloop_count[which]<8) {
                unsigned n=0;while(n<selfloop_count[which] && selfloop_pc[which][n]!=pc)++n;
                if(n==selfloop_count[which]) {
                    selfloop_pc[which][selfloop_count[which]++]=pc;
                    printf("Unconditional self-loop cpu=%u pc=%06X time=%llu sr=%04X\n",which,pc,(unsigned long long)coordinator.device_time,s->regs.SR);
                    for(unsigned r=0;r<8;++r)printf("self-loop D%u=%08X A%u=%08X\n",r,s->regs.D[r],r,s->regs.A[r]);
                    for(uint64_t k=steps>32?steps-32:0;k<steps;++k)printf("self-loop predecessor time=%llu main=%06X sub=%06X\n",(unsigned long long)ring[k%64].time,ring[k%64].main,ring[k%64].sub);
                    char name[64];snprintf(name,sizeof name,"selfloop-cpu%u-%06X-memory.bin",which,pc);
                    dump(dump_directory,name,which?cd.prg:g_ram,which?sizeof cd.prg:sizeof g_ram);
                }
            }
            int boot=which==0?(pc>=0xFF0000u && pc<0xFF7E00u):(pc>=0x6000u && pc<0xD000u);
            if(boot) {
                observed_boot_native[which]+=native_delta;observed_boot_floor[which]+=floor_delta;
                if(!first_boot_pc[which]){first_boot_pc[which]=pc;printf("Observed boot-address-range execution cpu=%u pc=%06X native=%u time=%llu\n",which,pc,(unsigned)native_delta,(unsigned long long)coordinator.device_time);}
            }
            if(floor_delta && native_delta+floor_delta==1) {
                record_floor_pc(which,pc,coordinator.device_time,coordinator.cpu[which].state.fault_opcode);
            }
        }
        if(status!=M68K_STEP_OK)break;
        if(cd.cdc.sectors!=cdc_before) {
            if(cd.cdc.sectors<=24)printf("CDC sector time=%llu count=%llu header=%02X%02X%02X%02X ctrl=%02X/%02X if=%02X/%02X PT=%04X WA=%04X stat=%02X/%02X/%02X/%02X sub=%06X\n",(unsigned long long)coordinator.device_time,(unsigned long long)cd.cdc.sectors,cd.cdc.header[0],cd.cdc.header[1],cd.cdc.header[2],cd.cdc.header[3],cd.cdc.ctrl0,cd.cdc.ctrl1,cd.cdc.ifctrl,cd.cdc.ifstat,cd.cdc.block_pointer,cd.cdc.write_address,cd.cdc.status[0],cd.cdc.status[1],cd.cdc.status[2],cd.cdc.status[3],coordinator.cpu[1].state.regs.PC);
            cdc_before=cd.cdc.sectors;
        }
        if(cd.cdc.transfer_active && !transfer_before && transfers++<24)printf("CDC transfer time=%llu dest=%u DAC=%04X DBC=%04X remaining=%u sub=%06X\n",(unsigned long long)coordinator.device_time,cd.cdc_destination,cd.cdc.dac,cd.cdc.dbc,cd.cdc.remaining,coordinator.cpu[1].state.instruction_pc);
        transfer_before=cd.cdc.transfer_active;
        if(cd.drive.command_pending && cd.drive.command_deadline!=command_deadline_before && cd.drive.pending[0]) {
            int log=cd.drive.pending[0]==2?queries++<24:commands++<256;
            if(log) {
                printf("CDD command time=%llu main=%06X sub=%06X packet=",(unsigned long long)coordinator.device_time,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc);
                for(unsigned n=0;n<10;++n)printf("%X",cd.drive.pending[n]);
                printf("\n");
            }
        }
        command_deadline_before=cd.drive.command_deadline;
        if(mask_before!=cd.irq_mask || wp_before!=cd.write_protect || control_before!=cd.drive.control) {
            if(event_count++<64)printf("device event time=%llu main=%06X sub=%06X mask=%02X wp=%02X drive_control=%02X\n",(unsigned long long)coordinator.device_time,coordinator.cpu[0].state.instruction_pc,coordinator.cpu[1].state.instruction_pc,cd.irq_mask,cd.write_protect,cd.drive.control);
            mask_before=cd.irq_mask;wp_before=cd.write_protect;control_before=cd.drive.control;
        }
    }
    printf("native dual-program probe: status=%u cpu=%u steps=%llu device_time=%llu lines=%llu waits=%llu wait_cpu=%u wait_result=%u\n",
        (unsigned)status,coordinator.last_cpu,(unsigned long long)steps,(unsigned long long)coordinator.device_time,(unsigned long long)line_number,(unsigned long long)coordinator.waits,coordinator.wait_cpu,(unsigned)coordinator.wait_result);
    for(unsigned which=0;which<2;++which) {
        M68kStepState*s=&coordinator.cpu[which].state;
        printf("cpu%u pc=%06X sr=%04X instructions=%llu cycles=%llu fault=%u address=%06X instruction=%06X opcode=%04X\n",which,s->regs.PC,s->regs.SR,(unsigned long long)s->instructions,(unsigned long long)s->cycles,s->fault,s->fault_address,s->instruction_pc,s->fault_opcode);
        for(unsigned r=0;r<8;++r)printf("cpu%u D%u=%08X A%u=%08X\n",which,r,s->regs.D[r],r,s->regs.A[r]);
    }
    for(uint64_t n=steps>64?steps-64:0;n<steps;++n)printf("%llu time=%llu main=%06X sub=%06X\n",(unsigned long long)n,(unsigned long long)ring[n%64].time,ring[n%64].main,ring[n%64].sub);
    printf("sub_control=%u audio_events=%llu dma_stalls=%llu media_sectors=%llu cdc_sectors=%llu cdda_sectors=%llu media_error=%u lba=%d (host audio output absent)\n",cd.main_control,(unsigned long long)audio_events,(unsigned long long)dma_stalls,(unsigned long long)cd.media_sectors,(unsigned long long)cd.cdc.sectors,(unsigned long long)cd.cdda_sectors,cd.media_error,cd.media_error_lba);
#ifdef SEGACD_BIOS_PROBE_NATIVE_Z80
    printf("Z80 native result=%u retired=%llu time=%llu wait_ticks=%llu tstates=%llu pc=%04X miss=%04X bank=%03X irq=%u iff=%u/%u halted=%u reset_off=%u busreq=%u\n",
        sound_cpu.result,(unsigned long long)sound_cpu.retired,(unsigned long long)sound_cpu.time,
        (unsigned long long)sound_cpu.wait_ticks,(unsigned long long)sound_cpu.state.cyc,sound_cpu.state.pc,
        sound_cpu.miss_pc,genesis.z80_bank,sound_cpu.irq_pending,sound_cpu.state.iff1,sound_cpu.state.iff2,
        sound_cpu.state.halted,genesis.z80_reset_off,genesis.z80_busreq);
    dump(argv[3],"z80-ram.bin",genesis.z80_ram,sizeof genesis.z80_ram);
#endif
    printf("stamp operations=%llu pixels=%llu busy=%u remaining_lines=%u\n",(unsigned long long)cd.stamp.operations,(unsigned long long)cd.stamp.pixels,cd.stamp.busy,cd.stamp.reg[6]);
    printf("gate mask=%02X pending=%02X flags=%02X/%02X wp=%02X events=%u; drive status=%u control=%02X command=%u unsupported=%u lba=%d frames=%llu rx=",cd.irq_mask,cd.irq_pending,cd.flags[0],cd.flags[1],cd.write_protect,event_count,cd.drive.status,cd.drive.control,cd.drive.command_pending,cd.drive.unsupported_command,cd.drive.lba,(unsigned long long)cd.drive.frames);
    for(unsigned n=0;n<10;++n)printf("%X",cd.drive.rx[n]);
    printf(" tx=");
    for(unsigned n=0;n<10;++n)printf("%X",cd.drive.tx[n]);
    printf("\n");
    printf("retired native=%llu/%llu floor=%llu/%llu floor_only=%u\n",(unsigned long long)coordinator.native_steps[0],(unsigned long long)coordinator.native_steps[1],(unsigned long long)coordinator.floor_steps[0],(unsigned long long)coordinator.floor_steps[1],(unsigned)floor_only);
    printf("floor PC unique epoch=%u main=%llu sub=%llu print_limit=%u normalized_even_pc_bitmap=1MB_per_CPU\n",floor_pc_epoch,(unsigned long long)floor_pc_count[0],(unsigned long long)floor_pc_count[1],floor_pc_limit);
    printf("Main PC capture enabled=%u target=%06X matched=%u observed=%06X opcode=%04X time=%llu\n",
        main_pc_capture_enabled,main_pc_capture_target,main_pc_capture_matched,main_pc_capture_observed,
        main_pc_capture_opcode,(unsigned long long)main_pc_capture_time);
    printf("observed boot-address-range native=%llu/%llu floor=%llu/%llu first_pc=%06X/%06X require_native=%u (single-retirement observations; address range alone does not identify loaded code)\n",(unsigned long long)observed_boot_native[0],(unsigned long long)observed_boot_native[1],(unsigned long long)observed_boot_floor[0],(unsigned long long)observed_boot_floor[1],first_boot_pc[0],first_boot_pc[1],(unsigned)require_native);
    printf("P1 reads=%llu while_Start_pressed=%llu start_frame=%llu\n",(unsigned long long)pad_reads,(unsigned long long)pressed_pad_reads,(unsigned long long)start_frame);
    printf("fader events=%llu (printed cap=256) peripheral_ready_events=%llu memory_mode=%02X fader_raw=%04X busy=%u deadline=%llu pcm_samples=%llu cdda_sectors=%llu capture=%u\n",
        (unsigned long long)fader_event_count,(unsigned long long)peripheral_ready_events,cd.memory_mode,cd.drive.fader,
        cd.drive.fader_deadline!=0,(unsigned long long)cd.drive.fader_deadline,(unsigned long long)pcm_event_count,
        (unsigned long long)cdda_event_count,capture_audio);
    printf("CDDA error=%u status=%u clock=%llu ticks=%llu emitted=%llu source_anchored=%u fifo=%u/%u underflow=%llu capture_samples=%llu fader_current=%04X target=%04X\n",
        cd.cdda_error,cd.cdda.status,(unsigned long long)cd.cdda.clock_now,
        (unsigned long long)cd.cdda.total_sample_ticks,(unsigned long long)cd.cdda.delivered_frames,
        cd.cdda.source_anchored,cd.cdda.fifo_count,cd.cdda.fifo_frame,
        (unsigned long long)cd.cdda.underflow_frames,(unsigned long long)cdda_sample_count,
        segacd_fader_current_raw(&cd.cdda.fader),cd.cdda.fader_raw_target);
    printf("CDC final transfers=%u dest=%u DAC=%04X DBC=%04X PT=%04X WA=%04X ctrl=%02X/%02X if=%02X/%02X DMA_words=%llu\n",transfers,cd.cdc_destination,cd.cdc.dac,cd.cdc.dbc,cd.cdc.block_pointer,cd.cdc.write_address,cd.cdc.ctrl0,cd.cdc.ctrl1,cd.cdc.ifctrl,cd.cdc.ifstat,(unsigned long long)cd.dma.words);
    dump(argv[3],"cdc-ram.bin",cd.cdc.ram,sizeof cd.cdc.ram);
    dump(argv[3],"backup-ram.bin",cd.backup,sizeof cd.backup);
    dump(argv[3],"vdp-registers.bin",vdp.reg,sizeof vdp.reg);
    dump(argv[3],"cram-host-u16.bin",vdp.cram,sizeof vdp.cram);
    dump(argv[3],"vsram-host-u16.bin",vdp.vsram,sizeof vdp.vsram);
    dump(argv[3],"main-ram.bin",g_ram,sizeof g_ram);dump(argv[3],"sub-prg.bin",cd.prg,sizeof cd.prg);dump(argv[3],"word-ram.bin",cd.word,sizeof cd.word);dump(argv[3],"vram.bin",vdp.vram,sizeof vdp.vram);dump_frame_named(argv[3],"frame.ppm");
    int capture_ok=capture_close();
    int roots_ok=!pc_roots_error&&write_floor_pc_roots(floor_pc_epoch,(uint32_t)(line_number/262u),coordinator.device_time);
    int vector_changes_ok=close_sub_vector_change_log();
    segacd_image_close(image);
    return capture_ok && !capture_error && roots_ok && !pc_roots_error && vector_changes_ok && !sub_vector_capture_error && status==M68K_STEP_OK && coordinator.device_time>=deadline?0:3;
}
