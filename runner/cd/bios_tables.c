#include "bios_tables.h"
#include <stddef.h>
#include <string.h>
#include "sha256.h"

int segacd_bios_protection_tables(unsigned bits, uint8_t polynomial,
    uint8_t *work, uint8_t *powers, uint8_t *logarithms)
{
    uint8_t exponent[256] = {0}, inverse[256] = {0}, seen[256] = {0};
    if ((bits != 6 && bits != 8) || !work || !powers || !logarithms)
        return 0;
    unsigned count = 1u << bits;
    for (unsigned i = 1; i <= bits; ++i)
        exponent[i] = (uint8_t)(1u << (i - 1));
    for (unsigned i = bits + 1; i < count; ++i) {
        uint8_t value = 0;
        for (unsigned coefficient = 0; coefficient < bits; ++coefficient)
            if (polynomial & (1u << coefficient))
                value ^= exponent[i - coefficient - 1];
        exponent[i] = value;
    }
    /* The firmware sorts (value,index) pairs with an exchange sort. For a
     * primitive polynomial every field element occurs exactly once. Its
     * sorted values are therefore the identity and its sorted indices are
     * the inverse permutation. Construct both directly, in linear space and
     * O(count*bits) work, without retaining the quadratic sorting algorithm. */
    for (unsigned i = 0; i < count; ++i) {
        unsigned value = exponent[i];
        if (value >= count || seen[value])
            return 0;
        seen[value] = 1;
        inverse[value] = (uint8_t)i;
    }
    memcpy(powers, exponent, count);
    memcpy(logarithms, inverse, count);
    for (unsigned i = 0; i < count; ++i)
        work[i] = (uint8_t)i;
    return 1;
}

static int overlap(uint32_t a, unsigned an, uint32_t b, unsigned bn)
{ return a < b + bn && b < a + an; }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }

int segacd_bios_protection_service(SegaCD *cd, M68kStepContext *cpu)
{
    /* Hashes are profile metadata, not embedded firmware. The complete helper
     * is rechecked in current mutable PRG RAM on every invocation. */
    static const uint8_t expected[32]={
        0xe1,0xe5,0x90,0xb3,0x05,0xe8,0xab,0x7e,0x93,0x0f,0x56,0x31,0x15,0x84,0x18,0x58,
        0xee,0x28,0xa3,0xc7,0xd5,0x1a,0x26,0x5c,0xc9,0x7b,0x8a,0x19,0xd5,0x66,0x0f,0xd3};
    if (!cd || !cpu || cpu->state.instruction_pc != 0x50d8u || cpu->state.regs.PC != 0x50dcu ||
        (cpu->state.regs.SR & 0xa000u) != 0x2000u || cpu->state.fault || cpu->state.stopped)
        return 0;
    unsigned bits, count, final_d0, polynomial=0;
    if (cpu->state.regs.D[2] == 0x00080100u) {bits=8;count=256;final_d0=176;}
    else if (cpu->state.regs.D[2] == 0x00060040u) {bits=6;count=64;final_d0=59;}
    else return 0;
    uint32_t addresses[3]={cpu->state.regs.A[0],cpu->state.regs.A[3],cpu->state.regs.A[4]};
    uint8_t *buffers[3];
    uint32_t poly=cpu->state.regs.A[1],sp=cpu->state.regs.A[7];
    if (poly > SEGACD_PRG_BYTES-bits || sp<8u || (sp&1u) ||
        sp>SEGACD_PRG_BYTES-4u || sp-8u<(unsigned)cd->write_protect*512u ||
        overlap(sp-8u,12,0x50d8u,0x5174u-0x50d8u))
        return 0;
    for(unsigned i=0;i<3;++i) {
        SegaCDMemoryRegion view;
        if(addresses[i]<0x6000u ||
           !segacd_memory_region(cd,SEGACD_SUB,addresses[i],count,1,&view) ||
           overlap(addresses[i],count,sp-8u,12) || overlap(addresses[i],count,poly,bits))return 0;
        buffers[i]=view.data+(addresses[i]-view.address);
        for(unsigned j=0;j<i;++j)if(overlap(addresses[i],count,addresses[j],count))return 0;
    }
    if(overlap(poly,bits,sp-8u,12))return 0;
    uint32_t ret=get32(cd->prg+sp);
    if(ret>0xffffffu || (ret&1u))return 0;
    Sha256 sha;uint8_t digest[32];sha_init(&sha);
    sha_update(&sha,cd->prg+0x50d8u,0x5174u-0x50d8u);sha_final(&sha,digest);
    if(memcmp(digest,expected,sizeof digest))return 0;
    for(unsigned i=0;i<bits;++i)if(cd->prg[poly+i])polynomial|=1u<<i;
    if(polynomial!=(bits==8?0xb8u:0x30u))return 0;
    /* The BRMINIT work reservation is private until return. In particular the
     * work/power/inverse arrays, coefficient input and stack may not alias.
     * Resolve ordinary writable PRG or exclusively Sub-owned 2M Word RAM.
     * No bus/device callback occurs before these ephemeral views expire.
     * MMIO, ownership waits, protected PRG and noncontiguous 1M views decline. */
    if(!segacd_bios_protection_tables(bits,(uint8_t)polynomial,
        buffers[0],buffers[1],buffers[2]))return 0;
    /* Match the helper's saved-register stack residue and entire return ABI.
     * The two fixed profiles' final D0 values are verified against firmware;
     * D1's untouched upper half and all other registers remain unchanged. */
    put32(cd->prg+sp-8u,cpu->state.regs.D[6]);
    put32(cd->prg+sp-4u,cpu->state.regs.D[7]);
    cpu->state.regs.D[0]=final_d0;
    cpu->state.regs.D[1]=(cpu->state.regs.D[1]&0xffff0000u)|(count-1u);
    cpu->state.regs.SR=(uint16_t)((cpu->state.regs.SR&0xffe0u)|4u);
    cpu->state.regs.A[7]=sp+4u;cpu->state.regs.PC=ret;
    return 1;
}
