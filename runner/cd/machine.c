#include "machine.h"
#include "../video/genesis_dac.h"
#include <stdlib.h>
#include <string.h>
#ifndef SEGACD_CATALOG_PC_INDEX
#define SEGACD_CATALOG_PC_INDEX 1
#endif

/* Genesis bus ABI. Work RAM remains CPU-visible real storage, not a host
 * campaign/state replacement. No cartridge ROM is installed in CD mode. */
uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext,g_sndwatch[0x2000];
uint32_t g_rom_ext_size,g_audio_cycle_counter,g_68k_stamp_rebase;
unsigned long g_snd_frame;
unsigned g_snd_pcz;
static SegaCDMachine *current;
static SegaCDMachineVideoObserver video_observer;
static void *video_user;
static int observing_video;
/* One active machine, as for the bus ABI. Derived image inputs deliberately
 * live outside hardware state and are never copied into execution snapshots. */
typedef struct {
    GVDPRasterRow raster;
    uint16_t cram[GVDP_CRAM_ENTRIES];
    int pending;
} CapturedRow;
typedef struct {
    CapturedRow rows[GVDP_MAX_HEIGHT];
    SegaCDMachineVideoStats stats;
} CapturedVideo;
static CapturedVideo *captured_video;
static void resolve_row(SegaCDMachine *m,unsigned row)
{
    CapturedRow *r=&captured_video->rows[row];
    if(!r->pending)return;
    uint8_t indices[GVDP_MAX_WIDTH];
    int width=gvdp_compose_captured(&r->raster,indices);
    /* Captures are internal and validated when produced. Keep the old CD
     * contract for reserved host/bar indices: opaque black. */
    uint32_t palette[192];
    unsigned levels=r->raster.shadow_mode||r->raster.diagnostic?3u:1u;
    for(unsigned i=0;i<levels*64u;++i)
        palette[i]=genesis_dac_cram_to_argb(r->cram[i%64u],(int)(i/64u));
    for(int x=0;x<width;++x) {
        unsigned i=indices[x];m->pixels[row][x]=i<levels*64u?palette[i]:0xff000000u;
    }
    r->pending=0;++captured_video->stats.composed_rows;
}
const uint32_t *segacd_machine_pixels(const SegaCDMachine *m)
{
    if(!m||m!=current)return NULL;
    if(captured_video)for(unsigned row=0;row<GVDP_MAX_HEIGHT;++row)
        resolve_row(current,row);
    return &m->pixels[0][0];
}
int segacd_machine_video_stats(const SegaCDMachine *m,SegaCDMachineVideoStats *stats)
{
    if(!m||m!=current||!stats)return 0;
    if(captured_video)*stats=captured_video->stats;
    else memset(stats,0,sizeof *stats);
    return 1;
}
int segacd_machine_defer_video(SegaCDMachine *m,int enabled)
{
    if(!m||m!=current||observing_video||m->coordinator.busy[0]||m->coordinator.busy[1]||
       m->coordinator.external_active||m->z80.in_step)return 0;
    if(enabled&&!captured_video) {
        captured_video=(CapturedVideo *)calloc(1,sizeof *captured_video);
        if(!captured_video)return 0;
        captured_video->stats.retained_bytes=sizeof *captured_video;
    }
    if(captured_video) {
        if(!enabled)(void)segacd_machine_pixels(m);
        captured_video->stats.deferred=enabled!=0;
    }
    return 1;
}
static uint64_t master_clock(uint64_t sub)
{return sub/12500000u*53693175u+(sub%12500000u)*53693175u/12500000u;}
uint64_t segacd_machine_line_clock(uint64_t line)
{
    uint64_t master=line*3420u;
    return master/53693175u*12500000u+
        ((master%53693175u)*12500000u+53693174u)/53693175u;
}
/* The NTSC deadline is a pure function of the line counter. CPU operations
 * query it many times between raster events; derive it once per changed
 * counter, including arbitrary current-build counter restore/replacement. */
