#include "video/genesis_vdp_compose.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t rng=0x6d2b79f5u;
static uint32_t next_random(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}

static uint8_t reference(unsigned a,unsigned b,unsigned s,unsigned so,unsigned high,
                         unsigned shadow,unsigned hilite,unsigned backdrop,int mode)
{
    unsigned ah=(a&0xc0u)==0xc0u,bh=(b&0xc0u)==0xc0u;
    unsigned sh=so&&high,pixel;
    if(sh)pixel=s;else if(ah)pixel=a&63u;else if(bh)pixel=b&63u;
    else if(so)pixel=s;else if(a&64u)pixel=a&63u;else if(b&64u)pixel=b&63u;
    else pixel=backdrop;
    if(mode&&pixel<64u){
        unsigned dark=!(ah||bh||sh),light=0;
        if(shadow)dark=1;
        if(hilite){if(dark)dark=0;else light=1;}
        if(light)pixel+=128u;else if(dark)pixel+=64u;
    }
    return (uint8_t)pixel;
}

int main(void)
{
    uint8_t a[520],b[520],s[520],op[520],hi[520],sh[520],hl[520],out[536];
    for(unsigned test=0;test<12000;++test){
        unsigned count=1u+next_random()%512u,offset=next_random()%8u;
        unsigned backdrop=next_random()%64u;int mode=(int)(test&1u);
        for(unsigned i=0;i<count;++i){
            /* Include stale high-priority bytes behind transparent sprites,
             * host palette indices, both operators and every plane state. */
            a[i]=(uint8_t)next_random();b[i]=(uint8_t)next_random();
            s[i]=(uint8_t)next_random();op[i]=(uint8_t)(next_random()&1u);
            hi[i]=(uint8_t)(next_random()&1u);sh[i]=(uint8_t)(next_random()&1u);
            hl[i]=(uint8_t)(next_random()&1u);
        }
        memset(out,0xa5,sizeof out);
        gvdp_compose_span(out+offset,a,b,s,op,hi,sh,hl,(int)count,(uint8_t)backdrop,mode);
        for(unsigned i=0;i<count;++i)
            if(out[offset+i]!=reference(a[i],b[i],s[i],op[i],hi[i],sh[i],hl[i],backdrop,mode)){
                fprintf(stderr,"composition mismatch test=%u count=%u pixel=%u\n",test,count,i);return 1;
            }
        for(unsigned i=0;i<sizeof out;++i)
            if((i<offset||i>=offset+count)&&out[i]!=0xa5){fputs("composition guard changed\n",stderr);return 1;}
    }
    puts("native compositor: 12000 random spans, alignments, tails, host colors and shadow/highlight matched");
    return 0;
}
