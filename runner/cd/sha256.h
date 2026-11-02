/* Shared portable SHA-256, also used by full media provider fallback. */
#ifndef SEGACD_SHA256_H
#define SEGACD_SHA256_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Sha256;

static const uint32_t sha_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,
    0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,
    0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,
    0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,
    0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,
    0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,
    0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,
    0xc67178f2u
};

static uint32_t rotr32(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha_transform(Sha256 *sha, const uint8_t block[64])
{
    uint32_t w[64], a,b,c,d,e,f,g,h;
    unsigned i;
    for (i = 0; i < 16; ++i) {
        const size_t p = (size_t)i * 4u;
        w[i] = ((uint32_t)block[p] << 24) | ((uint32_t)block[p+1] << 16) |
               ((uint32_t)block[p+2] << 8) | (uint32_t)block[p+3];
    }
    for (; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        const uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=sha->state[0]; b=sha->state[1]; c=sha->state[2]; d=sha->state[3];
    e=sha->state[4]; f=sha->state[5]; g=sha->state[6]; h=sha->state[7];
    for (i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + sha_k[i] + w[i];
        const uint32_t s0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    sha->state[0]+=a; sha->state[1]+=b; sha->state[2]+=c; sha->state[3]+=d;
    sha->state[4]+=e; sha->state[5]+=f; sha->state[6]+=g; sha->state[7]+=h;
}

static void sha_init(Sha256 *sha)
{
    static const uint32_t initial[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memcpy(sha->state, initial, sizeof(initial));
    sha->bytes = 0;
    sha->used = 0;
}

static void sha_update(Sha256 *sha, const uint8_t *data, size_t size)
{
    sha->bytes += (uint64_t)size;
    while (size != 0) {
        size_t take = sizeof(sha->block) - sha->used;
        if (take > size) take = size;
        memcpy(sha->block + sha->used, data, take);
        sha->used += take;
        data += take;
        size -= take;
        if (sha->used == sizeof(sha->block)) {
            sha_transform(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha_final(Sha256 *sha, uint8_t out[32])
{
    const uint64_t bit_count = sha->bytes * 8u;
    unsigned i;
    sha->block[sha->used++] = 0x80;
    if (sha->used > 56) {
        memset(sha->block + sha->used, 0, sizeof(sha->block) - sha->used);
        sha_transform(sha, sha->block);
        sha->used = 0;
    }
    memset(sha->block + sha->used, 0, 56 - sha->used);
    for (i = 0; i < 8; ++i)
        sha->block[63u-i] = (uint8_t)(bit_count >> (i*8u));
    sha_transform(sha, sha->block);
    for (i = 0; i < 8; ++i) {
        out[i*4u] = (uint8_t)(sha->state[i] >> 24);
        out[i*4u+1u] = (uint8_t)(sha->state[i] >> 16);
        out[i*4u+2u] = (uint8_t)(sha->state[i] >> 8);
        out[i*4u+3u] = (uint8_t)sha->state[i];
    }
}

#endif