static uint64_t cached_line,cached_line_clock;
static uint64_t line_deadline(uint64_t line)
{
    if(line!=cached_line){cached_line_clock=segacd_machine_line_clock(line);cached_line=line;}
    return cached_line_clock;
}
static void stamp_bus(SegaCDMachine *m,uint64_t sub)
{
    uint64_t master=master_clock(sub);m->bus_clock=sub;
    g_snd_frame=(unsigned long)(master/(262u*3420u));
    g_audio_cycle_counter=0;g_68k_stamp_rebase=(uint32_t)(master%(262u*3420u));
}
static void main_stamp(SegaCDMachine *m)
{
    stamp_bus(m,m->coordinator.external_active?segacd_z80_bus_time(&m->z80):m->coordinator.time[0]);
}
uint32_t machine_z80_stamp(void)
{return current?(uint32_t)(master_clock(current->bus_clock)%(262u*3420u)):0;}
uint32_t machine_z80_pc(void){return g_z80.pc;}
void snd_trace_68k_write(uint16_t o,uint8_t a,uint8_t b,int req,int reset)
{(void)o;(void)a;(void)b;(void)req;(void)reset;}
void snd_trace_z80(uint16_t o,uint8_t v,int w){(void)o;(void)v;(void)w;}
void snd_trace_busreq(const char *s,int v){(void)s;(void)v;}
void audio_event_push(uint32_t unused,uint8_t port,uint8_t value)
{
    (void)unused;
    if(current&&current->audio_write)
        current->audio_write(current->audio_user,current->bus_clock,port,value);
}
void glue_charge_68k_stall(uint32_t cycles)
{if(current)segacd_coordinator_main_stall(&current->coordinator,cycles);}
static M68kStepBusResult read8(void *u,uint32_t a,uint8_t *v)
{SegaCDMachine*m=u;main_stamp(m);*v=gbus_read8(&m->bus,a);return M68K_BUS_OK;}
static M68kStepBusResult read16(void *u,uint32_t a,uint16_t *v)
{SegaCDMachine*m=u;main_stamp(m);*v=gbus_read16(&m->bus,a);return M68K_BUS_OK;}
static M68kStepBusResult write8(void *u,uint32_t a,uint8_t v)
{SegaCDMachine*m=u;main_stamp(m);gbus_write8(&m->bus,a,v);return M68K_BUS_OK;}
static M68kStepBusResult write16(void *u,uint32_t a,uint16_t v)
{SegaCDMachine*m=u;main_stamp(m);gbus_write16(&m->bus,a,v);return M68K_BUS_OK;}
static M68kStepBusResult fetch16(void *u,uint32_t a,uint16_t *v)
{
    SegaCDMachine*m=u;if(a<0xff0000u)return M68K_BUS_FAULT;
    *v=gbus_peek16(&m->bus,a);return M68K_BUS_OK;
}
static const uint8_t *host_code_span(void *u,uint32_t a,size_t bytes)
{
    (void)u;
    if(a<0xff0000u||a>0xffffffu||!bytes||bytes>0x1000000u-a)return NULL;
    return g_ram+(a&0xffffu);
}
static unsigned ack(void *u,unsigned level)
{SegaCDMachine*m=u;if(level==6)m->irq6=0;if(level==4)m->irq4=0;return M68K_STEP_AUTOVECTOR;}
static uint16_t dma_read(void *u,uint32_t address)
{return segacd_coordinator_vdp_read16(&((SegaCDMachine*)u)->coordinator,address);}
static uint8_t z80_read(void *u,uint16_t a,uint64_t clock)
{SegaCDMachine*m=u;stamp_bus(m,clock);return gbus_z80_read(&m->bus,a);}
static void z80_write(void *u,uint16_t a,uint8_t v,uint64_t clock)
{SegaCDMachine*m=u;stamp_bus(m,clock);gbus_z80_write(&m->bus,a,v);}
static uint16_t z80_bank(void *u){return (uint16_t)((SegaCDMachine*)u)->bus.z80_bank;}
static uint64_t sound_limit(void *u)
{
    SegaCDMachine*m=u;
    if(m->z80.in_step)return segacd_z80_bus_time(&m->z80);
    return m->bus.z80_reset_off&&!m->bus.z80_busreq?m->z80.time:UINT64_MAX;
}
static uint64_t next_host_event(void *u)
{return line_deadline(((SegaCDMachine*)u)->line);}
static void host_advance(void *u,uint64_t clock)
{
    SegaCDMachine*m=u;
    while(line_deadline(m->line)<=clock) {
        unsigned line=(unsigned)(m->line%262u);
        if(m->line) {
            unsigned row=(line+261u)%262u;
            if(row<GVDP_MAX_HEIGHT) {
                uint8_t indices[GVDP_MAX_WIDTH];
                int width=0,deferred=0;
                if(captured_video&&captured_video->stats.deferred) {
                    CapturedRow *r=&captured_video->rows[row];
                    /* Eager rendering leaves the tail of a narrowed row
                     * untouched. Resolve that previous tail before reuse. */
                    if(r->pending&&r->raster.width>(unsigned)gvdp_active_width(&m->vdp))
                        resolve_row(m,row);
                    width=gvdp_capture_scanline(&m->vdp,(int)row,&r->raster);
                    if(width>0) {
                        memcpy(r->cram,m->vdp.cram,sizeof r->cram);
                        r->pending=1;deferred=1;
                        ++captured_video->stats.captured_rows;
                    }else resolve_row(m,row);
                }
                if(!deferred)width=gvdp_render_scanline(&m->vdp,(int)row,indices);
                if(width>0&&width<=GVDP_MAX_WIDTH) {
                    m->width=(unsigned)width;
                    if(!deferred) {
                        for(int x=0;x<width;++x){unsigned i=indices[x];m->pixels[row][x]=i<192?
                            genesis_dac_cram_to_argb(m->vdp.cram[i%64],(int)(i/64)):0xff000000u;}
                        if(captured_video)++captured_video->stats.eager_rows;
                    }
                    if(video_observer&&row<(unsigned)gvdp_screen_height(&m->vdp)) {
                        observing_video=1;video_observer(video_user,m,row);observing_video=0;
                    }
                }
            }
        }
        if(!line&&m->line){++m->frames;m->height=(unsigned)gvdp_screen_height(&m->vdp);}
        unsigned irq=gvdp_begin_scanline(&m->vdp,(int)line);
        if(irq&GVDP_IRQ_VBLANK){m->irq6=1;segacd_z80_irq(&m->z80);}
        if(irq&GVDP_IRQ_HBLANK)m->irq4=1;
        ++m->line;
    }
    segacd_coordinator_main_irq(&m->coordinator,m->irq6?6u:m->irq4?4u:0u);
}
int segacd_machine_init(SegaCDMachine *m,const void *bios,size_t bytes,
                        const SegaCDNativeProgram *program)
{
    if(!m||!program||!bios||bytes!=SEGACD_BIOS_BYTES||current||!program->z80_step)return 0;
    memset(m,0,sizeof *m);memset(g_rom,0,sizeof g_rom);memset(g_ram,0,sizeof g_ram);
    g_rom_ext=NULL;g_rom_ext_size=0;g_snd_frame=0;g_audio_cycle_counter=g_68k_stamp_rebase=0;
    m->width=320;m->height=224;
    if(!segacd_init(&m->devices,bios,bytes))return 0;
    gvdp_init(&m->vdp);gbus_init(&m->bus,&m->vdp);
    m->vdp.bus_read=dma_read;m->vdp.bus_read_user=m;
    const M68kStepBus host={read8,read16,write8,write16,fetch16,NULL,ack,NULL};
    if(!segacd_coordinator_init(&m->coordinator,&m->devices,&host,m)||
       !segacd_coordinator_modules(&m->coordinator,SEGACD_MAIN,program->main_modules,program->main_count)||
       !segacd_coordinator_modules(&m->coordinator,SEGACD_SUB,program->sub_modules,program->sub_count))return 0;
    const SegaCDZ80Bus sound={z80_read,z80_write,z80_bank};
    if(!segacd_z80_init(&m->z80,&m->coordinator,&sound,m,program->z80_step))return 0;
    (void)segacd_coordinator_main_code_span(&m->coordinator,host_code_span);
    segacd_coordinator_host_clock(&m->coordinator,host_advance,next_host_event);
    segacd_coordinator_device_limit(&m->coordinator,sound_limit);
    (void)segacd_coordinator_catalog_index(&m->coordinator,SEGACD_CATALOG_PC_INDEX);
    video_observer=NULL;video_user=NULL;observing_video=0;current=m;return 1;
}
void segacd_machine_close(SegaCDMachine *m)
{if(m&&current==m&&!observing_video){(void)segacd_coordinator_catalog_index(&m->coordinator,0);
    free(captured_video);captured_video=NULL;
    current=NULL;video_observer=NULL;video_user=NULL;}}
