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
void glue_check_vblank(void){CHECK(0);}
int game_instruction_hook_site(uint32_t pc){(void)pc;return 0;}
int genesis_game_instruction_hook(uint32_t pc){(void)pc;return 0;}
uint8_t m68k_read8(uint32_t a){a&=0xFFFFFFu;if(a<sizeof g_rom)return g_rom[a];CHECK(a>=0xFF0000u);return g_ram[a&65535];}
uint16_t m68k_read16(uint32_t a){return (uint16_t)(((unsigned)m68k_read8(a)<<8)|m68k_read8(a+1));}
uint32_t m68k_read32(uint32_t a){return ((uint32_t)m68k_read16(a)<<16)|m68k_read16(a+2);}
void m68k_write8(uint32_t a,uint8_t v){a&=0xFFFFFFu;CHECK(a>=0xFF0000u);g_ram[a&65535]=v;}
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
    puts("Existing correctness floor: divide boundary/overflow/remainder and loud zero-divide rejection pass");
    return 0;
}
