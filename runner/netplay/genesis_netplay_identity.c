/*
 * genesis_netplay_identity.c -- who this build is, for the lobby and the
 * rollback identity handshake (recomp-ai-rules/NETPLAY.md section 4:
 * "version identity must be exact and machine-checked").
 *
 *   build fingerprint   FNV-1a 32 of the running executable's bytes
 *   game_version        "<release>+<build fingerprint hex>": two builds of
 *                       "the same version" from different trees are different
 *                       lobby versions and never see each other's rooms
 *   content fingerprint SHA-256 of the ROM image (lower-case hex, 64 chars)
 *
 * SHA-256 is the FIPS 180-4 algorithm, written out here so the engine needs
 * no crypto dependency (it is a fingerprint, not a security boundary).
 */
#include "genesis_netplay_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/* ---- SHA-256 ------------------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) | ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void genesis_identity_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    uint8_t tail[128];
    size_t full = len & ~(size_t)63, rem = len - full;
    for (size_t o = 0; o < full; o += 64) sha256_block(h, data + o);
    memset(tail, 0, sizeof tail);
    if (rem) memcpy(tail, data + full, rem);
    tail[rem] = 0x80;
    size_t tl = rem + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha256_block(h, tail);
    if (tl == 128) sha256_block(h, tail + 64);
    for (int i = 0; i < 8; i++) {
        out[4*i] = (uint8_t)(h[i] >> 24); out[4*i+1] = (uint8_t)(h[i] >> 16);
        out[4*i+2] = (uint8_t)(h[i] >> 8); out[4*i+3] = (uint8_t)h[i];
    }
}

void genesis_identity_hex(const uint8_t *bytes, size_t n, char *out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2*i] = hx[bytes[i] >> 4]; out[2*i+1] = hx[bytes[i] & 15]; }
    out[2*n] = 0;
}

/* ---- the executable --------------------------------------------------------- */

uint32_t genesis_identity_build_fp(void)
{
    static uint32_t cached;
    char path[1024] = "";
    if (cached) return cached;
#if defined(_WIN32)
    GetModuleFileNameA(NULL, path, sizeof path);
#else
    snprintf(path, sizeof path, "/proc/self/exe");
#endif
    FILE *f = fopen(path, "rb");
    uint32_t h = 2166136261u;
    if (f) {
        unsigned char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= 16777619u; }
        fclose(f);
    } else {
        fprintf(stderr, "genesis_netplay: cannot read the executable for its fingerprint\n");
    }
    cached = h ? h : 1u;
    return cached;
}

const char *genesis_identity_game_version(const char *release)
{
    static char v[64];
    snprintf(v, sizeof v, "%s+%08x", release && release[0] ? release : "0",
             (unsigned)genesis_identity_build_fp());
    return v;
}
