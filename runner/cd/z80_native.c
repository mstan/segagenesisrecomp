#include "z80_native.h"
#include <setjmp.h>
#include <string.h>

/* NTSC master/15 is the Z80 clock. Retain the fractional Sub-clock phase
 * across every instruction; reset/held intervals start a new clock phase.
 * Bus effects use generated instruction boundaries, not electrical T states. */
enum { Z80_NUMER=187500000, Z80_DENOM=53693175 };
Z80State g_z80;
uint64_t g_sync_deadline=UINT64_MAX,g_frame_ic,g_diff_icount;
uint16_t g_enter_ring[SMS_ENTER_RING_SIZE],g_dbg_pc;
uint32_t g_enter_pos;
int g_diff_freeze,g_diff_active;
static SegaCDZ80 *active;
static jmp_buf fault_escape;
static uint64_t cycle_start,time_start,remainder_start,wait_charge;

static void fail(SegaCDZ80Result r,M68kStepResult bus)
{
    active->result=r;active->bus_result=bus;
    longjmp(fault_escape,1);
}
static uint64_t cycle_offset(uint64_t cycles,uint64_t remainder,uint64_t *out)
{
    /* Instruction/IRQ costs are small; split keeps the API valid for long
     * clock histories without multiplying an absolute cycle count. */
    uint64_t whole=cycles/Z80_DENOM;
    uint64_t scaled=(cycles%Z80_DENOM)*Z80_NUMER+remainder;
    if(out)*out=scaled%Z80_DENOM;
    return whole*Z80_NUMER+scaled/Z80_DENOM;
}
uint64_t segacd_z80_bus_time(const SegaCDZ80 *s)
{
    if(active==s)return time_start+wait_charge+
        cycle_offset(g_z80.cyc-cycle_start,remainder_start,NULL);
    return s?s->time:0;
}
static void synchronize(void)
{
    M68kStepResult r=segacd_coordinator_run_until(active->coordinator,
                                                segacd_z80_bus_time(active));
    if(r!=M68K_STEP_OK)fail(SEGACD_Z80_BUS_ERROR,r);
}
static void after_external(uint64_t before,M68kStepResult r)
{
    uint64_t now=active->coordinator->device_time;
    if(now>before)wait_charge+=now-before;
    if(r!=M68K_STEP_OK)fail(SEGACD_Z80_BUS_ERROR,r);
}
uint8_t sms_read8(uint16_t a)
{
    uint8_t value=0;
    if(!active)return 0xff; /* ABI is callable only from segacd_z80_step. */
    synchronize();
    if(a<0x8000u)return active->bus.read(active->user,a,segacd_z80_bus_time(active));
    uint32_t main=(((uint32_t)active->bus.bank(active->user)&0x1ffu)<<15)+(a-0x8000u);
    uint64_t before=active->coordinator->device_time;
    M68kStepResult r=segacd_coordinator_external_main_read8(active->coordinator,main,&value);
    after_external(before,r);return value;
}
void sms_write8(uint16_t a,uint8_t value)
{
    if(!active)return;
    synchronize();
    if(a<0x8000u){active->bus.write(active->user,a,value,segacd_z80_bus_time(active));return;}
    uint32_t main=(((uint32_t)active->bus.bank(active->user)&0x1ffu)<<15)+(a-0x8000u);
    uint64_t before=active->coordinator->device_time;
    M68kStepResult r=segacd_coordinator_external_main_write8(active->coordinator,main,value);
    after_external(before,r);
}
uint8_t sms_io_in(uint8_t p)
{
    (void)p;
    if(active) {
        synchronize();
        fail(SEGACD_Z80_BUS_ERROR,M68K_STEP_UNSUPPORTED);
    }
    return 0xff;
}
void sms_io_out(uint8_t p,uint8_t v)
{
    (void)p;(void)v;
    if(!active)return;
    synchronize();
    fail(SEGACD_Z80_BUS_ERROR,M68K_STEP_UNSUPPORTED);
}
int sms_slot_bank(uint16_t a){(void)a;return 0;}
void sms_halt(void){g_z80.halted=true;}
void sms_sync(void){if(active)synchronize();}
void sms_dispatch_miss(uint16_t pc)
{
    if(active){active->miss_pc=pc;fail(SEGACD_Z80_MISS,M68K_STEP_MISS);}
}
void sms_diff_abort(void){if(active)fail(SEGACD_Z80_BAD_MODULE,M68K_STEP_MISS);}
void sms_diff_enter(uint16_t pc){(void)pc;}

