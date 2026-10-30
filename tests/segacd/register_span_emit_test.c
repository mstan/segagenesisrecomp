/* Compile shared-emitter spans and their ordinary instruction streams, then
 * compare architectural state for edge/random inputs. No title/ROM fixture. */
#include "code_generator.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static const uint16_t operations[][4]={
 {0x0640,1}, {0x0600,0xff}, {0x0680,0x8000,1}, /* ADDI B/W/L */
 {0x0441,0x8000}, {0x0401,1}, {0x0481,0xffff,0xffff},
 {0x70ff}, {0x7200}, {0x7401}, {0x3001}, {0x1200}, {0x2401},
 {0x5240}, {0x5381}, {0x5888}, {0x5189}, {0xd041}, {0x9001},
 {0xd1c1}, {0x91c0}, {0x0240,0x8000}, {0x0001,0xff}, {0x0a82,0xffff,0xffff},
 {0xb041}, {0x0c40,0x8000}, {0xb1c1}, {0x4a40}, {0x4241},
 {0x4440}, {0x4641}, {0x4840}, {0x4881}, {0x48c2},
 {0xd141}, {0x9141}, {0x4040}, {0xc141}, {0x8141}, {0x4800},
 {0x41e8,0x7fff}, {0x43f0,0x1804}, {0x45fa,0xfff0}, {0xc141}, {0xc149}, {0xc189}
};
int main(int argc,char **argv)
{
 CHECK(argc==2);FILE*out=fopen(argv[1],"wb");CHECK(out);
 fputs("#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"m68k_step.h\"\n#define g_cpu ctx->state.regs\n#define m68k_brief_index_value m68k_step_brief_index_value\n"
       "#define CHECK(x) do{if(!(x)){fprintf(stderr,\"line %u: %s\\n\",(unsigned)__LINE__,#x);exit(1);}}while(0)\n",out);
 unsigned total=sizeof operations/sizeof*operations;
 /* Exhaustively enumerate legal opcode words with varied extension patterns.
  * Every admitted operation must stay inside the closed register ABI. */
 unsigned admitted=0;
 for(unsigned opcode=0;opcode<65536;++opcode)for(unsigned pattern=0;pattern<3;++pattern){
   uint8_t bytes[16];for(unsigned b=0;b<sizeof bytes;++b)bytes[b]=(uint8_t)(pattern==0?0:pattern==1?0xff:(b*37));
   bytes[0]=(uint8_t)(opcode>>8);bytes[1]=(uint8_t)opcode;
   GenesisRom rom={0};rom.rom_data=bytes;rom.rom_size=sizeof bytes;M68KInstr ins;
   if(!m68k_decode_68000(&rom,0,&ins)||!codegen_register_instruction(&ins,NULL,NULL))continue;
   FILE*probe=tmpfile();CHECK(probe);CHECK(codegen_emit_step_operation(probe,&rom,&ins));
   CHECK(!fflush(probe));long size=ftell(probe);CHECK(size>=0&&size<32768);char code[32768];rewind(probe);
   CHECK(fread(code,1,(size_t)size,probe)==(size_t)size);code[size]=0;CHECK(!fclose(probe));
   CHECK(!strstr(code,"m68k_read")&&!strstr(code,"m68k_write")&&!strstr(code,"m68k_step_")&&
         !strstr(code,"ctx->")&&!strstr(code,"g_cpu.PC")&&!strstr(code,"return")&&!strstr(code,"glue_")&&!strstr(code,"_sp_popped"));
   ++admitted;
 }
 /* Every pair covers overwrite/use combinations. Longer windows exercise
  * chaining and LEA/index composition; all initial CCR combinations included. */
 unsigned sequences=total*total+total;
 for(unsigned s=0;s<sequences;++s){
   M68KInstr ins[CODEGEN_REGISTER_SPAN_MAX];uint8_t image[128]={0};unsigned count=s<total*total?2:8,pc=0;
   for(unsigned n=0;n<count;++n){
     unsigned op=count==2?(n?s%total:s/total):(s-total*total+n*7)%total;
     for(unsigned w=0;w<4;++w){image[pc+w*2]=(uint8_t)(operations[op][w]>>8);image[pc+w*2+1]=(uint8_t)operations[op][w];}
     GenesisRom rom={0};rom.rom_data=image;rom.rom_size=sizeof image;
     CHECK(m68k_decode_68000(&rom,pc,&ins[n]));CHECK(codegen_register_instruction(&ins[n],NULL,NULL));pc+=ins[n].byte_length;
   }
   GenesisRom rom={0};rom.rom_data=image;rom.rom_size=sizeof image;
   fprintf(out,"static void a%u(M68kStepContext*ctx){\n",s);
   for(unsigned n=0;n<count;++n){fputs(" {\n",out);CHECK(codegen_emit_step_semantics(out,&rom,&ins[n]));fputs(" }\n",out);}
   fputs("}\n",out);fprintf(out,"static void b%u(M68kStepContext*ctx){\n",s);
   CHECK(codegen_emit_register_span(out,&rom,ins,count));fputs("}\n",out);
 }
 /* The old signed CMPA subtraction is undefined on overflow. GCC exposed
  * N being optimized away after the preceding SUBI; assert real 68000 flags,
  * independently of the span/ordinary differential oracle. */
 {
  uint8_t bytes[]={0x04,0x81,0xff,0xff,0xff,0xff,0xb1,0xc1};
  GenesisRom rom={0};rom.rom_data=bytes;rom.rom_size=sizeof bytes;M68KInstr ins;
  fputs("static void cmpa_overflow(M68kStepContext*ctx){\n",out);
  CHECK(m68k_decode_68000(&rom,0,&ins));CHECK(codegen_emit_step_semantics(out,&rom,&ins));
  CHECK(m68k_decode_68000(&rom,6,&ins));CHECK(codegen_emit_step_semantics(out,&rom,&ins));fputs("}\n",out);
 }
 fputs("typedef void(*Op)(M68kStepContext*);static const Op before[]={",out);
 for(unsigned s=0;s<sequences;++s)fprintf(out,"%sa%u",s?",":"",s);fputs("};\nstatic const Op after[]={",out);
 for(unsigned s=0;s<sequences;++s)fprintf(out,"%sb%u",s?",":"",s);
 fputs("};\nstatic uint32_t seed=0x12345678;static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}\n"
 "int main(void){M68kStepContext overflow={0};overflow.state.regs.A[0]=0xffff;overflow.state.regs.D[1]=0x7fffffff;overflow.state.regs.SR=0x2010;cmpa_overflow(&overflow);CHECK(overflow.state.regs.D[1]==0x80000000u&&overflow.state.regs.SR==0x201b);\n"
 "uint32_t edge[]={0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff};unsigned long cases=0;\n"
 "for(unsigned s=0;s<sizeof before/sizeof*before;++s)for(unsigned sample=0;sample<96;++sample){M68kStepContext a={0},b;"
 "for(unsigned r=0;r<8;++r){a.state.regs.D[r]=sample<32?edge[(sample+r)%11]:random32();a.state.regs.A[r]=sample<32?edge[(sample+r*3)%11]:random32();}"
 "a.state.regs.SR=(uint16_t)(0x2700|(sample&31));a.state.regs.PC=0x123456;a.state.cycles=71;a.state.instructions=23;a.charge=99;b=a;before[s](&a);after[s](&b);"
 "if(memcmp(&a,&b,sizeof a)){fprintf(stderr,\"sequence %u sample %u SR %04X/%04X\\n\",s,sample,a.state.regs.SR,b.state.regs.SR);return 1;}++cases;}"
 "printf(\"%lu compiled sequence comparisons pass\\n\",cases);return 0;}\n",out);
 CHECK(!fclose(out));printf("%u admitted opcode/extensions audited; %u compiled sequences\n",admitted,sequences);return 0;
}
