#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../runner/video/genesis_graphics_cache.h"
#include "../../runner/video/genesis_dac.h"

static uint32_t seed = 0x813479abu;
static uint32_t random_word(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}

static int pattern_cases(void)
{
    GenesisPatternCache cache = {0};
    for (unsigned n = 0; n < 20000; ++n) {
        uint32_t bits = n < 3 ? (n == 0 ? 0u : n == 1 ? UINT32_MAX : 0x12345678u)
                              : random_word();
        /* Addresses deliberately collide, including the same address reused
         * by another machine/provider, and values restored after replacement. */
        unsigned address = n * 2048u;
        for (unsigned restore = 0; restore < 2; ++restore) {
            for (unsigned flip = 0; flip < 2; ++flip) {
                uint8_t attr = (uint8_t)(random_word() & 0xb0u);
                uint64_t packed = ggraphics_pattern_row(&cache,address,bits,attr,(int)flip);
                uint8_t pixels[8]; memcpy(pixels,&packed,sizeof pixels);
                for (unsigned x = 0; x < 8; ++x) {
                    unsigned source = flip ? 7u-x : x;
                    unsigned nibble = (bits >> ((7u-source)*4u)) & 15u;
                    uint8_t expected = (uint8_t)(attr | nibble | (nibble ? 64u : 0u));
                    if (pixels[x] != expected) {
                        fprintf(stderr,"pattern value identity failed n=%u flip=%u x=%u\n",n,flip,x);
                        return 1;
                    }
                }
            }
            (void)ggraphics_pattern_row(&cache,address,bits ^ UINT32_MAX,0,0);
        }
    }
    return 0;
}

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
    if(pattern_cases() || palette_cases())return 1;
    puts("graphics derivation: 20000 colliding/restored pattern identities, both flips/current attributes and all65536 CRAM words/6 shading levels pass");
    return 0;
}
