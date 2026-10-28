#include "../../runner/cd/disc_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        (void)fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

enum { RAW = 2352, HEADER = 96, TRACK = 32, PAYLOAD = 153 * RAW, TOTAL = HEADER + 2 * TRACK + PAYLOAD };

static const uint8_t known_digest[32] = {
    0xf1,0x67,0x33,0x7c,0x4d,0xb6,0x7d,0x03,0xf1,0x48,0x26,0xd3,0x7c,0x05,0xc3,0x77,
    0xc8,0xd3,0x8d,0x1a,0x64,0x4f,0xc5,0x03,0x0b,0x64,0x25,0x89,0x66,0x9b,0x83,0xcc
};

/* Python hashlib.sha256 over the exact synthetic payload made below: 153 raw
 * sectors, all zero except LBA 0 filled with 0x11 and LBA 2 filled with 0x22. */

static void put32(uint8_t *p, uint32_t n)
{
    p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}

static void put64(uint8_t *p, uint64_t n)
{
    put32(p, (uint32_t)n); put32(p+4, (uint32_t)(n>>32));
}

static void put_track(uint8_t *p, uint32_t number, uint32_t kind, int32_t index00, int32_t start, int32_t end)
{
    put32(p, number); put32(p+4, kind); put32(p+8, (uint32_t)index00);
    put32(p+12, (uint32_t)start); put32(p+16, (uint32_t)end);
    memset(p+20, 0, 12);
}

static void make_fixture(const char *path)
{
    uint8_t *bytes = (uint8_t *)calloc(1, TOTAL);
    FILE *file;
    CHECK(bytes != NULL);
    memcpy(bytes, "SGCDIMG\0", 8);
    put32(bytes+8, 1); put32(bytes+12, HEADER+2*TRACK); put32(bytes+16, RAW); put32(bytes+20, 2);
    put32(bytes+24, (uint32_t)-150); put32(bytes+28, 3); put64(bytes+32, HEADER+2*TRACK);
    put64(bytes+40, PAYLOAD); memcpy(bytes+48, known_digest, sizeof(known_digest));
    put_track(bytes+HEADER, 1, 0, -150, 0, 1);
    put_track(bytes+HEADER+TRACK, 2, 1, 1, 2, 3);
    memset(bytes+HEADER+2*TRACK+150*RAW, 0x11, RAW);
    memset(bytes+HEADER+2*TRACK+152*RAW, 0x22, RAW);
    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(bytes, 1, TOTAL, file) == TOTAL);
    CHECK(fclose(file) == 0);
    free(bytes);
}

