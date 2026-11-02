/* Real common-emitter bodies, compiled and compared with ordinary retirement. */
#include "code_generator.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static unsigned sequence;
static void emit(FILE *out,unsigned prefix,const uint16_t *words,unsigned length)
{
    uint8_t image[128]={0};unsigned pc=0x100,count=0;
    M68KInstr ins[CODEGEN_REGISTER_SPAN_MAX];
    /* LEA preserves every initial CCR. Other prefixes create real CCR/data
     * dependencies consumed by Bcc, DBcc and indexed JMP. */
    static const uint16_t prefixes[][14]={
        {0x41e8,2}, {0x3001,0x4a83},
        {0x5280,0x5289,0x3401,0x4842,0x4682,0x41e8,2,0x4a83}
    };
    unsigned prefix_words=prefix==0?2:prefix==1?2:8;
    for(unsigned n=0;n<prefix_words;++n){image[n*2]=(uint8_t)(prefixes[prefix][n]>>8);image[n*2+1]=(uint8_t)prefixes[prefix][n];}
    for(unsigned n=0;n<length;++n){image[(prefix_words+n)*2]=(uint8_t)(words[n]>>8);image[(prefix_words+n)*2+1]=(uint8_t)words[n];}
    GenesisRom rom={0};rom.rom_data=image;rom.rom_size=sizeof image;
    unsigned offset=0,cycles=0,maximum=0;
    while(offset<(prefix_words+length)*2){
        CHECK(count<CODEGEN_REGISTER_SPAN_MAX);
        CHECK(m68k_decode_68000(&rom,offset,&ins[count]));
        ins[count].addr=pc; /* target/displacement decoding below needs base100 */
        pc+=ins[count].byte_length;offset+=ins[count].byte_length;++count;
    }
    /* Decode again with the actual address view, not a patched target. */
    uint8_t based[512]={0};memcpy(based+0x100,image,sizeof image);
    rom.rom_data=based;rom.rom_size=sizeof based;pc=0x100;
    for(unsigned n=0;n<count;++n){CHECK(m68k_decode_68000(&rom,pc,&ins[n]));pc+=ins[n].byte_length;}
    CHECK(codegen_flow_terminal(&ins[count-1],&maximum));
    for(unsigned n=0;n<count;++n){
        if(n+1<count){CHECK(codegen_register_instruction(&ins[n],NULL,NULL));cycles+=codegen_step_nominal_cycles(&ins[n]);}
        fprintf(out,"static void op_%u_%u(M68kStepContext*ctx){\n",sequence,n);
        CHECK(codegen_emit_step_operation(out,&rom,&ins[n]));fputs("}\n",out);
    }
    fprintf(out,"static void flow_%u(M68kStepContext*ctx){\n",sequence);
    CHECK(codegen_emit_flow_span(out,&rom,ins,count));fputs("}\n",out);
    fprintf(out,"static const M68kStepEntry entries_%u[]={\n",sequence);
    for(unsigned n=0;n<count;++n){
        fprintf(out," {0x%x,{",ins[n].addr);
        for(unsigned w=0;w<ins[n].word_count;++w)fprintf(out,"%s0x%x",w?",":"",ins[n].words[w]);
        fprintf(out,"},%u,%u,op_%u_%u},\n",ins[n].word_count,codegen_step_nominal_cycles(&ins[n]),sequence,n);
    }
    fputs("};\n",out);fprintf(out,"static const M68kStepEntry*const members_%u[]={",sequence);
    for(unsigned n=0;n<count;++n)fprintf(out,"%sentries_%u+%u",n?",":"",sequence,n);
    fprintf(out,"};\nstatic const Case case_%u={ {members_%u,flow_%u,%u,%u},%u,0x%x,%u,%d,%u,%u};\n",
        sequence,sequence,sequence,count,cycles+maximum,prefix,ins[count-1].target_addr,
        (unsigned)ins[count-1].mnemonic,ins[count-1].src_ea,(unsigned)ins[count-1].reg,maximum);
    ++sequence;
}
int main(int argc,char **argv)
{
    CHECK(argc==3);FILE*out=fopen(argv[1],"wb");CHECK(out);
    fputs("#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include \"m68k_step.h\"\n#define g_cpu ctx->state.regs\n#define m68k_brief_index_value m68k_step_brief_index_value\n"
          "#define CHECK(x) do{if(!(x)){fprintf(stderr,\"line %u: %s\\n\",(unsigned)__LINE__,#x);exit(1);}}while(0)\n"
          "typedef struct {M68kStepFlowSpan span;unsigned prefix,target,kind;int ea;unsigned reg,maximum;} Case;\n",out);
    for(unsigned prefix=0;prefix<3;++prefix){
        for(unsigned cc=0;cc<16;++cc)if(cc!=1)for(unsigned word=0;word<2;++word){
            uint16_t words[2]={(uint16_t)(0x6000|(cc<<8)|(word?0:0xe0)),0xffd0};
            emit(out,prefix,words,word?2:1);
        }
        for(unsigned cc=0;cc<16;++cc)for(unsigned reg=0;reg<8;++reg){
            uint16_t words[2]={(uint16_t)(0x50c8|(cc<<8)|reg),0xffd0};emit(out,prefix,words,2);
        }
        for(unsigned reg=0;reg<8;++reg){
            uint16_t words[2]={(uint16_t)(0x4ed0|reg),0};emit(out,prefix,words,1);
            words[0]=(uint16_t)(0x4ee8|reg);words[1]=0xfff0;emit(out,prefix,words,2);
            words[0]=(uint16_t)(0x4ef0|reg);words[1]=0x1004;emit(out,prefix,words,2);
            words[1]=0x98fc;emit(out,prefix,words,2);
        }
        const uint16_t absolute[][3]={{0x4ef8,0x8001},{0x4ef9,0xffab,0xcdef},{0x4efa,0xfff0},{0x4efb,0x1804}};
        for(unsigned n=0;n<4;++n)emit(out,prefix,absolute[n],n==1?3:2);
    }
    /* These stack/privilege/fault instructions are deliberately not pure flow. */
    const uint16_t refused[]={0x6102,0x4e90,0x4e75,0x4e73,0x4e77,0x4e72,0x4e40,0x4afc};
    for(unsigned n=0;n<sizeof refused/sizeof*refused;++n){
        uint8_t bytes[8]={(uint8_t)(refused[n]>>8),(uint8_t)refused[n]};
        GenesisRom rom={0};rom.rom_data=bytes;rom.rom_size=sizeof bytes;M68KInstr ins;
        CHECK(m68k_decode_68000(&rom,0,&ins));CHECK(!codegen_flow_terminal(&ins,NULL));
    }
    fputs("static const Case*const cases[]={",out);
    for(unsigned n=0;n<sequence;++n)fprintf(out,"%s&case_%u",n?",":"",n);fputs("};\n",out);
    fprintf(out,"enum{BRA=%u,BCC=%u,DBCC=%u,JMP=%u};\n",MN_BRA,MN_Bcc,MN_DBcc,MN_JMP);
    FILE*harness=fopen(argv[2],"rb");CHECK(harness);int ch;while((ch=fgetc(harness))!=EOF)fputc(ch,out);
    CHECK(!fclose(harness));CHECK(!fclose(out));printf("%u compiled pure-control regions\n",sequence);return 0;
}
