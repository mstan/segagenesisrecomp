#include "genesis_expansion.h"
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_rom_ext;
uint32_t g_rom_ext_size;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
int main(void)
{
    uint16_t v=0xCAFE;
    CHECK(!gbus_expansion_word(0x400000,&v) && v==0xCAFE);
    g_rom_ext_size=4;CHECK(!gbus_expansion_word(0x400000,&v));
    uint8_t data[]={0x12,0x34,0xAB};g_rom_ext=data;g_rom_ext_size=sizeof data;
    CHECK(gbus_expansion_word(0x400000,&v) && v==0x1234);
    CHECK(gbus_expansion_word(0x400001,&v) && v==0x34AB);
    CHECK(gbus_expansion_word(0x400002,&v) && v==0xABFF);
    CHECK(!gbus_expansion_word(0x400003,&v));
    CHECK(!gbus_expansion_word(0x3FFFFF,&v));
    CHECK(!gbus_expansion_word(0x800000,&v));
    CHECK(!gbus_expansion_word(0xFF0000,&v));
    g_rom_ext_size=0;CHECK(!gbus_expansion_word(0x400000,&v));
    puts("Expansion ROM bounds, byte lanes, partial word and disabled mapping passed");return 0;
}
