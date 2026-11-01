#include "audio/sn76489.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void ref_psg_init(void);
void ref_psg_advance(uint32_t);
void ref_psg_write(uint8_t);
size_t ref_psg_render(int16_t *,size_t);
size_t ref_psg_samples_available(void);
void ref_psg_reset_leftover(void);
size_t ref_psg_rb_save(void *,size_t);
int ref_psg_rb_load(const void *,size_t);
uint64_t psg_cosim_hash(void);
uint64_t ref_psg_cosim_hash(void);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"PSG span line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t seed=0x1830572u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void check(void)
{
    uint8_t a[17000],b[17000];size_t n=psg_rb_save(NULL,0);
    CHECK(n<=sizeof a&&n==ref_psg_rb_save(NULL,0));
    CHECK(psg_rb_save(a,sizeof a)==n&&ref_psg_rb_save(b,sizeof b)==n);
    CHECK(!memcmp(a,b,n)&&psg_cosim_hash()==ref_psg_cosim_hash());
}
static void advance(uint32_t cycles){psg_advance(cycles);ref_psg_advance(cycles);check();}
static void write_byte(uint8_t value){psg_write(value);ref_psg_write(value);check();}
static void drain(size_t count)
{
    int16_t a[8192],b[8192];memset(a,0x5a,sizeof a);memset(b,0x5a,sizeof b);
    CHECK(count<=8192&&psg_samples_available()==ref_psg_samples_available());
    CHECK(psg_render(a,count)==ref_psg_render(b,count));CHECK(!memcmp(a,b,sizeof a));check();
}
int main(void)
{
    static const unsigned periods[]={0,1,2,15,16,31,1023};
    static const unsigned ticks[]={1,2,15,16,31,32,255,4095,8192};
    for(unsigned noise=0;noise<8;++noise)for(unsigned p=0;p<7;++p) {
        psg_init();ref_psg_init();
        for(unsigned c=0;c<3;++c){write_byte((uint8_t)(0x80u|(c<<5)|(periods[p]&15u)));write_byte((uint8_t)(periods[p]>>4));}
        write_byte((uint8_t)(0xe0u|noise));
        for(unsigned n=0;n<9;++n){advance(ticks[n]*240u+17u);drain(8192);}
        /* Reset noise with an old output latch, then unmute after quiet work. */
        write_byte((uint8_t)(0xe0u|noise));advance(240);drain(1);write_byte(0xf0);advance(240*73u);drain(8192);
        for(unsigned c=0;c<4;++c)write_byte((uint8_t)(0x9fu|(c<<5)));
        advance(240*8192u);drain(8192);advance(240*8192u);drain(8192);
    }
    psg_init();ref_psg_init();
    for(unsigned i=0;i<8000;++i) {
        if(i%97u==0){psg_init();ref_psg_init();}
        if(i%13u==0)for(unsigned c=0;c<4;++c)write_byte((uint8_t)(0x9fu|(c<<5)));
        else write_byte((uint8_t)rnd());
        advance(rnd()%2100000u);drain(rnd()%8193u);
        if(i%29u==0) {
            uint8_t state[17000];size_t n=psg_rb_save(state,sizeof state);
            advance(rnd());CHECK(psg_rb_load(state,n)&&ref_psg_rb_load(state,n));check();
            advance(rnd()%30000u);drain(8192);
        }
        if(i%71u==0){psg_reset_leftover();ref_psg_reset_leftover();check();}
    }
    /* Full scratch retains the established drop and remainder behavior. */
    advance(UINT32_MAX);advance(UINT32_MAX);drain(8192);advance(239);drain(8192);
    puts("PSG spans: exact counters/LFSR/filter/PCM, live writes, pending-output restore and scratch overflow PASS");
    return 0;
}
