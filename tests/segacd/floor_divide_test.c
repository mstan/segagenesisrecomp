#include "m68k_interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
M68KState g_cpu;
uint8_t g_rom[0x400000],g_ram[0x10000];
uint8_t *g_rom_ext;uint32_t g_rom_ext_size;
uint64_t g_native_insn_count,g_frame_count;
uint32_t g_cycle_accumulator,g_audio_cycle_counter,g_vblank_threshold=0xFFFFFFFFu;
static int rte;
int *g_rte_pending_ptr=&rte;
int g_split_sp_popped,g_early_return;
static unsigned bus_events;
static uint8_t bus_op[16];
static uint32_t bus_addr[16];
static void bus_event(uint8_t op,uint32_t a){if(a>=0xFF0600u && a<0xFF0700u && bus_events<16){bus_op[bus_events]=op;bus_addr[bus_events]=a;++bus_events;}}
void glue_check_vblank(void){CHECK(0);}
int game_instruction_hook_site(uint32_t pc){(void)pc;return 0;}
int genesis_game_instruction_hook(uint32_t pc){(void)pc;return 0;}
uint8_t m68k_read8(uint32_t a){a&=0xFFFFFFu;bus_event(1,a);if(a<sizeof g_rom)return g_rom[a];CHECK(a>=0xFF0000u);return g_ram[a&65535];}
uint16_t m68k_read16(uint32_t a){uint16_t hi=m68k_read8(a);uint16_t lo=m68k_read8(a+1);return (uint16_t)((hi<<8)|lo);}
uint32_t m68k_read32(uint32_t a){uint32_t hi=m68k_read16(a);uint32_t lo=m68k_read16(a+2);return (hi<<16)|lo;}
void m68k_write8(uint32_t a,uint8_t v){a&=0xFFFFFFu;bus_event(2,a);CHECK(a>=0xFF0000u);g_ram[a&65535]=v;}
void m68k_write16(uint32_t a,uint16_t v){m68k_write8(a,(uint8_t)(v>>8));m68k_write8(a+1,(uint8_t)v);}
void m68k_write32(uint32_t a,uint32_t v){m68k_write16(a,(uint16_t)(v>>16));m68k_write16(a+2,(uint16_t)v);}
static M68kiStatus run(uint32_t dividend,uint16_t divisor,int is_signed)
{
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.D[0]=dividend;g_cpu.SR=0x2011;
    g_rom[0x200]=(uint8_t)(is_signed?0x81:0x80);g_rom[0x201]=0xFC;
    g_rom[0x202]=(uint8_t)(divisor>>8);g_rom[0x203]=(uint8_t)divisor;
    return m68k_interp_step();
}
int main(void)
{
    CHECK(run(0x10000,1,0)==M68KI_OK);CHECK(g_cpu.D[0]==0x10000 && (g_cpu.SR&0x13)==0x12);
    CHECK(run(0xFFFF,1,0)==M68KI_OK);CHECK(g_cpu.D[0]==0xFFFF && !(g_cpu.SR&3));
    CHECK(run(0x1FFFF,2,0)==M68KI_OK);CHECK(g_cpu.D[0]==0x1FFFF && !(g_cpu.SR&3));
    CHECK(run(0xFFFFFFFFu,0xFFFF,0)==M68KI_OK);CHECK(g_cpu.D[0]==0xFFFFFFFFu && (g_cpu.SR&2));
    CHECK(run((uint32_t)-7,3,1)==M68KI_OK);CHECK(g_cpu.D[0]==0xFFFFFFFEu && !(g_cpu.SR&3));
    CHECK(run(0x80000000u,0xFFFF,1)==M68KI_OK);CHECK(g_cpu.D[0]==0x80000000u && (g_cpu.SR&2));
    CHECK(run(0x12345678,0,0)==M68KI_HALT_UNIMPL);CHECK(g_cpu.PC==0x200 && g_cpu.D[0]==0x12345678 && g_m68ki_bad_pc==0x200 && g_m68ki_bad_op==0x80FC);
    CHECK(run(0x12345678,0,1)==M68KI_HALT_UNIMPL);CHECK(g_cpu.PC==0x200 && g_m68ki_bad_op==0x81FC);
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.SR=0x2000;g_cpu.A[7]=0xFFFF0022u;g_cpu.D[0]=0xAABBCC00u;
    g_rom[0x200]=0x10;g_rom[0x201]=0x27;g_ram[0x20]=0x42;
    CHECK(m68k_interp_step()==M68KI_OK && g_cpu.A[7]==0xFFFF0020u && g_cpu.D[0]==0xAABBCC42u);
    g_cpu.PC=0x200;g_rom[0x200]=0x1E;g_rom[0x201]=0xC0;g_cpu.D[0]=0x3E;
    CHECK(m68k_interp_step()==M68KI_OK && g_cpu.A[7]==0xFFFF0022u && g_ram[0x20]==0x3E);
    g_cpu.PC=0x200;g_cpu.A[7]=0xFFFF0020u;g_cpu.SR=0x2010;g_rom[0x200]=0xBF;g_rom[0x201]=0x0F;g_ram[0x20]=1;g_ram[0x22]=3;
    CHECK(m68k_interp_step()==M68KI_OK && g_cpu.A[7]==0xFFFF0024u && g_cpu.SR==0x2010);
    g_cpu.PC=0x200;g_rom[0x200]=0xE3;g_rom[0x201]=0xA0;g_cpu.D[0]=UINT32_MAX;g_cpu.D[1]=31;g_cpu.SR=0x2000;
    CHECK(m68k_interp_step()==M68KI_OK && g_cpu.D[0]==0x80000000u && g_cpu.SR==0x2019);
    for(unsigned direction=0;direction<2;++direction)for(unsigned x=0;x<2;++x) {
        g_cpu.PC=0x200;g_rom[0x200]=(uint8_t)(direction?0xE3:0xE2);g_rom[0x201]=0xB0;
        g_cpu.D[0]=0x12345678;g_cpu.D[1]=0;g_cpu.SR=(uint16_t)(0x2000|(x?16u:1u));
        CHECK(m68k_interp_step()==M68KI_OK && g_cpu.D[0]==0x12345678 && g_cpu.SR==(x?0x2011:0x2000));
    }
    /* Original-68000 memory CLR is a read followed by a write. Trace byte
     * bus callbacks to prove ordering and single postincrement (A7 byte uses
     * a two-byte step); long accesses comprise two ordered word-sized halves. */
    const struct {uint16_t opcode;unsigned bytes,increment;} clr_cases[]={
        {0x4218,1,1},{0x4258,2,2},{0x4298,4,4}
    };
    for(unsigned n=0;n<sizeof clr_cases/sizeof *clr_cases;++n) {
        memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.A[0]=0xFF0600u;g_cpu.SR=0x2011;
        g_rom[0x200]=(uint8_t)(clr_cases[n].opcode>>8);g_rom[0x201]=(uint8_t)clr_cases[n].opcode;
        for(unsigned k=0;k<clr_cases[n].bytes;++k)g_ram[0x600+k]=(uint8_t)(0xA0u+k);
        bus_events=0;CHECK(m68k_interp_step()==M68KI_OK);
        unsigned expected=clr_cases[n].bytes*2;
        CHECK(bus_events==expected && g_cpu.A[0]==0xFF0600u+clr_cases[n].increment);
        for(unsigned k=0;k<clr_cases[n].bytes;++k)if(!(bus_op[k]==1 && bus_addr[k]==0xFF0600u+k)){fprintf(stderr,"CLR case %u read event %u: op=%u address=%06X A0=%06X events=%u\n",n,k,bus_op[k],bus_addr[k],g_cpu.A[0],bus_events);exit(1);}
        for(unsigned k=0;k<clr_cases[n].bytes;++k)CHECK(bus_op[clr_cases[n].bytes+k]==2 && bus_addr[clr_cases[n].bytes+k]==0xFF0600u+k && g_ram[0x600+k]==0);
        CHECK((g_cpu.SR&0x1Fu)==0x14);
    }
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.A[0]=0xFF0641u;g_cpu.SR=0x2011;
    g_rom[0x200]=0x42;g_rom[0x201]=0x20;g_ram[0x640]=0xA5;bus_events=0;
    CHECK(m68k_interp_step()==M68KI_OK && bus_events==2 && bus_op[0]==1 && bus_op[1]==2 && bus_addr[0]==0xFF0640u && bus_addr[1]==0xFF0640u && g_cpu.A[0]==0xFF0640u && g_ram[0x640]==0);
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.A[0]=0xFF0640u;g_cpu.SR=0x2011;
    g_rom[0x200]=0x42;g_rom[0x201]=0x68;g_rom[0x202]=0x00;g_rom[0x203]=0x20;g_ram[0x660]=0x12;g_ram[0x661]=0x34;bus_events=0;
    CHECK(m68k_interp_step()==M68KI_OK && bus_events==4 && g_cpu.A[0]==0xFF0640u);
    CHECK(bus_op[0]==1 && bus_op[1]==1 && bus_op[2]==2 && bus_op[3]==2 && bus_addr[0]==0xFF0660u && bus_addr[1]==0xFF0661u && bus_addr[2]==0xFF0660u && bus_addr[3]==0xFF0661u && g_ram[0x660]==0 && g_ram[0x661]==0);
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.SR=0x2011;
    g_rom[0x200]=0x42;g_rom[0x201]=0x39;g_rom[0x202]=0x00;g_rom[0x203]=0xFF;g_rom[0x204]=0x06;g_rom[0x205]=0x64;g_ram[0x664]=0x5A;bus_events=0;
    CHECK(m68k_interp_step()==M68KI_OK && bus_events==2 && bus_op[0]==1 && bus_op[1]==2 && bus_addr[0]==0xFF0664u && bus_addr[1]==0xFF0664u && g_ram[0x664]==0);
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.PC=0x200;g_cpu.A[7]=0xFF0620u;g_cpu.SR=0x2011;
    g_rom[0x200]=0x42;g_rom[0x201]=0x1F;g_ram[0x620]=0xAB;bus_events=0;
    CHECK(m68k_interp_step()==M68KI_OK && bus_events==2 && bus_op[0]==1 && bus_op[1]==2);
    CHECK(bus_addr[0]==0xFF0620u && bus_addr[1]==0xFF0620u && g_cpu.A[7]==0xFF0622u && g_ram[0x620]==0 && (g_cpu.SR&0x1Fu)==0x14);
    puts("Existing correctness floor: divide boundaries, CLR read-before-write and loud zero-divide rejection pass");
    return 0;
}
