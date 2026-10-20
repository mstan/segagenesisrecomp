/* Original-program instruction-sized AOT frontend. Produces portable native C,
 * never embeds the input image. Input/module metadata are caller-owned. */
#include "code_generator.h"
#include "m68k_validator.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SHARDS=32 };
typedef struct { uint8_t *bytes;uint32_t base,size; } Image;
typedef struct { M68KInstr ins;unsigned cycles;int illegal,emitted; } Operation;
static void fail(const char *message){fprintf(stderr,"M68kStepRecomp: %s\n",message);exit(1);}
static uint32_t number(const char *s)
{
    char *end=NULL;errno=0;unsigned long n=strtoul(s,&end,0);
    if(errno || !*s || *end || n>0xFFFFFFu)fail("invalid 24-bit address");
    return (uint32_t)n;
}
static int in_image(const Image *i,uint32_t pc);
static void queue_pc(const Image *i,uint32_t pc,uint8_t *queued,uint32_t *queue,size_t *tail);
static void entries_file(const Image *image,const char *path,uint8_t *queued,
                         uint32_t *queue,size_t *tail)
{
    enum { MAX_ENTRY_TOKEN=32 };
    FILE *f=fopen(path,"rb");if(!f)fail("cannot open entries file");
    size_t count=0;char token[MAX_ENTRY_TOKEN+1];size_t used=0;int c;
    while((c=fgetc(f))!=EOF) {
        if(isspace((unsigned char)c)) {
            if(!used)continue;
            token[used]='\0';uint32_t pc=number(token);
            if(!in_image(image,pc))fail("entry outside image or odd");
            queue_pc(image,pc,queued,queue,tail);++count;used=0;
        } else {
            if(c==0)fail("NUL byte in entries file token");
            if(used==MAX_ENTRY_TOKEN)fail("entries file token too long");
            token[used++]=(char)c;
        }
    }
    if(ferror(f))fail("cannot read entries file");
    if(used) {
        token[used]='\0';uint32_t pc=number(token);
        if(!in_image(image,pc))fail("entry outside image or odd");
        queue_pc(image,pc,queued,queue,tail);++count;
    }
    if(fclose(f))fail("cannot close entries file");
    if(!count)fail("entries file must contain at least one address");
}
static uint8_t image_read(uint32_t a,void *user)
{
    Image *i=user;return a>=i->base && a-i->base<i->size?i->bytes[a-i->base]:0xFF;
}
static uint32_t crc32(const Image *i)
{
    uint32_t c=0xFFFFFFFFu;
    for(uint32_t n=0;n<i->size;++n){c^=i->bytes[n];for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}
    return ~c;
}
static int in_image(const Image *i,uint32_t pc)
{return !(pc&1u) && pc>=i->base && pc-i->base+1u<i->size;}
static void queue_pc(const Image *i,uint32_t pc,uint8_t *queued,uint32_t *queue,size_t *tail)
{
    if(!in_image(i,pc))return;
    uint32_t offset=(pc-i->base)/2;
    if(!queued[offset]){queued[offset]=1;queue[(*tail)++]=pc;}
}
static FILE *output(const char *dir,const char *prefix,const char *suffix)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s%s",dir,prefix,suffix);
    if(n<0 || (size_t)n>=sizeof path)fail("output path too long");
    FILE *f=fopen(path,"wb");if(!f)fail("cannot create output (directory must exist)");return f;
}
static void close_output(FILE *f){if(ferror(f) || fclose(f))fail("output write failed");}

