/* Compare the real cartridge adapter and generated guarded dispatch with its
 * actual interpreter, plus independent boundary/refresh/cycle expectations. */
#include "z80_recomp.h"
#include "game_spec.h"
#include "video/genesis_machine.h"
#include "proof_control_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u mode=%s: %s\n",(unsigned)__LINE__,mode,#x);exit(1);}}while(0)
static const char*mode;
GenesisMachine g_machine;
static uint8_t native_ram[65536],reference_ram[65536];
static z80 reference;
static uint64_t reference_cycles;
int force_fallback,variant_fixture;
typedef struct BusEvent { uint32_t stamp;uint16_t address,legacy_pc;uint8_t value,write; } BusEvent;
static BusEvent events[4096];static unsigned event_count,observe_bus,inject_irq;
static uint32_t stamp;
static void observe(uint16_t a,uint8_t v,int write){
 if(!observe_bus)return;CHECK(event_count<4096);BusEvent*e=&events[event_count++];
 memset(e,0,sizeof *e);e->stamp=stamp;e->address=a;e->value=v;e->write=(uint8_t)write;
#ifdef GEN_DEV_TRACE
 e->legacy_pc=g_machine.z80.pc;
#endif
 if(a==6&&inject_irq){g_machine.z80.int_pending=1;inject_irq=0;}
}
static uint8_t read_byte(void*u,uint16_t a){return ((uint8_t*)u)[a];}
static void write_byte(void*u,uint16_t a,uint8_t v){((uint8_t*)u)[a]=v;}
uint8_t gbus_z80_read(GenesisBus*b,uint16_t a){(void)b;uint8_t v=native_ram[a];observe(a,v,0);return v;}
void gbus_z80_write(GenesisBus*b,uint16_t a,uint8_t v){(void)b;observe(a,v,1);native_ram[a]=v;}
static uint8_t native_read(void*u,uint16_t a){(void)u;return gbus_z80_read(&g_machine.bus,a);}
static void native_write(void*u,uint16_t a,uint8_t v){(void)u;gbus_z80_write(&g_machine.bus,a,v);}
/* A consumer without a game-specific pointer uses the shared CMake entry. */
const GameSpec g_game_spec={0};
static void setup(uint8_t refresh,uint64_t cycles)
{
 memset(&g_machine,0,sizeof g_machine);memset(native_ram,0,sizeof native_ram);memset(reference_ram,0,sizeof reference_ram);
 z80_init(&g_machine.z80);g_machine.z80.read_byte=native_read;g_machine.z80.write_byte=native_write;g_machine.z80.userdata=native_ram;g_machine.z80.int_data=0xff;
 z80_recomp_init();g_z80.r=refresh;g_z80.cyc=cycles;g_z80.sp=0x7000;g_z80.im=1;g_z80.i=0x12;g_z80.iff1=g_z80.iff2=true;
 native_ram[0x38]=0xed;native_ram[0x39]=0x5f;native_ram[0x12ff]=0x38;
 reference_cycles=cycles;reference=g_machine.z80;z80_recomp_mirror_to_interpreter(&reference);reference.userdata=reference_ram;reference.read_byte=read_byte;reference.write_byte=write_byte;
 memcpy(reference_ram,native_ram,sizeof native_ram);
}
static void sync_reference(void){z80_recomp_mirror_to_interpreter(&reference);reference.userdata=reference_ram;memcpy(reference_ram,native_ram,sizeof native_ram);}
static void step(unsigned expected_charge)
{
 unsigned long before=reference.cyc;z80_step(&reference);reference_cycles+=(unsigned long)(reference.cyc-before);
 uint32_t charge=z80_recomp_step_one();if(charge!=expected_charge)fprintf(stderr,"charge=%u expected=%u PC=%04x IRQ=%u\n",charge,expected_charge,g_z80.pc,g_machine.z80.int_pending);CHECK(charge==expected_charge);
 Z80State actual=g_z80;z80_recomp_restore_from_interpreter(&reference);Z80State expected=g_z80;g_z80=actual;expected.cyc=reference_cycles;
 if(memcmp(&expected,&actual,sizeof actual))fprintf(stderr,"state PC=%04x/%04x R=%02x/%02x WZ=%04x/%04x cyc=%llu/%llu iff=%u/%u pending=%u/%u\n",actual.pc,expected.pc,actual.r,expected.r,actual.wz,expected.wz,(unsigned long long)actual.cyc,(unsigned long long)expected.cyc,actual.iff1,expected.iff1,g_machine.z80.int_pending,reference.int_pending);
 CHECK(!memcmp(&expected,&actual,sizeof actual));CHECK(g_machine.z80.int_pending==reference.int_pending);
 CHECK(!memcmp(native_ram,reference_ram,sizeof native_ram));
}
static void irq(void){z80_recomp_assert_irq();z80_gen_int(&reference,0xff);}
int main(int argc,char**argv)
{
 mode=argc>1?argv[1]:"halt";
 if(!strcmp(mode,"halt")){
  const uint8_t refresh[]={0,0x7e,0x7f,0xfe,0xff};
  for(unsigned r=0;r<sizeof refresh;++r)for(unsigned im=1;im<=2;++im){
   setup(refresh[r],0);g_z80.halted=true;g_z80.pc=2;g_z80.im=(uint8_t)im;sync_reference();
   for(unsigned n=0;n<3;++n)step(4);
   CHECK(g_z80.r==(uint8_t)((refresh[r]&0x80)|((refresh[r]+3)&0x7f)));
   irq();step(im==1?17:23);CHECK(g_z80.pc==0x38&&!g_z80.halted);step(9);CHECK(g_z80.a==g_z80.r);
  }
 }else if(!strcmp(mode,"ei")||!strcmp(mode,"prefix")){
  for(force_fallback=1;force_fallback>=0;--force_fallback){
   setup(0xff,0);g_z80.iff1=g_z80.iff2=false;
   unsigned prefixed=!strcmp(mode,"prefix");native_ram[0]=prefixed?0xdd:0xfb;native_ram[1]=prefixed?0xfb:0x76;native_ram[2]=0x76;sync_reference();irq();
   step(prefixed?8:4);CHECK(g_z80.pc==(prefixed?2:1)&&g_z80.iff1&&g_machine.z80.int_pending);
   step(17);CHECK(g_z80.pc==0x38&&!g_z80.iff1&&!g_machine.z80.int_pending);
   if(!prefixed)CHECK(z80_recomp_fallback_steps()==(force_fallback?2u:0u));
  }
 }else if(!strcmp(mode,"irq")){
  setup(0x7f,0);g_z80.pc=6;sync_reference();irq();step(17);CHECK(g_z80.wz==0x38);
 }else if(!strcmp(mode,"cycles")){
  const uint64_t cycles[]={0xfffffffdull,0x100000005ull};
  for(unsigned n=0;n<2;++n){force_fallback=1;setup(0,cycles[n]);g_z80.pc=6;sync_reference();step(4);CHECK(g_z80.cyc==cycles[n]+4);}
 }else if(!strcmp(mode,"guards")){
  setup(0,0);g_z80.iff1=g_z80.iff2=false;g_z80.pc=3;
  const uint8_t code[]={0xfb,0x76,0xdb,1,0xd3,1,0,0};memcpy(native_ram,code,sizeof code);
  native_ram[5]=2;sync_reference();step(10);CHECK(g_z80.b==2&&g_z80.c==0xd3&&z80_recomp_fallback_steps()==1);
  step(4);CHECK(z80_recomp_fallback_steps()==1); /* guarded native NOP */
  native_ram[7]=reference_ram[7]=0x3e;native_ram[8]=reference_ram[8]=0x77;
  step(7);CHECK(g_z80.a==0x77&&g_z80.pc==9&&z80_recomp_fallback_steps()==2);
 }else if(!strcmp(mode,"slice")){
  static uint8_t expected_ram[65536];static BusEvent expected_events[4096];
  for(unsigned kind=0;kind<3;++kind)for(unsigned debt=0;debt<8;++debt)for(unsigned target=0;target<300;target+=7){
   Z80State expected_state={0};z80 expected_legacy={0};uint32_t expected_done=0,expected_stamp=0;unsigned expected_count=0;uint64_t expected_misses=0;
   for(unsigned continuous=0;continuous<2;++continuous){
    force_fallback=kind==2;variant_fixture=kind!=0;setup(0xff,0x100000005ull);g_z80.iff1=g_z80.iff2=false;
    const uint8_t code[]={0x3e,0x55,0x32,0x00,0x40,0xfb,0x76};memcpy(native_ram,code,sizeof code);
    z80_recomp_mirror_to_interpreter(&g_machine.z80);stamp=0;event_count=0;observe_bus=1;inject_irq=1;
    uint32_t done=debt;
    if(continuous)done=z80_recomp_run_slice(done,target,&stamp,15);
    else while(done<target){stamp=done*15;done+=z80_recomp_step_one();}
    observe_bus=0;
    if(!continuous){expected_state=g_z80;expected_legacy=g_machine.z80;expected_done=done;expected_stamp=stamp;expected_count=event_count;expected_misses=z80_recomp_fallback_steps();memcpy(expected_ram,native_ram,sizeof native_ram);memcpy(expected_events,events,event_count*sizeof *events);}
    else{CHECK(!memcmp(&expected_state,&g_z80,sizeof g_z80));CHECK(!memcmp(&expected_legacy,&g_machine.z80,sizeof expected_legacy));CHECK(done==expected_done&&stamp==expected_stamp&&event_count==expected_count);CHECK(!memcmp(expected_ram,native_ram,sizeof native_ram));CHECK(!memcmp(expected_events,events,event_count*sizeof *events));CHECK(expected_misses==z80_recomp_fallback_steps());}
   }
  }
 }else if(!strcmp(mode,"variants")){
  /* Full states/RAM against the actual interpreter, including live WZ. */
  const unsigned pcs[]={0,8,16,24,32,40,48,56,58};
  const unsigned costs[]={7,13,13,16,16,19,19,4,10};
  const uint8_t codes[][3]={{0x3e,0x11,0},{0x32,0,0x40},{0x3a,0,0x40},{0x22,0,0x40},{0x2a,0,0x40},{0xdd,0x86,0},{0xfd,0x96,0},{4,0,0},{1,0x34,0x12}};
  variant_fixture=1;
  for(unsigned op=0;op<9;++op)for(unsigned operand=0;operand<256;++operand){
   force_fallback=0;setup(0xff,0x100000005ull);g_z80.pc=(uint16_t)pcs[op];g_z80.iff1=g_z80.iff2=false;
   g_z80.a=0xa5;g_z80.h=0x56;g_z80.l=0x78;g_z80.ix=0x4100;g_z80.iy=0x4200;
   memcpy(native_ram+pcs[op],codes[op],3);
   if(op==0)native_ram[1]=(uint8_t)operand;
   else if(op<5)native_ram[pcs[op]+1]=(uint8_t)operand;
   else if(op<7)native_ram[pcs[op]+2]=(uint8_t)operand;
   else if(op==7)native_ram[56]=(uint8_t)((operand&1)?5:4);
   native_ram[0x4000+operand]=(uint8_t)(operand^0x5a);native_ram[0x4001+operand]=0x9b;
   native_ram[(uint16_t)(0x4100+(int8_t)operand)]=(uint8_t)operand;
   native_ram[(uint16_t)(0x4200+(int8_t)operand)]=(uint8_t)(operand^0xa5);
   sync_reference();step(costs[op]);CHECK(!z80_recomp_fallback_steps());
  }
  /* Unknown opcode and a partly uploaded multi-byte instruction fall back
   * once, then return to native code as soon as its live guard matches. */
  setup(0,0);g_z80.pc=56;native_ram[56]=0;sync_reference();step(4);CHECK(z80_recomp_fallback_steps()==1);
  g_z80.pc=58;native_ram[58]=1;native_ram[59]=0x99;native_ram[60]=0x12;sync_reference();step(10);CHECK(g_z80.c==0x99&&z80_recomp_fallback_steps()==2);
  g_z80.pc=58;native_ram[59]=0x34;sync_reference();step(10);CHECK(g_z80.c==0x34&&z80_recomp_fallback_steps()==2);
 }else if(!strcmp(mode,"reset")){
  setup(0xff,123);g_z80.pc=0x4321;g_z80.halted=true;g_z80.im=2;g_z80.ei_block=1;irq();
  Z80State saved=g_z80;z80_recomp_reset();
  CHECK(!g_z80.pc&&!g_z80.halted&&!g_z80.iff1&&!g_z80.iff2&&!g_z80.im&&!g_z80.ei_block&&!z80_recomp_irq_pending());
  CHECK(g_z80.r==saved.r&&g_z80.sp==saved.sp&&g_z80.a==saved.a&&g_z80.cyc==saved.cyc);
 }else CHECK(0);
 printf("adapter %s: real generated/fallback/interpreter boundary states pass\n",mode);return 0;
}
