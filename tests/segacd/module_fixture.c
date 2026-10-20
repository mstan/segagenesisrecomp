#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    static const unsigned short words[]={0x7003,0x6108,0x5340,0x66FA,
        0x4E72,0x2300,0x5241,0x4E75,0x60FF,0x4E7A,0x4E71,0x4E71,
        0x4E80,0x4E71,0x4E71,0x203C};
    if(argc!=3)return 1;
    for(unsigned n=0;n<2;++n) {
        FILE *f=fopen(argv[n+1],"wb");if(!f)return 1;
        for(unsigned i=0;i<sizeof words/sizeof *words;++i) {
            unsigned w=words[i];if(n && i==0)w=0x7005;if(n && i==5)w=0x2400;
            if(fputc((int)(w>>8),f)==EOF || fputc((int)(w&255),f)==EOF)return 1;
        }
        if(fclose(f))return 1;
    }
    return 0;
}
