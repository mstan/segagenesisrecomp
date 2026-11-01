#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "audio/ym2612.h"
void ref_ym2612_init(void);
void ref_ym2612_advance(uint32_t);
void ref_ym2612_write(uint8_t,uint8_t);
size_t ref_ym2612_render(int16_t *,size_t);
size_t ref_ym2612_samples_available(void);
size_t ref_ym2612_rb_save(void *,size_t);
int ref_ym2612_rb_load(const void *,size_t);
uint64_t ym2612_cosim_hash(void);
uint64_t ref_ym2612_cosim_hash(void);
}
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"FM parity failed line %d\n",__LINE__);std::exit(1); } } while(0)
static uint32_t seed=0x12345678;
static uint32_t random_u32() { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static void write(uint8_t port,uint8_t value) { ym2612_write(port,value);ref_ym2612_write(port,value); }
static void reg(uint8_t part,uint8_t addr,uint8_t value) { write(part,addr);write(part+1,value); }
static void check_state()
{
    CHECK(ym2612_cosim_hash()==ref_ym2612_cosim_hash());
    size_t n=ym2612_rb_save(nullptr,0);CHECK(n==ref_ym2612_rb_save(nullptr,0));
    std::vector<uint8_t> a(n),b(n);
    CHECK(ym2612_rb_save(a.data(),n)==n);CHECK(ref_ym2612_rb_save(b.data(),n)==n);
    CHECK(a==b);
}
static void advance(uint32_t n) { ym2612_advance(n);ref_ym2612_advance(n);check_state(); }
static void drain(size_t requested)
{
    CHECK(ym2612_samples_available()==ref_ym2612_samples_available());
    std::vector<int16_t> a(requested*2),b(requested*2);
    CHECK(ym2612_render(a.data(),requested)==ref_ym2612_render(b.data(),requested));
    CHECK(a==b);check_state();
}
int main()
{
    ym2612_init();ref_ym2612_init();
    for(uint8_t part=0;part<4;part+=2)for(uint8_t ch=0;ch<3;++ch) {
        reg(part,0xb0+ch,7);reg(part,0xb4+ch,0xc0);
        for(uint8_t op=0;op<4;++op) {
            reg(part,0x30+op*4+ch,1);reg(part,0x40+op*4+ch,0x10);
            reg(part,0x50+op*4+ch,0x1f);reg(part,0x60+op*4+ch,0x08);
            reg(part,0x80+op*4+ch,0x0f);
        }
        reg(part,0xa4+ch,0x22);reg(part,0xa0+ch,0x69);
        reg(0,0x28,(uint8_t)(0xf0|ch|(part?4:0)));
    }
    for(unsigned i=0;i<1200;++i) {
        advance(random_u32()%32000);
        uint8_t part=(uint8_t)(random_u32()&2),addr=(uint8_t)random_u32();
        reg(part,addr,(uint8_t)random_u32());check_state();
        if(i%7==0)drain(random_u32()%512+1);
    }
    drain(16384);advance(1008u*9000u+123);advance(1008u*64u);
    drain(127);advance(1008u*100u);drain(16384);
    advance(0xffffffffu);drain(16384);
    size_t n=ym2612_rb_save(nullptr,0);std::vector<uint8_t> a(n),b(n);
    ym2612_rb_save(a.data(),n);ref_ym2612_rb_save(b.data(),n);
    advance(20000);drain(100);
    CHECK(ym2612_rb_load(a.data(),a.size()));CHECK(ref_ym2612_rb_load(b.data(),b.size()));
    advance(15000);drain(1000);
    std::puts("FM block/scalar: PCM, chip/filter state, arbitrary writes, remainder/wrap, full scratch and restore matched");
    return 0;
}