static void accept_irq(void)
{
    if(g_z80.ei_block){g_z80.ei_block=0;return;}
    if(!active->irq_pending || !g_z80.iff1)return;
    active->irq_pending=0;g_z80.halted=false;g_z80.iff1=g_z80.iff2=false;
    g_z80.r=(uint8_t)((g_z80.r&0x80u)|((g_z80.r+1u)&0x7fu));
    uint16_t target;
    if(g_z80.im<=1){g_z80.cyc+=13;target=0x38;}
    else if(g_z80.im==2){
        g_z80.cyc+=19;target=sms_read16((uint16_t)((g_z80.i<<8)|0xff));
    }else{fail(SEGACD_Z80_BAD_MODULE,M68K_STEP_MISS);return;}
    g_z80.sp=(uint16_t)(g_z80.sp-2u);sms_write16(g_z80.sp,g_z80.pc);g_z80.pc=target;
}
int segacd_z80_init(SegaCDZ80 *s,SegaCDCoordinator *c,const SegaCDZ80Bus *b,
                   void *user,void (*step)(void))
{
    if(!s||!c||!b||!b->read||!b->write||!b->bank||!step||active)return 0;
    memset(s,0,sizeof *s);s->coordinator=c;s->bus=*b;s->user=user;s->step=step;
    s->state.a=0xff;s->state.f=0xff;s->state.sp=0xffff;
    segacd_z80_reset(s);return 1;
}
void segacd_z80_reset(SegaCDZ80 *s)
{
    if(!s||active||s->result!=SEGACD_Z80_OK)return;
    s->state.pc=0;s->state.iff1=s->state.iff2=false;s->state.halted=false;
    s->state.im=0;s->state.ei_block=0;s->irq_pending=0;
    s->time=s->coordinator->device_time;s->remainder=0;
}
void segacd_z80_irq(SegaCDZ80 *s){if(s)s->irq_pending=1;}
int segacd_z80_hold_until(SegaCDZ80 *s,uint64_t clock)
{
    if(!s||active||s->result!=SEGACD_Z80_OK||clock<s->time||clock<s->coordinator->device_time)return 0;
    s->time=clock;s->remainder=0;return 1;
}
SegaCDZ80Result segacd_z80_step(SegaCDZ80 *s)
{
    if(!s)return SEGACD_Z80_BAD_MODULE;
    if(active)return SEGACD_Z80_REENTERED;
    if(s->result!=SEGACD_Z80_OK)return s->result;
    if(!s->step)return s->result=SEGACD_Z80_BAD_MODULE;
    active=s;s->in_step=1;g_z80=s->state;cycle_start=g_z80.cyc;time_start=s->time;
    remainder_start=s->remainder;wait_charge=0;
    if(!setjmp(fault_escape)) {
        if(g_z80.halted){g_z80.cyc+=4;g_z80.r=(uint8_t)((g_z80.r&0x80u)|((g_z80.r+1u)&0x7fu));}
        else s->step();
        if(g_z80.cyc<=cycle_start)fail(SEGACD_Z80_BAD_MODULE,M68K_STEP_MISS);
        /* Settle the just-completed instruction before sampling /INT. */
        synchronize();accept_irq();synchronize();++s->retired;
    }
    /* Faults are terminal. Preserve committed effects/partial architecture;
     * never replay the operation or manufacture a successful read value. */
    s->state=g_z80;s->time=time_start+wait_charge+
        cycle_offset(g_z80.cyc-cycle_start,remainder_start,&s->remainder);
    s->wait_ticks+=wait_charge;s->in_step=0;active=NULL;return s->result;
}
