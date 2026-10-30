#include <stdint.h>
#include <stdio.h>
#include "../../runner/video/genesis_dac.h"

static uint32_t color_reference(uint16_t word, int level)
{
    static const uint8_t ladder[15] = {0,27,49,71,87,103,119,130,146,157,174,190,206,228,255};
    unsigned r=(word>>1)&7u,g=(word>>5)&7u,b=(word>>9)&7u;
    if(level==1) { /* shadow: unscaled channel positions */ }
    else if(level==2) { r+=7;g+=7;b+=7; }
    else { r*=2;g*=2;b*=2; }
    return UINT32_C(0xff000000)|((uint32_t)ladder[r]<<16)|((uint32_t)ladder[g]<<8)|ladder[b];
}

static int palette_cases(void)
{
    static const int levels[] = {-17,0,1,2,3,2147483647};
    for(unsigned word=0;word<65536u;++word)
        for(unsigned i=0;i<sizeof levels/sizeof levels[0];++i) {
            uint32_t expected=color_reference((uint16_t)word,levels[i]);
            if(genesis_dac_cram_to_argb((uint16_t)word,levels[i])!=expected) {
                fprintf(stderr,"complete CRAM domain failed word=%u level=%d\n",word,levels[i]);
                return 1;
            }
        }
    return 0;
}

int main(void)
{
    if(palette_cases())return 1;
    puts("graphics derivation: all65536 CRAM words/6 shading levels match independent nonlinear DAC ladder");
    return 0;
}
