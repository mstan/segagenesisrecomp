#include "fader.h"

#include <stddef.h>

#define FADER_FIELD_MASK UINT16_C(0x07ff)
#define FADER_CONTROL_MASK UINT16_C(0x000e)

typedef struct FaderGain {
    uint16_t numerator;
    uint16_t denominator;
} FaderGain;

static FaderGain fader_gain(uint16_t field)
{
    FaderGain gain;

    if (field < 4u) {
        /* Sanyo's special low codes correspond to -infinity, -60.21,
         * -54.19, and -50.66 dB: respectively 0, 1, 2, 3 over 1024. */
        gain.numerator = field;
        gain.denominator = 1024u;
    } else {
        /* The normal multiplier uses the upper encoded coefficient bits.
         * The lower two FD bits form plateaus until that multiplier is zero. */
        gain.numerator = (uint16_t)(field >> 2);
        gain.denominator = 256u;
    }

    return gain;
}

void segacd_fader_reset(SegaCDFader *fader, uint16_t raw_word)
{
    uint16_t field;
    uint16_t control;

    if (fader == NULL) {
        return;
    }

    field = (uint16_t)((raw_word >> 4) & FADER_FIELD_MASK);
    control = (uint16_t)(raw_word & FADER_CONTROL_MASK);
    fader->current_field = field;
    fader->target_field = field;
    fader->control_bits = control;
}

void segacd_fader_set_target(SegaCDFader *fader, uint16_t raw_word)
{
    if (fader == NULL) {
        return;
    }

    fader->target_field = (uint16_t)((raw_word >> 4) & FADER_FIELD_MASK);
    fader->control_bits = (uint16_t)(raw_word & FADER_CONTROL_MASK);
}

uint16_t segacd_fader_current_raw(const SegaCDFader *fader)
{
    if (fader == NULL) {
        return 0u;
    }

    return (uint16_t)((fader->current_field << 4) | fader->control_bits);
}

uint16_t segacd_fader_target_raw(const SegaCDFader *fader)
{
    if (fader == NULL) {
        return 0u;
    }

    return (uint16_t)((fader->target_field << 4) | fader->control_bits);
}

uint16_t segacd_fader_control(const SegaCDFader *fader)
{
    if (fader == NULL) {
        return 0u;
    }

    return fader->control_bits;
}

static int16_t scale_sample(int16_t sample, FaderGain gain)
{
    int64_t product = (int64_t)sample * (int64_t)gain.numerator;
    int64_t magnitude;
    int64_t rounded;

    if (product < 0) {
        magnitude = -product;
        rounded = (magnitude + (int64_t)(gain.denominator / 2u)) /
                  (int64_t)gain.denominator;
        rounded = -rounded;
    } else {
        rounded = (product + (int64_t)(gain.denominator / 2u)) /
                  (int64_t)gain.denominator;
    }

    if (rounded > INT16_MAX) {
        return INT16_MAX;
    }
    if (rounded < INT16_MIN) {
        return INT16_MIN;
    }

    return (int16_t)rounded;
}

void segacd_fader_tick(SegaCDFader *fader, int16_t *left, int16_t *right)
{
    FaderGain gain;

    if (fader == NULL) {
        return;
    }

    gain = fader_gain(fader->current_field);
    *left = scale_sample(*left, gain);
    *right = scale_sample(*right, gain);

    if (fader->current_field < fader->target_field) {
        fader->current_field++;
    } else if (fader->current_field > fader->target_field) {
        fader->current_field--;
    }

}
