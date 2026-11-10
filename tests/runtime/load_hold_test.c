#include "load_hold.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static unsigned gold_in_bar(const uint32_t *p)
{
    unsigned n=0;
    for(int x=12;x<228;++x)if(p[27*GENESIS_LOAD_HOLD_WIDTH+x]==0xFFF6C66Bu)++n;
    return n;
}
int main(int argc,char **argv)
{
    GenesisLoadHold h={0};
    uint32_t pixels[GENESIS_LOAD_HOLD_WIDTH*GENESIS_LOAD_HOLD_HEIGHT];
    CHECK(!genesis_load_hold_update(&h,9000,1,1)); /* A held key alone cannot arm. */
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,1000);
    CHECK(!genesis_load_hold_update(&h,2499,1,1));
    CHECK(genesis_load_hold_visible(&h));
    CHECK(genesis_load_hold_update(&h,2500,1,1)==1);
    CHECK(!genesis_load_hold_visible(&h));
    CHECK(!genesis_load_hold_update(&h,9000,1,1));
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_KEYBOARD,4,9000);
    CHECK(h.source==GENESIS_LOAD_HOLD_PAD); /* One press cannot repeat or switch slots. */
    CHECK(!genesis_load_hold_update(&h,9001,0,1));
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_KEYBOARD,4,10000);
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_KEYBOARD,4,10500);
    CHECK(!genesis_load_hold_update(&h,11499,1,1));
    CHECK(genesis_load_hold_update(&h,11500,1,1)==4); /* Repeat did not restart timer. */
    genesis_load_hold_cancel(&h);
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,12000);
    CHECK(!genesis_load_hold_update(&h,12750,0,1));
    CHECK(!genesis_load_hold_update(&h,14000,1,1)); /* Release cancels rather than suspends. */
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,14000);
    CHECK(!genesis_load_hold_update(&h,15499,1,1));
    CHECK(genesis_load_hold_update(&h,15500,1,1)==1);
    genesis_load_hold_cancel(&h);
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,16000);
    CHECK(!genesis_load_hold_update(&h,17000,1,0)); /* Focus/menu/netplay gate. */
    CHECK(!genesis_load_hold_update(&h,19000,1,1));
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_KEYBOARD,9,UINT64_C(0x100000000));
    CHECK(genesis_load_hold_update(&h,UINT64_C(0x100000000)+1500,1,1)==9);
    genesis_load_hold_cancel(&h);
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,100);
    CHECK(!genesis_load_hold_update(&h,99,1,1)); /* Invalid clock cannot fire. */
    CHECK(!h.source);
    genesis_load_hold_begin(&h,0,1,0);genesis_load_hold_begin(&h,1,0,0);
    genesis_load_hold_begin(&h,1,10,0);CHECK(!h.source);
    genesis_load_hold_draw(&h,0,pixels);
    for(unsigned i=0;i<sizeof pixels/sizeof *pixels;++i)CHECK(!pixels[i]);
    genesis_load_hold_begin(&h,GENESIS_LOAD_HOLD_PAD,1,0);
    genesis_load_hold_draw(&h,0,pixels);CHECK(gold_in_bar(pixels)==0);
    genesis_load_hold_draw(&h,750,pixels);CHECK(gold_in_bar(pixels)==108);
    if(argc>1){FILE *f=fopen(argv[1],"wb");CHECK(f);CHECK(fwrite(pixels,1,sizeof pixels,f)==sizeof pixels);CHECK(!fclose(f));}
    genesis_load_hold_draw(&h,1499,pixels);CHECK(gold_in_bar(pixels)>=215);
    genesis_load_hold_update(&h,1500,1,1);
    genesis_load_hold_draw(&h,1500,pixels);
    for(unsigned i=0;i<sizeof pixels/sizeof *pixels;++i)CHECK(!pixels[i]);
    puts("PASS 1500ms threshold, release/gate cancellation, fresh-press rearm, one-shot, clock and progress rendering");
    return 0;
}
