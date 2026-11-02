#include "rf5c164.h"
#include <string.h>

/* Sample kernel extracted from the independently qualified campaign chip,
 * game/trilogy/scd_audio.c. The original program driver is not included. */
void rf5c164_reset(Rf5c164 *p)
{
    memset(p->channel,0,sizeof p->channel);
    p->selected=p->bank=p->enabled=0;p->disabled=255;p->phase=0;
    p->left=p->right=0;p->samples=0;
}
void rf5c164_init(Rf5c164 *p){memset(p,0,sizeof *p);rf5c164_reset(p);}
uint8_t rf5c164_read(const Rf5c164 *p,unsigned a)
{
    a&=0x1FFFu;
    if(a>=0x1000u)return p->enabled?0xFF:p->wave[((unsigned)p->bank<<12)|(a&0xFFFu)];
    if(a>=0x10 && a<0x20) {
        unsigned position=(p->channel[(a-0x10)/2].address>>11)&65535u;
        return (uint8_t)(a&1u?position>>8:position);
    }
    return 0xFF; /* Write-only/unused chip registers have no fabricated value. */
}
void rf5c164_write(Rf5c164 *p,unsigned a,uint8_t v)
{
    a&=0x1FFFu;
    if(a>=0x1000u){p->wave[((unsigned)p->bank<<12)|(a&0xFFFu)]=v;return;}
    Rf5c164Channel*c=&p->channel[p->selected];
    switch(a) {
    case 0:c->env=v;break;
    case 1:c->pan=v;break;
    case 2:c->fdl=v;break;
    case 3:c->fdh=v;break;
    case 4:c->lsl=v;break;
    case 5:c->lsh=v;break;
    case 6:c->start=v;if(!c->on)c->address=(uint32_t)v<<19;break;
    case 7:
        if(v&0x40u)p->selected=v&7u;else p->bank=v&15u;
        p->enabled=v>>7;break;
    case 8:
        p->disabled=v;
        for(unsigned n=0;n<8;++n){p->channel[n].on=(uint8_t)!((v>>n)&1u);if(!p->channel[n].on)p->channel[n].address=(uint32_t)p->channel[n].start<<19;}
        break;
    default:break;
    }
}
static int16_t saturate(int32_t n){return (int16_t)(n>32767?32767:n<-32768?-32768:n);}
void rf5c164_sample(Rf5c164 *p,int16_t *left,int16_t *right)
{
    int32_t l=0,r=0;
    if(p->enabled)for(unsigned n=0;n<8;++n) {
        Rf5c164Channel*c=&p->channel[n];if(!c->on)continue;
        uint8_t sample=p->wave[(c->address>>11)&65535u];
        if(sample==255) {
            c->address=(uint32_t)(((unsigned)c->lsh<<8)|c->lsl)<<11;
            sample=p->wave[(c->address>>11)&65535u];if(sample==255)continue;
        } else c->address=(c->address+((uint32_t)c->fdh<<8)+c->fdl)&0x7FFFFFFu;
        int32_t magnitude=sample&127u;
        int32_t lv=(magnitude*(c->pan&15u)*c->env)>>5;
        int32_t rv=(magnitude*(c->pan>>4)*c->env)>>5;
        if(sample&128u){l+=lv;r+=rv;}else{l-=lv;r-=rv;}
    }
    p->left=saturate(l);p->right=saturate(r);++p->samples;
    if(left)*left=p->left;
    if(right)*right=p->right;
}
void rf5c164_advance(Rf5c164 *p,uint32_t clocks,Rf5c164Sink sink,void *user)
{
    uint64_t total=(uint64_t)p->phase+clocks;
    uint32_t count=(uint32_t)(total/384u);p->phase=(uint16_t)(total%384u);
    for(uint32_t n=0;n<count;++n){rf5c164_sample(p,NULL,NULL);if(sink)sink(user,p->left,p->right);}
}
void rf5c164_advance_timed(Rf5c164 *p,uint64_t start,uint32_t clocks,Rf5c164TimedSink sink,void *user)
{
    uint32_t phase=p->phase;
    uint64_t total=(uint64_t)phase+clocks;
    uint32_t count=(uint32_t)(total/384u);
    p->phase=(uint16_t)(total%384u);
    uint32_t first=384u-phase;
    for(uint32_t n=0;n<count;++n) {
        uint64_t stamp=start+(uint64_t)first+(uint64_t)n*384u;
        rf5c164_sample(p,NULL,NULL);
        if(sink)sink(user,stamp,p->left,p->right);
    }
}
