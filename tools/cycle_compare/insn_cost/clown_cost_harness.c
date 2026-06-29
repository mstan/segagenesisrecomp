#include "clown68000.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
void (*g_hybrid_pre_insn_fn)(cc_u32f pc) = 0;
cc_u32f g_hybrid_cycle_counter = 0;
static uint8_t ROM[0x10000];
static cc_u16f rd(const void*u,cc_u32f wa,cc_bool h,cc_bool l,cc_u32f c){
  (void)u;(void)h;(void)l;(void)c; uint32_t b=wa*2; if(b+1<sizeof(ROM)) return (ROM[b]<<8)|ROM[b+1]; return 0;}
static void wr(const void*u,cc_u32f wa,cc_bool h,cc_bool l,cc_u32f c,cc_u16f v){(void)u;(void)wa;(void)h;(void)l;(void)c;(void)v;}
static Clown68000_ReadWriteCallbacks cb={rd,wr,0};
static void pw(uint32_t a,uint16_t w){ROM[a]=w>>8;ROM[a+1]=w&0xFF;}
static int measure(uint32_t addr,uint32_t d0,uint32_t d1,uint32_t d2){
  Clown68000_State st; memset(&st,0,sizeof st);
  st.program_counter=addr; st.status_register=0x2700;
  for(int i=0;i<8;i++){st.address_registers[i]=0x00FFFE00u;}
  st.supervisor_stack_pointer=0x00FFFE00u; st.user_stack_pointer=0x00FFFE00u;
  st.data_registers[0]=d0; st.data_registers[1]=d1; st.data_registers[2]=d2;
  st.leftover_cycles=0;
  Clown68000_DoCycles(&st,&cb,0);
  return (int)st.leftover_cycles;
}
int main(void){
  uint32_t a;
  a=0x400; pw(a,0x2001); printf("MOVE.L D1,D0   = %d (PRM 4)\n",measure(a,0,0x5555,0));
  a=0x410; pw(a,0xD041); printf("ADD.W  D1,D0   = %d (PRM 4)\n",measure(a,0,0x5555,0));
  a=0x420; pw(a,0x6000);pw(a+2,0x0010); printf("BRA.W          = %d (PRM 10)\n",measure(a,0,0,0));
  a=0x430; pw(a,0x4EB9);pw(a+2,0);pw(a+4,0x0500); printf("JSR (xxx).L    = %d (PRM 20)\n",measure(a,0,0,0));
  a=0x440; pw(a,0x4E90); printf("JSR (A0)       = %d (PRM 16)\n",measure(a,0,0,0));
  a=0x450; pw(a,0x41F9);pw(a+2,0);pw(a+4,0x0500); printf("LEA (xxx).L,A0 = %d (PRM 12)\n",measure(a,0,0,0));
  a=0x460; pw(a,0x4E75); printf("RTS            = %d (PRM 16)\n",measure(a,0,0,0));
  a=0x470; pw(a,0x4E71); printf("NOP            = %d (PRM 4)\n",measure(a,0,0,0));
  a=0x480; pw(a,0xC0C1); printf("MULU D1,D0 src=0x5555 = %d ; src=0=%d ; src=0xFFFF=%d\n",
     measure(a,0,0x5555,0),measure(a,0,0,0),measure(a,0,0xFFFF,0));
  a=0x490; pw(a,0xC1C1); printf("MULS D1,D0 src=0x5555 = %d ; src=0=%d ; src=0xFFFF=%d ; src=0x0001=%d\n",
     measure(a,0,0x5555,0),measure(a,0,0,0),measure(a,0,0xFFFF,0),measure(a,0,1,0));
  a=0x4A0; pw(a,0x80C1); printf("DIVU D1,D0 dest=0x5555,src=0x5555 = %d ; dest=0x10000,src=2 = %d\n",
     measure(a,0x5555,0x5555,0),measure(a,0x10000,2,0));
  a=0x4B0; pw(a,0x81C1); printf("DIVS D1,D0 dest=0x5555,src=0x5555 = %d ; dest=0xFFFF0000,src=2=%d\n",
     measure(a,0x5555,0x5555,0),measure(a,0xFFFF0000,2,0));
  a=0x4C0; pw(a,0xE560); printf("ASL.W D2,D0 cnt(D2)=0x5555(&63=21)=%d ; cnt=4=%d ; cnt=1=%d\n",
     measure(a,0,0,0x5555),measure(a,0,0,4),measure(a,0,0,1));
  a=0x4D0; pw(a,0xE740); printf("ASL.W #3,D0 = %d (PRM 6+2*3=12)\n",measure(a,0,0,0));
  return 0;
}
