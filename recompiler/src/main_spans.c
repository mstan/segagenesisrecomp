/* Generic companion emitter for exact, already-qualified compact catalogs.
 * Input: ordered FIRST descriptor records, not a ROM/image or title profile.
 * Every instruction is decoded/validated again through the shared core. */
#include "code_generator.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct {M68KInstr ins;char array[128],symbol[128];unsigned index,cycles;int qualified;} Record;
static void fail(const char*s){fprintf(stderr,"M68kSpanRecomp: %s\n",s);exit(1);}
static uint8_t read_byte(uint32_t address,void*user)
{
 Record*r=user;unsigned offset=address-r->ins.addr;
 return offset<sizeof r->ins.words? (uint8_t)(r->ins.words[offset/2]>>(offset&1?0:8)):0;
}
static FILE *output(const char*dir,const char*name,const char*suffix)
{
 char path[4096];int n=snprintf(path,sizeof path,"%s/%s%s",dir,name,suffix);
 if(n<0 || n>=(int)sizeof path)fail("output path too long");
 FILE*f=fopen(path,"wb");if(!f)fail("cannot open output");return f;
}
static void close_file(FILE*f){if(ferror(f)||fclose(f))fail("output write failed");}
static void ordinary_body(FILE *proof,const Record *record,const GenesisRom *rom)
{
 FILE*buffer=tmpfile();if(!buffer)fail("cannot open ordinary body buffer");
 if(!codegen_emit_step_operation(buffer,rom,&record->ins)||fflush(buffer))fail("ordinary emission failed");
 long size=ftell(buffer);if(size<0||fseek(buffer,0,SEEK_SET))fail("ordinary body read failed");
 fprintf(proof,"[\"%s\",\"",record->symbol);
 for(long n=0;n<size;++n){int ch=fgetc(buffer);if(ch==EOF)fail("short ordinary body");
  if(ch=='\n')fputs("\\n",proof);else if(ch=='\r')fputs("\\r",proof);
  else if(ch=='\t')fputs("\\t",proof);else if(ch=='\\'||ch=='\"'){fputc('\\',proof);fputc(ch,proof);}
  else if(ch<32)fail("invalid ordinary body character");else fputc(ch,proof);
 }
 fputs("\"]\n",proof);close_file(buffer);
}
int main(int argc,char**argv)
{
 if(argc!=4)fail("usage: M68kSpanRecomp INPUT OUTPUT_DIR PREFIX");
 const char*prefix=argv[3];if(!isalpha((unsigned char)*prefix)&&*prefix!='_')fail("bad prefix");
 for(const char*p=prefix;*p;++p)if(!isalnum((unsigned char)*p)&&*p!='_')fail("bad prefix");
 if(strlen(prefix)>96)fail("prefix too long");
 FILE*input=fopen(argv[1],"rb");if(!input)fail("cannot open input");
 Record*rows=NULL;size_t count=0,capacity=0;unsigned previous=0;
 for(;;){
  Record record={0};unsigned pc,words,effect;
  int read=fscanf(input,"%127s %u %x %u %u %u",record.array,&record.index,&pc,&words,&record.cycles,&effect);
  if(read==EOF)break;if(read!=6||pc>0xffffffu||(pc&1u)||!words||words>M68K_MAX_WORDS||!record.cycles||record.cycles>65535u||(count&&pc<=previous))fail("bad descriptor");
  for(const char*p=record.array;*p;++p)if(!isalnum((unsigned char)*p)&&*p!='_')fail("bad array identifier");
  record.ins.addr=pc;
  for(unsigned w=0;w<words;++w){unsigned value;if(fscanf(input,"%x",&value)!=1||value>65535u)fail("bad instruction words");record.ins.words[w]=(uint16_t)value;}
  if(fscanf(input,"%127s",record.symbol)!=1)fail("missing original symbol");
  for(const char*p=record.symbol;*p;++p)if(!isalnum((unsigned char)*p)&&*p!='_')fail("bad operation identifier");
  GenesisRom rom={0};rom.rom_size=0x1000000u;rom.read8_override=read_byte;rom.read8_user=&record;
  M68KInstr decoded;
  record.qualified=effect==1u&&m68k_decode_68000(&rom,pc,&decoded)&&
    decoded.word_count==(int)words&&codegen_register_instruction(&decoded,NULL,NULL)&&
    codegen_step_nominal_cycles(&decoded)==record.cycles;
  if(record.qualified)record.ins=decoded;else record.ins.byte_length=words*2u;
  if(count==capacity){capacity=capacity?capacity*2:4096;Record*next=realloc(rows,capacity*sizeof*rows);if(!next)fail("out of memory");rows=next;}
  rows[count++]=record;previous=pc;
 }
 if(ferror(input)||fclose(input))fail("input read failed");
 FILE*parts[32];for(unsigned n=0;n<32;++n){char suffix[32];snprintf(suffix,sizeof suffix,"_part%02u.c",n);parts[n]=output(argv[2],prefix,suffix);fputs("#include \"m68k_step_bind.h\"\n",parts[n]);}
 FILE*module=output(argv[2],prefix,"_module.c");fputs("#include \"m68k_step.h\"\n",module);
 FILE*proof=output(argv[2],prefix,"_ordinary_bodies.jsonl");
 FILE*table=tmpfile();if(!table)fail("cannot open table buffer");
 unsigned spans=0,instructions=0;
 for(size_t s=0;s<count;){
  M68KInstr ins[CODEGEN_REGISTER_SPAN_MAX];unsigned length=0,cycles=0;uint32_t next=rows[s].ins.addr;
  while(length<CODEGEN_REGISTER_SPAN_MAX && s+length<count){Record*r=&rows[s+length];if(!r->qualified||r->ins.addr!=next)break;ins[length++]=r->ins;cycles+=r->cycles;next=r->ins.addr+r->ins.byte_length;}
  if(length<2){++s;continue;}
  FILE*part=parts[spans%32];
  for(unsigned n=0;n<length;++n)fprintf(part,"extern const M68kStepEntry %s[];\n",rows[s+n].array);
  fprintf(part,"const M68kStepEntry *const %s_members_%u[]={",prefix,spans);
  for(unsigned n=0;n<length;++n)fprintf(part,"%s&%s[%u]",n?",":"",rows[s+n].array,rows[s+n].index);
  fprintf(part,"};\nvoid %s_span_%u(M68kStepContext*ctx){\n",prefix,spans);
  GenesisRom rom={0}; /* Common semantics consume decoded extension words. */
  for(unsigned n=0;n<length;++n)ordinary_body(proof,&rows[s+n],&rom);
  if(!codegen_emit_register_span(part,&rom,ins,length))fail("span emission failed");fputs("}\n",part);
  fprintf(module,"extern const M68kStepEntry *const %s_members_%u[];\nextern void %s_span_%u(M68kStepContext*);\n",prefix,spans,prefix,spans);
  fprintf(table," {%s_members_%u,%s_span_%u,%u,%u},\n",prefix,spans,prefix,spans,length,cycles);
  ++spans;instructions+=length;s+=length;
 }
 if(spans){fprintf(module,"const M68kStepRegisterSpan %s_spans[]={\n",prefix);rewind(table);int ch;while((ch=fgetc(table))!=EOF)fputc(ch,module);fputs("};\n",module);}
 else fprintf(module,"const M68kStepRegisterSpan %s_spans[1]={{0}};\n",prefix);
 fprintf(module,"const size_t %s_count=%u;\n",prefix,spans);
 close_file(table);close_file(module);close_file(proof);for(unsigned n=0;n<32;++n)close_file(parts[n]);
 printf("%s: %zu descriptors, %u spans, %u instructions\n",prefix,count,spans,instructions);free(rows);return 0;
}
