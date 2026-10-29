/* Audit the closed grammar and compile ordinary/owned bodies from the SAME
 * emitter. Representative mnemonic/width/EA/extension classes execute later. */
#include "code_generator.h"
#include "m68k_validator.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {M68KInstr ins;unsigned bytes;} Sample;
static unsigned char seen[262144];static Sample samples[2048];
static char *emission(const GenesisRom*rom,const M68KInstr*ins,int owned)
{
 FILE*f=tmpfile();CHECK(f);int ok=owned?codegen_emit_owned_rmw_operation(f,rom,ins):codegen_emit_step_operation(f,rom,ins);
 if(!ok)fprintf(stderr,"emission failed opcode%04X mnemonic%u EA%02X owned%d\n",ins->words[0],(unsigned)ins->mnemonic,(unsigned)ins->src_ea,owned);CHECK(ok);
 CHECK(!fflush(f));long n=ftell(f);CHECK(n>0&&n<65536);char*s=malloc((size_t)n+1);CHECK(s);rewind(f);CHECK(fread(s,1,(size_t)n,f)==(size_t)n);s[n]=0;CHECK(!fclose(f));return s;
}
int main(int argc,char**argv)
{
 CHECK(argc==3);unsigned admitted=0,count=0;const unsigned eas[]={0x10,0x17,0x18,0x1f,0x20,0x27,0x28,0x30,0x38,0x39};
 FILE*out=fopen(argv[1],"wb");CHECK(out);
 fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"m68k_step_bind.h\"\n",out);
 for(unsigned opcode=0;opcode<65536;++opcode)for(unsigned pattern=0;pattern<2;++pattern){
  uint8_t bytes[24];memset(bytes,pattern?0xff:0,sizeof bytes);bytes[0]=(uint8_t)(opcode>>8);bytes[1]=(uint8_t)opcode;
  GenesisRom rom={0};rom.rom_data=bytes;rom.rom_size=sizeof bytes;M68KInstr ins;
  if(!m68k_decode_68000(&rom,0,&ins)||m68k_validate(&ins,NULL)!=M68K_LEGAL||!codegen_owned_rmw_instruction(&ins))continue;
  CHECK(codegen_step_nominal_cycles(&ins));
  char*ordinary=emission(&rom,&ins,0),*owned=emission(&rom,&ins,1);++admitted;
  CHECK(strstr(owned,"m68k_step_owned_rmw(ctx,")&&strstr(owned,"m68k_step_owned_read")&&strstr(owned,"m68k_step_owned_write"));
  const char*fallback=strstr(owned,"    }else{\n");CHECK(fallback&&!strncmp(fallback+11,ordinary,strlen(ordinary)));
  unsigned ea=10;for(unsigned e=0;e<10;++e)if((unsigned)ins.src_ea==eas[e])ea=e;
  if(ea<10){unsigned size=ins.size==M68K_SIZE_B?0:ins.size==M68K_SIZE_W?1:2;
   unsigned key=(unsigned)ins.mnemonic*1024+size*256+ea*8+pattern;CHECK(key<sizeof seen);
   if(!seen[key]){seen[key]=1;CHECK(count<sizeof samples/sizeof*samples);samples[count].ins=ins;
    samples[count].bytes=ins.mnemonic==MN_BCHG||ins.mnemonic==MN_BCLR||ins.mnemonic==MN_BSET||ins.mnemonic==MN_TAS||ins.mnemonic==MN_NBCD?1u:
       ins.mem_shift?2u:ins.size==M68K_SIZE_B?1u:ins.size==M68K_SIZE_W?2u:4u;++count;}
  }
  free(ordinary);free(owned);
 }
 for(unsigned n=0;n<count;++n){
  GenesisRom rom={0};fprintf(out,"static void a%u(M68kStepContext*ctx){\n",n);CHECK(codegen_emit_step_operation(out,&rom,&samples[n].ins));
  fprintf(out,"}\nstatic void b%u(M68kStepContext*ctx){\n",n);CHECK(codegen_emit_owned_rmw_operation(out,&rom,&samples[n].ins));fputs("}\n",out);
 }
 fputs("typedef struct {M68kStepOperation op[2];uint16_t words[11];unsigned count,cycles,ea,bytes;} Case;\nstatic const Case cases[]={\n",out);
 for(unsigned n=0;n<count;++n){M68KInstr*ins=&samples[n].ins;fprintf(out,"{{a%u,b%u},{",n,n);
  for(unsigned w=0;w<(unsigned)ins->word_count;++w)fprintf(out,"%s0x%04x",w?",":"",ins->words[w]);
  fprintf(out,"},%u,%u,%u,%u},\n",ins->word_count,codegen_step_nominal_cycles(ins),(unsigned)ins->src_ea,samples[n].bytes);
 }
 fputs("};\n",out);FILE*harness=fopen(argv[2],"rb");CHECK(harness);int ch;while((ch=fgetc(harness))!=EOF)fputc(ch,out);CHECK(!ferror(harness)&&!fclose(harness));CHECK(!ferror(out)&&!fclose(out));
 CHECK(admitted&&count);printf("%u admitted opcode/extensions audited, %u compiled owned-RMW classes\n",admitted,count);return 0;
}