int main(int argc,char **argv)
{
    if(argc<6){fprintf(stderr,"usage: M68kStepRecomp INPUT OUTPUT_DIR C_IDENTIFIER LOAD_BASE ENTRY... [--entries-file PATH] | --all\n");return 2;}
    const char *prefix=argv[3];
    if(!(isalpha((unsigned char)*prefix) || *prefix=='_'))fail("invalid C identifier");
    for(const char *p=prefix;*p;++p)if(!(isalnum((unsigned char)*p) || *p=='_'))fail("invalid C identifier");
    if(strlen(prefix)>96)fail("C identifier too long");
    Image image={0};image.base=number(argv[4]);
    if(image.base&1u)fail("load base must be word aligned");
    FILE *input=fopen(argv[1],"rb");if(!input)fail("cannot open input");
    if(fseek(input,0,SEEK_END))fail("cannot size input");long length=ftell(input);
    if(length<=0 || (unsigned long)length>0x1000000u-image.base || (length&1))fail("input must be a nonempty word-aligned 24-bit image");
    rewind(input);image.size=(uint32_t)length;image.bytes=malloc(image.size);
    if(!image.bytes || fread(image.bytes,1,image.size,input)!=image.size || fclose(input))fail("cannot read input");
    size_t slots=image.size/2u;
    uint32_t *queue=malloc(slots*sizeof *queue);
    uint8_t *queued=calloc(slots,1);
    Operation *ops=calloc(slots,sizeof *ops);
    if(!queue || !queued || !ops)fail("out of memory");
    size_t head=0,tail=0;int all=0;
    for(int n=5;n<argc;++n) {
        if(!strcmp(argv[n],"--all"))all=1;
        else if(!strcmp(argv[n],"--entries-file")) {
            if(++n>=argc)fail("--entries-file requires a path");
            entries_file(&image,argv[n],queued,queue,&tail);
        } else {uint32_t pc=number(argv[n]);if(!in_image(&image,pc))fail("entry outside image or odd");queue_pc(&image,pc,queued,queue,&tail);}
    }
    if(all)for(uint32_t n=0;n<image.size;n+=2)queue_pc(&image,image.base+n,queued,queue,&tail);
    GenesisRom rom={0};rom.rom_size=image.base+image.size;rom.read8_override=image_read;rom.read8_user=&image;
    while(head<tail) {
        uint32_t pc=queue[head++];Operation *o=&ops[(pc-image.base)/2];
        if(!m68k_decode_68000(&rom,pc,&o->ins))continue;
        M68KValidity validity=m68k_validate(&o->ins,NULL);
        o->illegal=validity!=M68K_LEGAL || o->ins.mnemonic==MN_ILLEGAL;
        if(o->illegal){o->ins.mnemonic=MN_ILLEGAL;o->ins.word_count=1;o->ins.byte_length=2;}
        if(!o->ins.word_count || o->ins.word_count>M68K_MAX_WORDS ||
           o->ins.byte_length>image.size-(pc-image.base)){o->ins.word_count=0;continue;}
        o->cycles=codegen_step_nominal_cycles(&o->ins);
        if(all || o->illegal)continue;
        if(o->ins.has_target)queue_pc(&image,o->ins.target_addr,queued,queue,&tail);
        if(!m68k_is_terminator(&o->ins))queue_pc(&image,pc+o->ins.byte_length,queued,queue,&tail);
    }
    FILE *parts[SHARDS];
    for(unsigned n=0;n<SHARDS;++n){char suffix[32];snprintf(suffix,sizeof suffix,"_part%02u.c",n);parts[n]=output(argv[2],prefix,suffix);fputs("/* Generated native MC68000 operations; no input image embedded. */\n#include \"m68k_step_bind.h\"\n",parts[n]);}
    FILE *module=output(argv[2],prefix,"_module.c");
    fprintf(module,"/* Generated module; always validate current instruction bytes. */\n#include \"%s.h\"\n",prefix);
    FILE *report=output(argv[2],prefix,"_coverage.csv");
    fputs("pc,opcode,words,nominal_cycles,kind\n",report);
    size_t emitted=0,illegal=0,unsupported=0;
    for(size_t n=0;n<slots;++n) {
        Operation *o=&ops[n];if(!o->ins.word_count)continue;
        if(o->illegal){o->emitted=1;++illegal;}
        else {
            FILE *probe=tmpfile();if(!probe)fail("cannot create emission buffer");
            if(codegen_emit_step_operation(probe,&rom,&o->ins)) {
                rewind(probe);FILE *part=parts[emitted%SHARDS];
                fprintf(part,"void %s_at_%06X(M68kStepContext *ctx){\n",prefix,o->ins.addr);
                char buffer[4096];size_t got;
                while((got=fread(buffer,1,sizeof buffer,probe))!=0)if(fwrite(buffer,1,got,part)!=got)fail("output write failed");
                fputs("}\n",part);o->emitted=1;
                fprintf(module,"extern void %s_at_%06X(M68kStepContext *);\n",prefix,o->ins.addr);
            } else ++unsupported;
            close_output(probe);
        }
        if(o->emitted)++emitted;
        fprintf(report,"%06X,%04X,%u,%u,%s\n",o->ins.addr,o->ins.words[0],(unsigned)o->ins.word_count,o->cycles,
                o->illegal?"illegal":o->emitted?"native":"unsupported");
    }
    if(!emitted)fail("no compiled operations");
    fputs("static const M68kStepEntry entries[]={\n",module);
    for(size_t n=0;n<slots;++n) {
        Operation *o=&ops[n];if(!o->emitted)continue;
        fprintf(module," {0x%06Xu,{",o->ins.addr);
        for(int w=0;w<o->ins.word_count;++w)fprintf(module,"%s0x%04X",w?",":"",o->ins.words[w]);
        fprintf(module,"},%u,%u,",(unsigned)o->ins.word_count,o->cycles);
        if(o->illegal)fputs("m68k_step_illegal",module);
        else fprintf(module,"%s_at_%06X",prefix,o->ins.addr);
        fputs("},\n",module);
    }
    fprintf(module,"};\nconst M68kStepModule %s_module={\"%s\",0x%06Xu,%uu,0x%08Xu,entries,sizeof entries/sizeof *entries};\n",
            prefix,prefix,image.base,image.size,crc32(&image));
    FILE *header=output(argv[2],prefix,".h");
    fprintf(header,"#pragma once\n#include \"m68k_step.h\"\nextern const M68kStepModule %s_module;\n",prefix);
    close_output(header);close_output(module);close_output(report);
    for(unsigned n=0;n<SHARDS;++n)close_output(parts[n]);
    printf("%s: image=%u base=%06X crc32=%08X native=%zu illegal=%zu unsupported=%zu examined=%zu\n",prefix,image.size,image.base,crc32(&image),emitted-illegal,illegal,unsupported,tail);
    free(ops);free(queued);free(queue);free(image.bytes);
    return unsupported?3:0;
}
