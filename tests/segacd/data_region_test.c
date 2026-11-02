#include "segacd_coordinator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static SegaCD devices[2];
static SegaCDCoordinator coordinators[2];
static uint8_t bios[SEGACD_BIOS_BYTES],host[0x1000000];
static unsigned test_mode,resolves;
static M68kStepBusResult host_read(void *u,uint32_t a,uint16_t *v)
{(void)u;*v=(uint16_t)(((unsigned)host[a]<<8)|host[a+1]);return M68K_BUS_OK;}
static int host_region(void *u,uint32_t a,size_t bytes,int write,M68kStepDataRegion *out)
{
    (void)u;(void)a;(void)bytes;(void)write;++resolves;
    out->address=test_mode==5?0xfffff0u:0;
    out->bytes=test_mode==5?0x80u:sizeof host;out->data=host;return 1;
}
static void operation(M68kStepContext *cpu)
{
    static const uint32_t first[]={0x40000,0xa11ffe,0x240000,0xa12200,0x40000,0x40000};
    static const uint32_t second[]={0x20000,0xa12002,0x200000,0xa12100,0x80000,0x80000};
    cpu->state.regs.D[0]=m68k_step_read16(cpu,first[test_mode]);
    cpu->state.regs.D[1]=m68k_step_read16(cpu,second[test_mode]);
}
static const M68kStepEntry entry={0x100,{0x4e71},1,8,operation};
int main(void)
{
    bios[2]=0xff;bios[6]=1;bios[0x100]=0x4e;bios[0x101]=0x71;
    memset(host,0xa5,sizeof host);
    for(test_mode=0;test_mode<6;++test_mode) {
        M68kStepResult result[2];
        for(unsigned fast=0;fast<2;++fast) {
            CHECK(segacd_init(&devices[fast],bios,sizeof bios));
            devices[fast].main_control=1;devices[fast].memory_mode=0;
            M68kStepBus bus={0};bus.read16=host_read;
            CHECK(segacd_coordinator_init(&coordinators[fast],&devices[fast],&bus,NULL));
            CHECK(segacd_coordinator_main_data_region(&coordinators[fast],host_region));
            resolves=0;
            M68kStepContext *cpu=&coordinators[fast].cpu[0];
            result[fast]=fast?m68k_step_execute_tracked(cpu,&entry):m68k_step_execute(cpu,&entry);
            if(test_mode==4&&fast)CHECK(resolves==1);
        }
        CHECK(result[0]==result[1]);
        CHECK(!memcmp(&devices[0],&devices[1],sizeof devices[0]));
        for(unsigned cpu=0;cpu<2;++cpu)
            CHECK(!memcmp(&coordinators[0].cpu[cpu].state,&coordinators[1].cpu[cpu].state,sizeof(M68kStepState)));
    }
    puts("Owned data regions: host views cannot cross CD memory/MMIO/WAIT apertures");
    return 0;
}
