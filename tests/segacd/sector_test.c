#include "../../runner/cd/sector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        (void)fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

enum { RAW = 2352, EDC_OFFSET = 2064 };

/* Independent known fixture: Python polynomial long division using the normal
 * product (x^16+x^15+x^2+1)(x^16+x^2+x+1) = 0x18001801b. The input bits are
 * consumed in each byte's transmitted LSB-first order; reverse the 32-bit
 * remainder to obtain the stored little-endian reflected EDC 0x7d100170. */
static const uint8_t known_edc[4] = {0x70,0x01,0x10,0x7d};

static void make_fixture(uint8_t raw[RAW])
{
    size_t i;
    memset(raw, 0, RAW);
    memset(raw+1, 0xff, 10);
    raw[12]=0x00; raw[13]=0x02; raw[14]=0x10; raw[15]=0x01;
    for (i=16; i<EDC_OFFSET; ++i)
        raw[i]=(uint8_t)(i*37u+11u);
    memcpy(raw+EDC_OFFSET, known_edc, sizeof(known_edc));
}

int main(void)
{
    uint8_t raw[RAW];
    make_fixture(raw);
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_MODE1_OK);
    CHECK(segacd_sector_validate(NULL) == SEGACD_SECTOR_BAD_SYNC);

    raw[500] ^= 0x01;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_BAD_EDC);
    raw[500] ^= 0x01;
    raw[EDC_OFFSET+2] ^= 0x80;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_BAD_EDC);

    make_fixture(raw);
    raw[0] = 0x01;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_BAD_SYNC);
    make_fixture(raw);
    raw[15] = 0x02;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_UNSUPPORTED_MODE);
    make_fixture(raw);
    raw[15] = 0x00;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_UNSUPPORTED_MODE);

    /* Intermediate/reserved bytes and ECC lie outside the EDC input and are
     * deliberately not validated by this function. They are neither checked
     * nor repaired here. */
    make_fixture(raw);
    raw[2068] = 0xa5;
    raw[2076] ^= 0x80;
    raw[2351] ^= 0x11;
    CHECK(segacd_sector_validate(raw) == SEGACD_SECTOR_MODE1_OK);

    puts("sector validator tests passed");
    return 0;
}