static void mutate_byte(const char *path, long offset, uint8_t value)
{
    FILE *file = fopen(path, "r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, offset, SEEK_SET) == 0);
    CHECK(fputc(value, file) != EOF);
    CHECK(fclose(file) == 0);
}

static void mutate_bytes(const char *path, long offset, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, offset, SEEK_SET) == 0);
    CHECK(fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void append_byte(const char *path, uint8_t value)
{
    FILE *file = fopen(path, "ab");
    CHECK(file != NULL);
    CHECK(fputc(value, file) != EOF);
    CHECK(fclose(file) == 0);
}

static int all_byte(const uint8_t *p, size_t n, uint8_t value)
{
    size_t i;
    for (i=0; i<n; ++i) if (p[i] != value) return 0;
    return 1;
}

static void expect_bad(const char *path)
{
    char error[96];
    SegaCDImage *image = segacd_image_open(path, error, sizeof(error));
    CHECK(image == NULL);
    CHECK(error[0] != '\0');
}

static void truncate_fixture(const char *path, size_t length)
{
    uint8_t *bytes = (uint8_t *)malloc(length);
    FILE *file = fopen(path, "rb");
    CHECK(bytes != NULL && file != NULL);
    CHECK(fread(bytes, 1, length, file) == length);
    CHECK(fclose(file) == 0);
    file = fopen(path, "wb");
    CHECK(file != NULL && fwrite(bytes, 1, length, file) == length);
    CHECK(fclose(file) == 0);
    free(bytes);
}

int main(void)
{
    const char *path = "disc_image_reader_fixture.sgcd";
    char error[96];
    uint8_t sector[RAW];
    SegaCDImage *image, *second;
    const SegaCDTOC *toc;
    struct MalformedCase { long offset; uint8_t bytes[8]; size_t size; };
    static const struct MalformedCase malformed[] = {
        { 0, {'X'}, 1 },                                    /* wrong magic */
        { 12, { 95,0,0,0 }, 4 },                            /* header size */
        { 16, { 0,8,0,0 }, 4 },                             /* sector size */
        { 20, { 0,0,0,0 }, 4 },                             /* zero tracks */
        { 20, { 100,0,0,0 }, 4 },                           /* too many tracks */
        { 24, { 0,0,0,0 }, 4 },                             /* first LBA */
        { 28, { 0,0,0,0 }, 4 },                             /* zero leadout */
        { 28, { 0x3a,0xdd,0x06,0x00 }, 4 },                 /* leadout 449850 */
        { 32, { 0,0,0,0,0,0,0,0 }, 8 },                    /* payload offset */
        { 40, { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff }, 8 }, /* overflow size */
        { 80, { 1 }, 1 },                                   /* header reserved */
        { HEADER+20, { 1 }, 1 }                             /* track reserved */
    };
    size_t i;

    CHECK(segacd_image_toc(NULL) == NULL);
    CHECK(segacd_image_sha256(NULL) == NULL);
    CHECK(segacd_image_resident_sector(NULL, 0) == NULL);
    CHECK(segacd_image_resident_bytes(NULL) == 0);
    CHECK(!segacd_image_read(NULL, 0, NULL));
    CHECK(segacd_image_open(NULL, error, sizeof(error)) == NULL);
    segacd_image_close(NULL);
    make_fixture(path);
    image = segacd_image_open(path, error, sizeof(error));
    CHECK(image != NULL && error[0] == '\0');
    second = segacd_image_open(path, error, sizeof(error));
    CHECK(second != NULL && error[0] == '\0');
    toc = segacd_image_toc(image);
    CHECK(toc != NULL && toc->count == 2 && toc->leadout == 3);
    CHECK(toc->track[0].index00 == -150 && toc->track[0].start == 0 && toc->track[0].end == 1 && toc->track[0].data == 0);
    CHECK(toc->track[1].index00 == 1 && toc->track[1].start == 2 && toc->track[1].end == 3 && toc->track[1].data == 1);
    CHECK(memcmp(segacd_image_sha256(image), known_digest, sizeof(known_digest)) == 0);
    /* Only the first data track is resident, including its INDEX 00. Audio
     * and arbitrary LBAs cannot escape that allocation. */
    CHECK(segacd_image_resident_bytes(image) == 2u*RAW);
    CHECK(segacd_image_resident_sector(image, INT32_MIN) == NULL);
    CHECK(segacd_image_resident_sector(image, 0) == NULL);
    CHECK(all_byte(segacd_image_resident_sector(image, 1), RAW, 0));
    CHECK(all_byte(segacd_image_resident_sector(image, 2), RAW, 0x22));
    CHECK(segacd_image_resident_sector(image, 3) == NULL);
    CHECK(segacd_image_resident_sector(image, INT32_MAX) == NULL);
    CHECK(segacd_image_read(image, -150, sector) && all_byte(sector, sizeof(sector), 0));
    CHECK(segacd_image_read(image, -1, sector) && all_byte(sector, sizeof(sector), 0));
    CHECK(segacd_image_read(image, 0, sector) && all_byte(sector, sizeof(sector), 0x11));
    SegaCDImageHostStats host_stats;
    CHECK(segacd_image_host_stats(image,&host_stats));
    CHECK(host_stats.raw_reads==3&&host_stats.host_seeks==2&&host_stats.resident_reads==0);
    CHECK(host_stats.private_resident_bytes==0 || host_stats.private_resident_bytes==2u*RAW);
    CHECK(host_stats.private_resident_bytes!=0 || host_stats.mapped_view_bytes>=2u*RAW);
    CHECK(!segacd_image_host_stats(NULL,&host_stats)&&!segacd_image_host_stats(image,NULL));
    CHECK(segacd_image_read(second, 2, sector) && all_byte(sector, sizeof(sector), 0x22));
    CHECK(segacd_image_read(image, 1, sector) && all_byte(sector, sizeof(sector), 0));
    CHECK(segacd_image_read(second, 0, sector) && all_byte(sector, sizeof(sector), 0x11));
    CHECK(segacd_image_read(image, 2, sector) && all_byte(sector, sizeof(sector), 0x22));
    memset(sector, 0xa5, sizeof(sector));
    CHECK(!segacd_image_read(image, -151, sector) && all_byte(sector, sizeof(sector), 0xa5));
    CHECK(!segacd_image_read(image, INT32_MIN, sector) && all_byte(sector, sizeof(sector), 0xa5));
    CHECK(!segacd_image_read(image, INT32_MAX, sector) && all_byte(sector, sizeof(sector), 0xa5));
    CHECK(!segacd_image_read(image, 3, sector) && all_byte(sector, sizeof(sector), 0xa5));
    CHECK(!segacd_image_read(image, 0, NULL));
    segacd_image_close(image);
    segacd_image_close(second);

    for (i = 0; i < sizeof(malformed)/sizeof(malformed[0]); ++i) {
        make_fixture(path);
        mutate_bytes(path, malformed[i].offset, malformed[i].bytes, malformed[i].size);
        expect_bad(path);
    }
    make_fixture(path); mutate_byte(path, 8, 2); expect_bad(path); /* version */
    make_fixture(path); mutate_byte(path, HEADER, 2); expect_bad(path); /* track ID */
    make_fixture(path); mutate_byte(path, HEADER+TRACK+8, 2); expect_bad(path); /* noncontiguous INDEX 00 */
    make_fixture(path); mutate_byte(path, TOTAL-1, 1); expect_bad(path); /* payload hash */
    make_fixture(path); append_byte(path, 0xee); expect_bad(path); /* extra trailer */
    make_fixture(path);
    truncate_fixture(path, TOTAL-1);
    expect_bad(path); /* exact file length/truncated payload */
    CHECK(remove(path) == 0);
    puts("disc image reader tests passed");
    return 0;
}
