#include "segacd_coordinator.h"
#include <limits.h>
#include <string.h>

/* NTSC master 53,693,175 Hz; main 68000 master/7; sub 12,500,000 Hz.
 * PAL and hardware bus-cycle phasing need separate qualification. */
enum { MAIN_DENOM=53693175,MAIN_NUMER=87500000 };
static M68kStepResult execute_cpu(SegaCDCoordinator *,unsigned);
static int device_fault(const SegaCDCoordinator*c)
{
    return c->devices->drive.unsupported_command || c->devices->media_error || c->devices->cdda_error ||
        c->devices->dma.last_result==SEGACD_DMA_BAD_DESTINATION ||
        c->devices->dma.last_result==SEGACD_DMA_ODD_BYTE_COUNT ||
        c->devices->dma.last_result==SEGACD_DMA_UNMAPPED;
}
static void advance_devices(SegaCDCoordinator *c)
{
    uint64_t now=c->time[0];
    if(segacd_sub_runnable(c->devices) && c->time[1]<now)now=c->time[1];
    while(now>c->device_time) {
        uint64_t remaining=now-c->device_time;
        uint32_t delta=remaining>UINT32_MAX?UINT32_MAX:(uint32_t)remaining;
        segacd_advance_io(c->devices,delta,&c->io);c->device_time+=delta;
    }
    if(c->host_advance)c->host_advance(c->host_user,c->device_time);
}
static uint64_t next_event_time(SegaCDCoordinator*c,uint64_t deadline)
{
    uint64_t now=c->device_time,next=now+SEGACD_TIMER_DIVISOR;
    if(c->devices->timer_reload) {
        uint64_t timer=c->devices->timer_origin+((uint64_t)c->devices->timer_reload+1)*SEGACD_TIMER_DIVISOR;
        if(timer>now && timer<next)next=timer;
    }
    if(c->host_next_event) {
        uint64_t host=c->host_next_event(c->host_user);
        if(host>now && host<next)next=host;
    }
    uint64_t drive=segacd_cdd_next_event(&c->devices->drive);
    if(drive!=UINT64_MAX && drive>c->devices->drive.cycles) {
        uint64_t deadline_drive=now+(drive-c->devices->drive.cycles);
        if(deadline_drive<next)next=deadline_drive;
    }
    if(c->devices->cdc.decoder_irq_clocks && now+c->devices->cdc.decoder_irq_clocks<next)
        next=now+c->devices->cdc.decoder_irq_clocks;
    if(c->devices->stamp.busy && !(c->devices->memory_mode&5u) && now+c->devices->stamp.remaining_clocks<next)
        next=now+c->devices->stamp.remaining_clocks;
    if(c->devices->dma.active && now+6u-c->devices->dma.phase<next)
        next=now+6u-c->devices->dma.phase;
    return deadline<next?deadline:next;
}
static void account(SegaCDCoordinator *c,unsigned cpu,uint64_t cycles)
{
    if(cpu==SEGACD_SUB)c->time[cpu]+=cycles;
    else {
        /* Each native operation/exception is small. Chunking also permits
         * long explicit idle/stall charges without a multiply overflow. */
        while(cycles) {
            uint64_t chunk=cycles>UINT32_MAX?UINT32_MAX:cycles;
            uint64_t scaled=chunk*MAIN_NUMER+c->main_remainder;
            c->time[0]+=scaled/MAIN_DENOM;c->main_remainder=scaled%MAIN_DENOM;
            cycles-=chunk;
        }
    }
}
static M68kStepBusResult translate(SegaCDResult r)
{return r==SEGACD_OK?M68K_BUS_OK:r==SEGACD_WAIT?M68K_BUS_WAIT:M68K_BUS_FAULT;}
static int main_cd_port(uint32_t a){return a>=0xA12000u && a<0xA12200u;}
static M68kStepBusResult bus_read8(void *user,uint32_t a,uint8_t *v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_read8(c->devices,b->cpu,a,v);
    if(r!=SEGACD_UNMAPPED)return translate(r);
    if(b->cpu==SEGACD_SUB || main_cd_port(a) || !c->host_main.read8)return M68K_BUS_FAULT;
    return c->host_main.read8(c->host_user,a,v);
}
static M68kStepBusResult bus_read16(void *user,uint32_t a,uint16_t *v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_read16(c->devices,b->cpu,a,v);
    if(r!=SEGACD_UNMAPPED)return translate(r);
    if(b->cpu==SEGACD_SUB || main_cd_port(a) || !c->host_main.read16)return M68K_BUS_FAULT;
    return c->host_main.read16(c->host_user,a,v);
}
static M68kStepBusResult bus_write8(void *user,uint32_t a,uint8_t v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_write8(c->devices,b->cpu,a,v);
    if(r!=SEGACD_UNMAPPED)return translate(r);
    if(b->cpu==SEGACD_SUB || main_cd_port(a) || !c->host_main.write8)return M68K_BUS_FAULT;
    return c->host_main.write8(c->host_user,a,v);
}
static M68kStepBusResult bus_write16(void *user,uint32_t a,uint16_t v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_write16(c->devices,b->cpu,a,v);
    if(r!=SEGACD_UNMAPPED)return translate(r);
    if(b->cpu==SEGACD_SUB || main_cd_port(a) || !c->host_main.write16)return M68K_BUS_FAULT;
    return c->host_main.write16(c->host_user,a,v);
}
static M68kStepBusResult bus_fetch(void *user,uint32_t a,uint16_t *v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    /* Executable CD memory views have no read side effects. Device ports
     * must not be read speculatively for a module's identity guard. */
    int memory=b->cpu==SEGACD_MAIN ? a<0x40000u || (a>=0x200000u && a<0x240000u) : a<0xE0000u;
    if(memory) {
        SegaCDResult r=segacd_read16(c->devices,b->cpu,a,v);
        if(r!=SEGACD_UNMAPPED)return translate(r);
    }
    if(b->cpu==SEGACD_MAIN && !main_cd_port(a) && c->host_main.fetch16)
        return c->host_main.fetch16(c->host_user,a,v);
    return M68K_BUS_FAULT;
}
static unsigned bus_ack(void *user,unsigned level)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    if(b->cpu==SEGACD_SUB)segacd_irq_ack(c->devices,level);
    else {
        c->main_irq=0;
        if(c->host_main.ack_interrupt)return c->host_main.ack_interrupt(c->host_user,level);
    }
    return M68K_STEP_AUTOVECTOR;
}
static void bus_reset(void *user)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    if(b->cpu==SEGACD_MAIN) {
        segacd_reset(c->devices);
        if(c->host_main.reset_devices)c->host_main.reset_devices(c->host_user);
    }
}
static int bus_wait(void *user,uint32_t address,int write)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;unsigned other=(unsigned)b->cpu^1u;
    (void)write;++c->waits;c->wait_address=address;c->wait_cpu=other;c->wait_result=M68K_STEP_OK;
    if(c->busy[other]){c->wait_result=M68K_STEP_REENTERED;return 0;}
    if(other==SEGACD_SUB && !segacd_sub_runnable(c->devices)){c->wait_result=M68K_STEP_STOPPED;return 0;}
    /* Run the actual owner while this operation stays on its C stack. The
     * caller retries only this bus access, preserving previous bus effects. */
    for(unsigned n=0;n<c->wait_limit;++n) {
        M68kStepResult r=execute_cpu(c,other);
        if(r!=M68K_STEP_OK && r!=M68K_STEP_STOPPED){c->wait_result=r;return 0;}
        if(c->time[b->cpu]<c->time[other])c->time[b->cpu]=c->time[other];
        advance_devices(c);
        uint8_t ignored=0;
        SegaCDResult access;
        if(write && b->cpu==SEGACD_SUB && address==0xFF8066u)
            access=(c->devices->memory_mode&4u)?SEGACD_UNMAPPED:
                (c->devices->memory_mode&1u)?SEGACD_WAIT:SEGACD_OK;
        else access=segacd_read8(c->devices,b->cpu,address,&ignored);
        if(access!=SEGACD_WAIT)return access==SEGACD_OK;
        if(r==M68K_STEP_STOPPED) {
            /* A stopped owner can wake on its timer or a host VDP interrupt.
             * Advance those real event sources while preserving this WAIT. */
            uint64_t next=next_event_time(c,UINT64_MAX);
            if(c->time[other]<next)c->time[other]=next;
            if(c->time[b->cpu]<c->time[other])c->time[b->cpu]=c->time[other];
            advance_devices(c);
        }
    }
    c->wait_result=M68K_STEP_NO_WAIT_HANDLER;return 0;
}
static M68kStepResult refresh_sub(SegaCDCoordinator *c)
{
    int runnable=segacd_sub_runnable(c->devices);
    if(!runnable){c->sub_was_running=0;c->time[1]=c->time[0];return M68K_STEP_OK;}
    if(!c->sub_initialized || c->reset_generation!=c->devices->sub_reset_generation) {
        if(c->busy[1])return M68K_STEP_REENTERED;
        c->reset_generation=c->devices->sub_reset_generation;
        M68kStepResult r=m68k_step_reset(&c->cpu[1]);
        if(r!=M68K_STEP_OK){c->last_cpu=1;return r;}
        c->sub_initialized=1;c->time[1]=c->time[0];
    } else if(!c->sub_was_running && c->time[1]<c->time[0])c->time[1]=c->time[0];
    c->sub_was_running=1;return M68K_STEP_OK;
}
static M68kStepResult execute_cpu(SegaCDCoordinator *c,unsigned which)
{
    M68kStepContext*cpu=&c->cpu[which];
    if(c->busy[which])return M68K_STEP_REENTERED;
    c->last_cpu=which;
    uint64_t before=cpu->state.cycles;
    unsigned level=which==SEGACD_SUB?segacd_irq_level(c->devices):c->main_irq;
    if(m68k_step_interrupt(cpu,level)) {
        account(c,which,cpu->state.cycles-before);return M68K_STEP_OK;
    }
    if(cpu->state.fault)return (M68kStepResult)cpu->state.fault;
    if(cpu->state.stopped)return M68K_STEP_STOPPED;
    c->busy[which]=1;M68kStepResult result=M68K_STEP_MISS;
    for(size_t n=0;n<c->module_count[which];++n) {
        result=m68k_step_module_execute(cpu,c->modules[which][n]);
        if(result!=M68K_STEP_MISS)break;
    }
    if(result==M68K_STEP_MISS && c->floor_step) {
        result=c->floor_step(cpu);if(result==M68K_STEP_OK)++c->floor_steps[which];
    } else if(result==M68K_STEP_OK)++c->native_steps[which];
    c->busy[which]=0;
    c->last_cpu=which;
    if(result==M68K_STEP_OK)account(c,which,cpu->state.cycles-before);
    return result;
}
int segacd_coordinator_init(SegaCDCoordinator *c,SegaCD *devices,const M68kStepBus *host,void *user)
{
    if(!c || !devices)return 0;
    memset(c,0,sizeof *c);c->devices=devices;c->host_user=user;c->wait_limit=100000;
    if(host)c->host_main=*host;
    c->device_time=c->time[0]=c->time[1]=devices->sub_cycles;
    c->reset_generation=devices->sub_reset_generation;
    M68kStepBus bus={bus_read8,bus_read16,bus_write8,bus_write16,bus_fetch,bus_wait,bus_ack,bus_reset};
    for(unsigned n=0;n<2;++n){c->binding[n].owner=c;c->binding[n].cpu=(SegaCDCpu)n;m68k_step_init(&c->cpu[n],&bus,&c->binding[n]);}
    return m68k_step_reset(&c->cpu[0])==M68K_STEP_OK;
}
int segacd_coordinator_modules(SegaCDCoordinator *c,SegaCDCpu cpu,const M68kStepModule *const *modules,size_t count)
{
    if(!c || (unsigned)cpu>1 || (count && !modules) || c->busy[0] || c->busy[1])return 0;
    for(size_t n=0;n<count;++n)if(!m68k_step_module_valid(modules[n]))return 0;
    c->modules[cpu]=modules;c->module_count[cpu]=count;return 1;
}
int segacd_coordinator_floor(SegaCDCoordinator *c,M68kStepResult (*step)(M68kStepContext *))
{
    if(!c || c->busy[0] || c->busy[1])return 0;
    c->floor_step=step;return 1;
}
void segacd_coordinator_main_irq(SegaCDCoordinator*c,unsigned level){if(c && level<=7)c->main_irq=level;}
void segacd_coordinator_host_clock(SegaCDCoordinator*c,void (*advance)(void *,uint64_t),uint64_t (*next)(void *))
{if(c && !c->busy[0] && !c->busy[1]){c->host_advance=advance;c->host_next_event=next;}}
void segacd_coordinator_pcm_sink(SegaCDCoordinator*c,Rf5c164Sink sink,void*user)
{if(c && !c->busy[0] && !c->busy[1]){c->io.pcm_sink=sink;c->io.pcm_user=user;}}
void segacd_coordinator_cdda_sink(SegaCDCoordinator*c,SegaCDCddaSample sink,void*user)
{if(c && !c->busy[0] && !c->busy[1]){c->io.cdda_sample=sink;c->io.cdda_sample_user=user;}}
uint16_t segacd_coordinator_vdp_read16(SegaCDCoordinator*c,uint32_t a)
{
    uint16_t value=m68k_step_read16(&c->cpu[0],a);a&=0xFFFFFFu;
    if(a>=0x200000u && a<0x240000u) {
        /* Hardware p61 documents this delay. ares's bus-external.cpp also
         * models the latch; initial CPU-prefetch content remains unqualified. */
        uint16_t previous=c->devices->vdp_word_latch;
        c->devices->vdp_word_latch=value;return previous;
    }
    return value;
}
void segacd_coordinator_main_stall(SegaCDCoordinator*c,uint32_t cycles)
{
    if(!c || !cycles)return;
    if(c->cpu[0].fault_escape)c->cpu[0].charge+=cycles;
    else {c->cpu[0].state.cycles+=cycles;account(c,0,cycles);advance_devices(c);}
}
void segacd_coordinator_media(SegaCDCoordinator*c,SegaCDReadSector read_sector,void*media_user,
                             SegaCDCddaSector sink,void*cdda_user)
{
    if(c && !c->busy[0] && !c->busy[1]) {
        c->io.read_sector=read_sector;c->io.media_user=media_user;
        c->io.cdda_sector=sink;c->io.cdda_user=cdda_user;
    }
}
static void idle_until(SegaCDCoordinator*c,uint64_t deadline)
{
    uint64_t next=next_event_time(c,deadline);
    if(c->cpu[0].state.stopped && c->time[0]<next)c->time[0]=next;
    if((!segacd_sub_runnable(c->devices) || c->cpu[1].state.stopped) && c->time[1]<next)c->time[1]=next;
    advance_devices(c);
}
M68kStepResult segacd_coordinator_step(SegaCDCoordinator *c)
{
    if(!c || c->busy[0] || c->busy[1])return M68K_STEP_REENTERED;
    if(device_fault(c)){c->last_cpu=1;return c->last_result=M68K_STEP_BUS_ERROR;}
    M68kStepResult r=refresh_sub(c);if(r!=M68K_STEP_OK)return c->last_result=r;
    advance_devices(c);
    unsigned which=segacd_sub_runnable(c->devices) && c->time[1]<c->time[0]?1u:0u;
    r=execute_cpu(c,which);
    if(r==M68K_STEP_STOPPED) {
        unsigned other=which^1u;
        if(other==0 || segacd_sub_runnable(c->devices))r=execute_cpu(c,other);
        if(r==M68K_STEP_STOPPED){idle_until(c,c->device_time+SEGACD_TIMER_DIVISOR);r=M68K_STEP_OK;}
        else if(c->time[which]<c->time[other])c->time[which]=c->time[other];
    }
    advance_devices(c);
    if(r==M68K_STEP_OK && device_fault(c)){c->last_cpu=1;r=M68K_STEP_BUS_ERROR;}
    return c->last_result=r;
}
M68kStepResult segacd_coordinator_run_until(SegaCDCoordinator*c,uint64_t deadline)
{
    if(!c)return M68K_STEP_REENTERED;
    while(c->time[0]<deadline || (segacd_sub_runnable(c->devices) && c->time[1]<deadline)) {
        if(device_fault(c)){c->last_cpu=1;return c->last_result=M68K_STEP_BUS_ERROR;}
        /* Never step an all-STOP system past a host raster deadline. */
        if(c->cpu[0].state.stopped && (!segacd_sub_runnable(c->devices) || c->cpu[1].state.stopped)) {
            advance_devices(c);
            unsigned sub_level=segacd_irq_level(c->devices);
            int main_ready=c->main_irq && (c->main_irq==7 || c->main_irq>((c->cpu[0].state.regs.SR>>8)&7u));
            int sub_ready=sub_level && sub_level>((c->cpu[1].state.regs.SR>>8)&7u);
            if(!main_ready && !sub_ready){idle_until(c,deadline);continue;}
        }
        M68kStepResult r=segacd_coordinator_step(c);if(r!=M68K_STEP_OK)return r;
    }
    advance_devices(c);
    if(device_fault(c)){c->last_cpu=1;return c->last_result=M68K_STEP_BUS_ERROR;}
    return M68K_STEP_OK;
}
