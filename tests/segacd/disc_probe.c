#include "../../runner/cd/disc_image.h"
#include "../../runner/cd/sector.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void print_sha256(const uint8_t digest[32])
{
    size_t i;
    for (i = 0; i < 32; ++i) (void)printf("%02x", (unsigned)digest[i]);
}

int main(int argc, char **argv)
{
    char error[160];
    SegaCDImage *image;
    const SegaCDTOC *toc;
    const uint8_t *digest;
    uint8_t raw[2352];
    uint64_t total_read = 0;
    uint64_t mode1_ok = 0, bad_sync = 0, unsupported_mode = 0, bad_edc = 0;
    int32_t lba;
    unsigned track_index = 0, i;

    if (argc != 2) {
        (void)fprintf(stderr, "usage: disc_probe IMAGE.sgcd\n");
        return 2;
    }
    image = segacd_image_open(argv[1], error, sizeof(error));
    if (image == NULL) {
        (void)fprintf(stderr, "cannot open image: %s\n", error);
        return 2;
    }
    toc = segacd_image_toc(image);
    digest = segacd_image_sha256(image);
    if (toc == NULL || digest == NULL) {
        (void)fprintf(stderr, "image reader returned incomplete metadata\n");
        segacd_image_close(image);
        return 2;
    }

    for (lba = -150; lba < toc->leadout; ++lba) {
        SegaCDSectorResult result;
        const SegaCDTrack *track;
        if (!segacd_image_read(image, lba, raw)) {
            (void)fprintf(stderr, "raw-sector read failed at LBA %" PRId32 "\n", lba);
            segacd_image_close(image);
            return 2;
        }
        ++total_read;
        while (track_index + 1u < toc->count && lba >= toc->track[track_index].end)
            ++track_index;
        track = &toc->track[track_index];
        if (track->data == 0 || lba < track->start || lba >= track->end)
            continue;
        result = segacd_sector_validate(raw);
        switch (result) {
        case SEGACD_SECTOR_MODE1_OK: ++mode1_ok; break;
        case SEGACD_SECTOR_BAD_SYNC: ++bad_sync; break;
        case SEGACD_SECTOR_UNSUPPORTED_MODE: ++unsupported_mode; break;
        case SEGACD_SECTOR_BAD_EDC: ++bad_edc; break;
        }
    }

    (void)printf("{\n  \"track_count\": %u,\n  \"leadout\": %" PRId32 ",\n  \"payload_sha256\": \"", toc->count, toc->leadout);
    print_sha256(digest);
    (void)printf("\",\n  \"tracks\": [\n");
    for (i = 0; i < toc->count; ++i) {
        const SegaCDTrack *track = &toc->track[i];
        (void)printf("    {\"number\": %u, \"index00\": %" PRId32
                     ", \"start\": %" PRId32 ", \"end\": %" PRId32
                     ", \"data\": %u}%s\n",
                     i+1u, track->index00, track->start, track->end,
                     (unsigned)track->data, i+1u == toc->count ? "" : ",");
    }
    (void)printf("  ],\n  \"total_raw_sectors_read\": %" PRIu64
                 ",\n  \"program_data_mode1_edc_ok\": %" PRIu64
                 ",\n  \"program_data_bad_sync\": %" PRIu64
                 ",\n  \"program_data_unsupported_mode\": %" PRIu64
                 ",\n  \"program_data_bad_edc\": %" PRIu64 "\n}\n",
                 total_read, mode1_ok, bad_sync, unsupported_mode, bad_edc);
    segacd_image_close(image);
    return bad_sync != 0 || unsupported_mode != 0 || bad_edc != 0 ? 3 : 0;
}
