#include "fader.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                      __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static int16_t mono_tick(SegaCDFader *fader, int16_t sample)
{
    int16_t right=sample;
    segacd_fader_tick(fader,&sample,&right);
    CHECK(sample==right);
    return sample;
}

static void test_stereo_clock(void)
{
    SegaCDFader fader;
    int16_t left=1024,right=-1024;
    segacd_fader_reset(&fader,0x4000);
    segacd_fader_set_target(&fader,0);
    segacd_fader_tick(&fader,&left,&right);
    CHECK(left==1024 && right==-1024);
    CHECK(segacd_fader_current_raw(&fader)==0x3FF0);
    segacd_fader_tick(&fader,&left,&right);
    CHECK(left==1020 && right==-1020);
    CHECK(segacd_fader_current_raw(&fader)==0x3FE0);
}
static uint16_t field_of(uint16_t raw)
{
    return (uint16_t)((raw >> 4) & UINT16_C(0x07ff));
}

static void test_unity_mute_and_special_codes(void)
{
    SegaCDFader fader;

    segacd_fader_reset(&fader, UINT16_C(0x4000));
    CHECK(segacd_fader_current_raw(&fader) == UINT16_C(0x4000));
    CHECK(segacd_fader_target_raw(&fader) == UINT16_C(0x4000));
    CHECK(mono_tick(&fader, INT16_MIN) == INT16_MIN);
    CHECK(mono_tick(&fader, INT16_MAX) == INT16_MAX);

    segacd_fader_reset(&fader, 0u);
    CHECK(mono_tick(&fader, INT16_MIN) == 0);
    CHECK(mono_tick(&fader, INT16_MAX) == 0);

    segacd_fader_reset(&fader, UINT16_C(0x0010));
    CHECK(mono_tick(&fader, 1024) == 1);
    CHECK(mono_tick(&fader, -1024) == -1);
    segacd_fader_reset(&fader, UINT16_C(0x0020));
    CHECK(mono_tick(&fader, 1024) == 2);
    CHECK(mono_tick(&fader, -1024) == -2);
    segacd_fader_reset(&fader, UINT16_C(0x0030));
    CHECK(mono_tick(&fader, 1024) == 3);
    CHECK(mono_tick(&fader, -1024) == -3);
}

static void test_low_bit_plateaus_and_gain_3100(void)
{
    SegaCDFader fader;
    uint16_t raw;

    /* Field codes 4..7 all use coefficient 1, i.e. gain 1/256. */
    for (raw = UINT16_C(0x0040); raw <= UINT16_C(0x0070);
         raw = (uint16_t)(raw + UINT16_C(0x0010))) {
        segacd_fader_reset(&fader, raw);
        CHECK(mono_tick(&fader, INT16_MAX) == 128);
        CHECK(mono_tick(&fader, INT16_MIN) == -128);
    }

    /* Word 0x3100 has field 0x310 and coefficient 196: 196/256=.765625. */
    segacd_fader_reset(&fader, UINT16_C(0x3100));
    CHECK(mono_tick(&fader, INT16_MAX) == 25087);
    CHECK(mono_tick(&fader, INT16_MIN) == -25088);
}

static void test_fade_duration_and_retarget(void)
{
    SegaCDFader fader;
    unsigned int i;

    segacd_fader_reset(&fader, UINT16_C(0x4000));
    segacd_fader_set_target(&fader, 0u);
    for (i = 0u; i < 1023u; ++i) {
        (void)mono_tick(&fader, 1000);
    }
    CHECK(field_of(segacd_fader_current_raw(&fader)) == 1u);
    CHECK(mono_tick(&fader, 1000) == 1);
    CHECK(field_of(segacd_fader_current_raw(&fader)) == 0u);
    CHECK(mono_tick(&fader, 1000) == 0);

    segacd_fader_reset(&fader, UINT16_C(0x4000));
    segacd_fader_set_target(&fader, 0u);
    for (i = 0u; i < 10u; ++i) {
        (void)mono_tick(&fader, 1000);
    }
    CHECK(field_of(segacd_fader_current_raw(&fader)) == 1014u);
    segacd_fader_set_target(&fader, UINT16_C(0x3100));
    CHECK(field_of(segacd_fader_target_raw(&fader)) == UINT16_C(0x310));
    (void)mono_tick(&fader, 1000);
    CHECK(field_of(segacd_fader_current_raw(&fader)) == 1013u);
    for (i = 0u; i < 229u; ++i) {
        (void)mono_tick(&fader, 1000);
    }
    CHECK(field_of(segacd_fader_current_raw(&fader)) == UINT16_C(0x310));
    CHECK(mono_tick(&fader, 1000) == 766);
    CHECK(field_of(segacd_fader_current_raw(&fader)) == UINT16_C(0x310));
}

static void test_signed_rounding_saturation_and_above_unity(void)
{
    SegaCDFader fader;

    /* Coefficient 1 at field 4: +/-0.5 ties round away from zero. */
    segacd_fader_reset(&fader, UINT16_C(0x0040));
    CHECK(mono_tick(&fader, 128) == 1);
    CHECK(mono_tick(&fader, -128) == -1);

    /* Values above unity are not clamped by this model; output saturates. */
    segacd_fader_reset(&fader, UINT16_C(0x7ff0));
    CHECK(segacd_fader_current_raw(&fader) == UINT16_C(0x7ff0));
    CHECK(mono_tick(&fader, INT16_MAX) == INT16_MAX);
    CHECK(mono_tick(&fader, INT16_MIN) == INT16_MIN);
    CHECK(mono_tick(&fader, 10000) == 19961);
}

static void test_controls_and_null(void)
{
    SegaCDFader fader;

    segacd_fader_reset(&fader, UINT16_C(0xc00f));
    CHECK(segacd_fader_current_raw(&fader) == UINT16_C(0x400e));
    CHECK(segacd_fader_target_raw(&fader) == UINT16_C(0x400e));
    CHECK(segacd_fader_control(&fader) == UINT16_C(0x000e));
    segacd_fader_set_target(&fader, UINT16_C(0x3101));
    CHECK(segacd_fader_target_raw(&fader) == UINT16_C(0x3100));
    CHECK(segacd_fader_control(&fader) == 0u);
    CHECK(segacd_fader_current_raw(NULL) == 0u);
    CHECK(segacd_fader_target_raw(NULL) == 0u);
    CHECK(segacd_fader_control(NULL) == 0u);
    CHECK(mono_tick(NULL, -1234) == -1234);
    segacd_fader_reset(NULL, 0u);
    segacd_fader_set_target(NULL, 0u);
}

int main(void)
{
    test_stereo_clock();
    test_unity_mute_and_special_codes();
    test_low_bit_plateaus_and_gain_3100();
    test_fade_duration_and_retarget();
    test_signed_rounding_saturation_and_above_unity();
    test_controls_and_null();
    (void)puts("CD fader prototype tests passed");
    return EXIT_SUCCESS;
}
