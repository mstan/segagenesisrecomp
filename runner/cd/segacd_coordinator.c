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
    /* A third master may be the current limiting clock. External Main byte
     * WAIT is the exception: its Sub owner and devices must continue advancing
     * while the Z80/master making that access is synchronously paused. */
    if(!c->external_active && c->device_limit) {
        uint64_t limit=c->device_limit(c->host_user);
        if(now>limit)now=limit;
    }
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
static void observe_cdd_submission(SegaCDCoordinator*c,SegaCDCpu cpu,uint32_t a,int word)
{
    a&=0xFFFFFFu;
    if(cpu!=SEGACD_SUB || (word?a!=0xFF804Au:a!=0xFF804Bu))return;
    SegaCDDrive*d=&c->devices->drive;
    /* Register 4B is the packet-submit edge. Payload-byte writes before it
     * are only staging; an ignored submit (HOCK low) leaves no pending
     * command. Command zero is a status poll, not a transport request. */
    if(!d->command_pending || !d->pending[0])return;
    uint64_t sequence=c->cdd_submission_total++;
    SegaCDCddSubmission*entry=&c->cdd_submissions[sequence%SEGACD_CDD_TRACE_CAPACITY];
    memcpy(entry->packet,d->pending,sizeof entry->packet);
    entry->instruction_pc=c->cpu[SEGACD_SUB].state.instruction_pc;
    entry->device_clock=d->cycles;
    entry->command_deadline=d->command_deadline;
    entry->lba=d->lba;
    entry->status=d->status;
}
static M68kStepBusResult bus_write8(void *user,uint32_t a,uint8_t v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_write8(c->devices,b->cpu,a,v);
    if(r==SEGACD_OK)observe_cdd_submission(c,b->cpu,a,0);
    if(r!=SEGACD_UNMAPPED)return translate(r);
    if(b->cpu==SEGACD_SUB || main_cd_port(a) || !c->host_main.write8)return M68K_BUS_FAULT;
    return c->host_main.write8(c->host_user,a,v);
}
static M68kStepBusResult bus_write16(void *user,uint32_t a,uint16_t v)
{
    SegaCDBusBinding*b=user;SegaCDCoordinator*c=b->owner;
    SegaCDResult r=segacd_write16(c->devices,b->cpu,a,v);
    if(r==SEGACD_OK)observe_cdd_submission(c,b->cpu,a,1);
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
    uint32_t entry_pc=cpu->state.regs.PC;
    if(m68k_step_interrupt(cpu,level)) {
        ++c->interrupts[which][level];
        c->last_interrupt_pc[which]=entry_pc;
        c->last_interrupt_target[which]=cpu->state.regs.PC;
        c->last_interrupt_vector[which]=cpu->state.exception_vector;
        c->last_interrupt_level[which]=(uint8_t)level;
        account(c,which,cpu->state.cycles-before);return M68K_STEP_OK;
    }
    if(cpu->state.fault)return (M68kStepResult)cpu->state.fault;
    if(cpu->state.stopped)return M68K_STEP_STOPPED;
    c->busy[which]=1;
    M68kStepResult result=segacd_catalog_index_execute(c->catalog_index[which],&c->catalog_lookup[which],cpu,
                                               c->modules[which],c->module_count[which]);
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
    c->modules[cpu]=modules;c->module_count[cpu]=count;
    segacd_catalog_index_destroy(c->catalog_index[cpu]);c->catalog_index[cpu]=NULL;
    if(c->catalog_index_enabled)
        c->catalog_index[cpu]=segacd_catalog_index_create(SEGACD_INDEX_DEFAULT_LIMIT);
    segacd_catalog_lookup_reset(&c->catalog_lookup[cpu]);return 1;
}
int segacd_coordinator_catalog_index(SegaCDCoordinator *c,int enabled)
{
    if(!c||c->busy[0]||c->busy[1]||c->external_active)return 0;
    for(unsigned cpu=0;cpu<2;++cpu) {
        if(!enabled){segacd_catalog_index_destroy(c->catalog_index[cpu]);c->catalog_index[cpu]=NULL;}
        else if(!c->catalog_index[cpu])
            c->catalog_index[cpu]=segacd_catalog_index_create(SEGACD_INDEX_DEFAULT_LIMIT);
    }
    c->catalog_index_enabled=(uint8_t)(enabled!=0);return 1;
}
int segacd_coordinator_floor(SegaCDCoordinator *c,M68kStepResult (*step)(M68kStepContext *))
{
    if(!c || c->busy[0] || c->busy[1])return 0;
    c->floor_step=step;return 1;
}
void segacd_coordinator_main_irq(SegaCDCoordinator*c,unsigned level){if(c && level<=7)c->main_irq=level;}
void segacd_coordinator_host_clock(SegaCDCoordinator*c,void (*advance)(void *,uint64_t),uint64_t (*next)(void *))
{if(c && !c->busy[0] && !c->busy[1]){c->host_advance=advance;c->host_next_event=next;}}
void segacd_coordinator_device_limit(SegaCDCoordinator*c,uint64_t (*limit)(void *))
{if(c && !c->busy[0] && !c->busy[1])c->device_limit=limit;}
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
    if(c->external_active){c->external_stall_seen=1;return;}
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
size_t segacd_coordinator_cdd_trace(const SegaCDCoordinator*c,SegaCDCddSnapshot*snapshot,
                                    SegaCDCddSubmission*events,size_t capacity)
{
    if(!c)return 0;
    const SegaCDDrive*d=&c->devices->drive;
    size_t retained=c->cdd_submission_total>SEGACD_CDD_TRACE_CAPACITY?
        SEGACD_CDD_TRACE_CAPACITY:(size_t)c->cdd_submission_total;
    if(snapshot) {
        memcpy(snapshot->tx,d->tx,sizeof snapshot->tx);
        memcpy(snapshot->pending,d->pending,sizeof snapshot->pending);
        snapshot->control=d->control;
        snapshot->status=d->status;
        snapshot->command_pending=d->command_pending;
        snapshot->lba=d->lba;
        snapshot->seek_target=d->seek_target;
        snapshot->device_clock=d->cycles;
        snapshot->command_deadline=d->command_deadline;
        snapshot->accepted_total=c->cdd_submission_total;
        snapshot->retained_count=retained;
    }
    if(!events || !capacity)return 0;
    size_t count=capacity<retained?capacity:retained;
    uint64_t first=c->cdd_submission_total-count;
    for(size_t n=0;n<count;++n)
        events[n]=c->cdd_submissions[(first+n)%SEGACD_CDD_TRACE_CAPACITY];
    return count;
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
    if(!c || c->busy[0] || c->busy[1] || c->external_active)return M68K_STEP_REENTERED;
    /* A synchronous caller (notably the Z80 instruction bridge) cannot wait
     * for an independent master that is itself waiting for this call to
     * return. Fail before advancing either CPU instead of reporting success
     * with device_time stranded below the requested deadline. */
    if(c->device_limit && deadline>c->device_limit(c->host_user))
        return c->last_result=M68K_STEP_NO_WAIT_HANDLER;
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

typedef struct { uint32_t address; uint8_t value; int write; } ExternalByte;

static M68kStepResult external_byte_once(SegaCDCoordinator *c,ExternalByte *op,int *wait)
{
    *wait=0;
    SegaCDResult r;
    if(op->write)r=segacd_write8(c->devices,SEGACD_MAIN,op->address,op->value);
    else r=segacd_read8(c->devices,SEGACD_MAIN,op->address,&op->value);
    if(r==SEGACD_WAIT){*wait=1;return M68K_STEP_OK;}
    if(r==SEGACD_UNMAPPED) {
        if(main_cd_port(op->address))return M68K_STEP_BUS_ERROR;
        M68kStepBusResult host;
        if(op->write) {
            if(!c->host_main.write8)return M68K_STEP_BUS_ERROR;
            host=c->host_main.write8(c->host_user,op->address,op->value);
        } else {
            if(!c->host_main.read8)return M68K_STEP_BUS_ERROR;
            uint8_t value;
            host=c->host_main.read8(c->host_user,op->address,&value);
            if(host==M68K_BUS_OK)op->value=value;
        }
        if(host==M68K_BUS_OK)return M68K_STEP_OK;
        return host==M68K_BUS_WAIT?M68K_STEP_NO_WAIT_HANDLER:M68K_STEP_BUS_ERROR;
    }
    if(r==SEGACD_OK)return M68K_STEP_OK;
    return r==SEGACD_ADDRESS_ERROR?M68K_STEP_ADDRESS_ERROR:M68K_STEP_BUS_ERROR;
}

static int external_stop_has_event(SegaCDCoordinator *c)
{
    return c->devices->timer_reload ||
        (c->host_next_event && c->host_next_event(c->host_user)!=UINT64_MAX) ||
        segacd_cdd_next_event(&c->devices->drive)!=UINT64_MAX ||
        c->devices->cdc.decoder_irq_clocks || c->devices->dma.active ||
        c->devices->stamp.busy;
}

static M68kStepResult external_main_byte(SegaCDCoordinator *c,ExternalByte *op)
{
    if(!c || c->busy[0] || c->busy[1] || c->external_active)return M68K_STEP_REENTERED;
    op->address&=0xFFFFFFu;
    c->external_active=1;c->busy[SEGACD_MAIN]=1;c->external_stall_seen=0;
    M68kStepResult result=M68K_STEP_OK;
    int wait=0;
    /* An immediate byte is still allowed with a zero owner-step budget, but
     * device faults never permit host callbacks or a synthetic value. */
    if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
    result=external_byte_once(c,op,&wait);
    if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
    if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
    if(!wait)goto done;
    if(c->wait_limit==0){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
    if(!segacd_sub_runnable(c->devices)){result=M68K_STEP_STOPPED;goto done;}

    result=refresh_sub(c);
    if(result!=M68K_STEP_OK)goto done;
    advance_devices(c);
    if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
    if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
    result=external_byte_once(c,op,&wait);
    if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
    if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
    if(!wait)goto done;

    /* The budget counts actual scheduler/owner advances, not the initial
     * immediate byte probe. Sub keeps its existing clock position. Main is
     * paused and catches up only after Sub genuinely advances beyond it. */
    for(unsigned n=0;n<c->wait_limit;++n) {
        if(!segacd_sub_runnable(c->devices)){result=M68K_STEP_STOPPED;goto done;}
        result=execute_cpu(c,SEGACD_SUB);
        if(result!=M68K_STEP_OK && result!=M68K_STEP_STOPPED)goto done;
        if(c->time[0]<c->time[1])c->time[0]=c->time[1];
        advance_devices(c);
        if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
        if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
        result=refresh_sub(c);
        if(result!=M68K_STEP_OK)goto done;
        result=external_byte_once(c,op,&wait);
        if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
        if(device_fault(c)){c->last_cpu=1;result=M68K_STEP_BUS_ERROR;goto done;}
        if(!wait)goto done;
        if(result!=M68K_STEP_OK)goto done;
        if(!segacd_sub_runnable(c->devices)){result=M68K_STEP_STOPPED;goto done;}
        if(c->cpu[SEGACD_SUB].state.stopped) {
            /* Bounded real events may wake a stopped owner; unrelated events
             * cannot fabricate a grant and eventually exhaust wait_limit. */
            if(!external_stop_has_event(c)){result=M68K_STEP_STOPPED;goto done;}
            uint64_t next=next_event_time(c,UINT64_MAX);
            if(next<=c->device_time){result=M68K_STEP_STOPPED;goto done;}
            if(c->time[SEGACD_SUB]<next)c->time[SEGACD_SUB]=next;
            if(c->time[0]<c->time[SEGACD_SUB])c->time[0]=c->time[SEGACD_SUB];
            advance_devices(c);
            if(c->external_stall_seen){result=M68K_STEP_NO_WAIT_HANDLER;goto done;}
        }
    }
    result=M68K_STEP_NO_WAIT_HANDLER;
done:
    c->busy[SEGACD_MAIN]=0;c->external_active=0;c->external_stall_seen=0;
    return result;
}

M68kStepResult segacd_coordinator_external_main_read8(SegaCDCoordinator*c,uint32_t a,uint8_t*v)
{
    if(!v)return M68K_STEP_BUS_ERROR;
    ExternalByte op={a,0,0};M68kStepResult r=external_main_byte(c,&op);
    if(r==M68K_STEP_OK)*v=op.value;
    return r;
}

M68kStepResult segacd_coordinator_external_main_write8(SegaCDCoordinator*c,uint32_t a,uint8_t v)
{
    ExternalByte op={a,v,1};return external_main_byte(c,&op);
}