int segacd_machine_video_observer(SegaCDMachine *m,SegaCDMachineVideoObserver observer,void *user)
{
    if(!m||m!=current||observing_video||m->coordinator.busy[0]||m->coordinator.busy[1]||
       m->coordinator.external_active||m->z80.in_step)return 0;
    if(!segacd_machine_defer_video(m,0))return 0;
    video_observer=observer;video_user=user;return 1;
}
void segacd_machine_pad(SegaCDMachine *m,unsigned port,uint16_t buttons)
{if(m&&port<2)m->bus.pad[port]=buttons&0xfffu;}
M68kStepResult segacd_machine_step(SegaCDMachine *m)
{
    if(!m||m!=current||observing_video)return M68K_STEP_REENTERED;
    if(m->coordinator.busy[0]||m->coordinator.busy[1]||m->coordinator.external_active||m->z80.in_step)
        return M68K_STEP_REENTERED;
    if(m->result!=M68K_STEP_OK)return m->result;
    if(m->bus.z80_reset_pending){segacd_z80_reset(&m->z80);m->bus.z80_reset_pending=0;}
    if(!m->bus.z80_reset_off||m->bus.z80_busreq) {
        if(m->z80.time<=m->coordinator.device_time)
            (void)segacd_z80_hold_until(&m->z80,m->coordinator.device_time);
        m->result=segacd_coordinator_step(&m->coordinator);
    }else if(m->z80.time<=m->coordinator.device_time) {
        SegaCDZ80Result result=segacd_z80_step(&m->z80);
        if(result!=SEGACD_Z80_OK)m->result=m->z80.bus_result;
    }else m->result=segacd_coordinator_step(&m->coordinator);
    return m->result;
}
M68kStepResult segacd_machine_run_frame(SegaCDMachine *m)
{
    if(!m||m!=current)return M68K_STEP_REENTERED;
    uint64_t frame=m->frames;
    do {M68kStepResult r=segacd_machine_step(m);if(r!=M68K_STEP_OK)return r;}
    while(m->frames==frame);
    return M68K_STEP_OK;
}
