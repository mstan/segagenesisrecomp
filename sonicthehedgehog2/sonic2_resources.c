#include "sonic2_resources.h"
#include "sonic2_party.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static S2DonorBank banks[S2_RESOURCE_COUNT];
static int owner_verified[S2_RESOURCE_COUNT];
static S2SaveAssets *save_assets;
int s2_resources_prepare_stage_images(const unsigned char *rom,unsigned size)
{ return s2_stage_images_decode(rom,size,save_assets); }
static char errors[S2_RESOURCE_COUNT][192];
static const char *hashes[S2_RESOURCE_COUNT] = {
    "9c028944730128f6b9999fc74babf69694b0edab50e3f42cc6b60a185d0b1457",
    "fba0677fde9f76df93f3e98d6310d8af68b9847bde16e253d73cd4dd8134ed23"
};
static char *resource_path(unsigned i) { return i == S2_RESOURCE_AMY ? s2_party.amy_path : s2_party.s3k_path; }
static int fail(unsigned i, const char *error) { snprintf(errors[i], sizeof errors[i], "%s", error); return 0; }
int s2_resource_set(unsigned i, const char *path)
{
    if (i >= S2_RESOURCE_COUNT || !path) return 0;
    if (strlen(path) >= sizeof s2_party.amy_path) return fail(i, "Donor path is too long");
    if (!*path) {
        s2_donor_free(&banks[i]); resource_path(i)[0] = 0;
        owner_verified[i]=0;
        if (i==S2_RESOURCE_SK) s2_save_assets_free(&save_assets);
        return fail(i, "Select the required owner-supplied ROM");
    }
    size_t size = i == S2_RESOURCE_AMY ? 0x200000 : 0x400000;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(i, "Cannot open donor ROM; previous verified resource is unchanged");
    uint8_t *rom = malloc(size);
    if (!rom) { fclose(f); return fail(i, "Cannot allocate donor verification buffer"); }
    size_t got = fread(rom, 1, size, f); int extra = fgetc(f), io = ferror(f); fclose(f);
    if (got != size || extra != EOF || io) {
        free(rom); return fail(i, "Wrong donor size; expected an unheadered, big-endian .bin/.md ROM");
    }
    uint8_t hash[32]; char hex[65]; recompui_sha256_compute(rom, size, hash);
    for (unsigned n = 0; n < 32; ++n) snprintf(hex + n * 2, 3, "%02x", hash[n]);
    if (strcmp(hex, hashes[i])) { free(rom); return fail(i, "Wrong donor revision (SHA-256 mismatch)"); }
    S2DonorBank candidate={0}; S2SaveAssets *menu=NULL;
    int ok = s2_donor_decode(rom, size, i == S2_RESOURCE_AMY ? &s2_amy_171_layout : &s2_sk_knuckles_layout,
        &candidate, errors[i], sizeof errors[i]);
    if (ok && i==S2_RESOURCE_SK) ok=s2_save_assets_decode(rom,size,&menu,errors[i],sizeof errors[i]);
    free(rom);
    if (!ok) { s2_donor_free(&candidate); s2_save_assets_free(&menu); return 0; }
    /* A failed replacement must not destroy a previously verified resource. */
    s2_donor_free(&banks[i]); banks[i]=candidate; owner_verified[i]=1;
    if (i==S2_RESOURCE_SK) { s2_save_assets_free(&save_assets); save_assets=menu; }
    memmove(resource_path(i), path, strlen(path) + 1);
    errors[i][0] = 0;
    return 1;
}
void s2_resources_shutdown(void)
{
    for (unsigned i = 0; i < S2_RESOURCE_COUNT; ++i) s2_donor_free(&banks[i]);
    memset(owner_verified,0,sizeof owner_verified);
    s2_save_assets_free(&save_assets);
}
void s2_resources_load(const char *settings_path)
{
    s2_resources_shutdown();
    for (unsigned i = 0; i < S2_RESOURCE_COUNT; ++i) {
        errors[i][0] = 0;
        if (*resource_path(i)) s2_resource_set(i, resource_path(i));
        else if (i == S2_RESOURCE_AMY) {
            /* Private import tool stages this next to settings.ini. No owner
             * machine paths or copyrighted binary payloads enter the source. */
            char path[1200]; snprintf(path, sizeof path, "%s", settings_path ? settings_path : "");
            char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
            if (back && (!slash || back > slash)) slash = back;
            if (slash) slash[1] = 0; else path[0] = 0;
            size_t n = strlen(path);
            snprintf(path + n, sizeof path - n, "assets/characters/amy-1.7.1.bin");
            s2_resource_set(i, path);
        } else fail(i, "Select your stock Sonic 3 & Knuckles combined ROM");
    }
}
int s2_resource_verified(unsigned i) { return i < S2_RESOURCE_COUNT && banks[i].count != 0; }
int s2_resource_owner_verified(unsigned i) { return i<S2_RESOURCE_COUNT && owner_verified[i]; }
const S2SaveAssets *s2_resource_save_assets(void) { return owner_verified[S2_RESOURCE_SK]?save_assets:NULL; }
const S2DonorBank *s2_resource_bank(unsigned i) { return s2_resource_verified(i) ? &banks[i] : NULL; }
const char *s2_resource_error(unsigned i) { return i < S2_RESOURCE_COUNT ? errors[i] : "Unknown donor"; }
