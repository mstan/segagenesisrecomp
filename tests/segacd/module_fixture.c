#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_words(const char *path,const unsigned short *words,unsigned count)
{
    FILE *f=fopen(path,"wb");if(!f)return 1;
    for(unsigned n=0;n<count;++n)if(fputc((int)(words[n]>>8),f)==EOF || fputc((int)(words[n]&255),f)==EOF)return 1;
    return fclose(f)!=0;
}
static int write_text(const char *path,const char *text)
{
    FILE *f=fopen(path,"wb");if(!f)return 1;
    size_t n=strlen(text);int result=fwrite(text,1,n,f)!=n;
    return fclose(f)!=0 || result;
}
int main(int argc,char **argv)
{
    static const unsigned short words[]={0x7003,0x6108,0x5340,0x66FA,
        0x4E72,0x2300,0x5241,0x4E75,0x60FF,0x4E7A,0x4E71,0x4E71,
        0x4E80,0x4E71,0x4E71,0x203C};
    if(argc!=3 && argc!=5 && argc!=6)return 1;
    for(unsigned n=0;n<2;++n) {
        FILE *f=fopen(argv[n+1],"wb");if(!f)return 1;
        for(unsigned i=0;i<sizeof words/sizeof *words;++i) {
            unsigned w=words[i];if(n && i==0)w=0x7005;if(n && i==5)w=0x2400;
            if(fputc((int)(w>>8),f)==EOF || fputc((int)(w&255),f)==EOF)return 1;
        }
        if(fclose(f))return 1;
    }
    if(argc>=5) {
        /* Main grants Word RAM, releases SUB, then really waits for its data. */
        static const unsigned short main_program[]={0x33FC,2,0x00A1,0x2002,
            0x13FC,1,0x00A1,0x2001,0x2039,0x0020,0,0x4E72,0x2000,0x60FE,
            0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E71,0x4E73};
        /* SUB stores data, returns Word RAM, STOPs with IRQ3 unmasked. */
        static const unsigned short sub_program[]={0x4E72,0x2000,0x4EF9,0,0x0100,0x4E71,0x4E71,0x4E71,
            0x23FC,0x1234,0x5678,8,0,
            0x13FC,1,0x00FF,0x8003,0x4E72,0x2000,0x60FE,
            0x4E71,0x4E71,0x4E71,0x4E71,0x4E73};
        if(write_words(argv[3],main_program,sizeof main_program/sizeof *main_program) ||
           write_words(argv[4],sub_program,sizeof sub_program/sizeof *sub_program))return 1;
    }
    if(argc==6) {
        /* Exact old callback for MOVEQ at 0x1000; generated delta CFG must
         * still discover and emit the following BSR/loop/STOP operations. */
        if(write_text(argv[5],"0x1000 1 4 0x7003\n"))return 1;
    }
    return 0;
}
