/*
 * fm_shadow.cpp — see fm_shadow.h.
 *
 * A second ymfm::ym2612 fed the same register writes as the canon chip,
 * rendered with the high-frequency-taming output LPF relaxed (less aliasing)
 * and optional ladder-effect correction, then policed by ShadowVerifier and
 * substituted only when proven. Canon stays authoritative + oracle.
 *
 * ymfm: Aaron Giles, BSD-3-Clause (runner/external/ymfm). Verifier: the
 * JRickey/gba-recomp shadow.rs port (audio_shadow.h). Wiring is ours.
 */
#include "ymfm_opn.h"

extern "C" {
#include "fm_shadow.h"
#include "audio_shadow.h"
}

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

/* Mirror the canon wrapper's cadence (ym2612_ymfm.cpp): one stereo sample per
 * 144*7 = 1008 MASTER cycles, gain 195/256. We keep the gain identical so the
 * verifier's level-ratio check starts near 1.0; the ENHANCEMENT is the relaxed
 * output filter (cleaner highs), not a different loudness. */
constexpr uint32_t MASTER_PER_SAMPLE = 144u * 7u; /* = 1008 */
constexpr int32_t  YM_GAIN_NUM = 195;
constexpr int32_t  YM_GAIN_DEN = 256;

constexpr size_t FM_SCRATCH_STEREO_SAMPLES = 8192;

struct ShadowIface : public ymfm::ymfm_interface {};

ShadowIface     s_iface;
ymfm::ym2612*   s_chip = nullptr;      /* base view: write/reset/advance */
ymfm::ym3438*   s_chip_3438 = nullptr; /* non-null iff ladder-off; aliases s_chip */
int             s_inited = 0;

int             s_enabled = 0;        /* opted in via env */
int             s_ladder_correct = 0; /* GENESIS_FM_LADDER=off */

/* Float scratch ring for this drain's shadow samples (paired 1:1 with canon).*/
float    s_scratch[FM_SCRATCH_STEREO_SAMPLES * 2];
size_t   s_scratch_write = 0;
size_t   s_scratch_read  = 0;
uint32_t s_master_accum = 0;

ShadowVerifier s_vf;

/* Relaxed output LPF. The canon wrapper uses a stronger 6.910/4.910 one-pole
 * to tame ymfm's excess highs and match the clownmdemu reference. The shadow's
 * purpose is to KEEP that high-frequency content, so we apply a much gentler
 * filter (near-passthrough, just DC-safe) — documented as the enhancement, not
 * a hardware claim. */
constexpr int64_t FM_LPF_SAMPLE_MAGIC = 58982; /* ~0.90 * 65536 (gentle) */
constexpr int64_t FM_LPF_OUTPUT_MAGIC = 6554;  /* ~0.10 * 65536           */
int32_t s_lpf_prev[2] = {0, 0};
int32_t s_lpf_out[2]  = {0, 0};

inline int32_t relaxed_lpf(int ch, int32_t in) {
    int64_t o = ((int64_t)in * FM_LPF_SAMPLE_MAGIC +
                 (int64_t)s_lpf_out[ch] * FM_LPF_OUTPUT_MAGIC) / 65536;
    s_lpf_prev[ch] = in;
    s_lpf_out[ch] = (int32_t)o;
    return (int32_t)o;
}

void emit_one_sample() {
    if (s_scratch_write >= FM_SCRATCH_STEREO_SAMPLES) return;
    ymfm::ym2612::output_data out;
    /* Ladder-effect correction (GENESIS_FM_LADDER=off). The real YM2612's 9-bit
     * multiplexed DAC has a discontinuity around zero (ymfm's dac_discontinuity:
     * negatives -3, positives +4 LSB, a ~7-LSB gap) — the audible "ladder"
     * crossover crunch. ymfm applies it in ym2612::generate(); its ym3438
     * sibling is the SAME FM engine WITHOUT that discontinuity (the later CMOS
     * Genesis FM revision that fixed it). When correction is requested we render
     * the shadow through ym3438 (clean DAC); otherwise ym2612, so the shadow's
     * FM matches canon and only the relaxed LPF differs. generate() is
     * non-virtual, so we dispatch on the concrete pointer. The ShadowVerifier
     * still polices the result: a ladder-free stream that stops correlating with
     * canon reverts loudly (DEGRADED) rather than substituting. */
    if (s_chip_3438) s_chip_3438->generate(&out, 1);
    else             s_chip->generate(&out, 1);
    int32_t l = out.data[0] * YM_GAIN_NUM / YM_GAIN_DEN;
    int32_t r = out.data[1] * YM_GAIN_NUM / YM_GAIN_DEN;
    l = relaxed_lpf(0, l);
    r = relaxed_lpf(1, r);
    s_scratch[s_scratch_write * 2 + 0] = (float)l;
    s_scratch[s_scratch_write * 2 + 1] = (float)r;
    s_scratch_write++;
}

void lazy_init() {
    if (!s_chip) {
        if (s_ladder_correct) {
            s_chip_3438 = new ymfm::ym3438(s_iface); /* clean DAC, no ladder */
            s_chip = s_chip_3438;                    /* base view for write/reset */
        } else {
            s_chip = new ymfm::ym2612(s_iface);
        }
    }
    s_chip->reset();
    s_scratch_write = s_scratch_read = 0;
    s_master_accum = 0;
    s_lpf_prev[0] = s_lpf_prev[1] = 0;
    s_lpf_out[0]  = s_lpf_out[1]  = 0;
    shadow_verifier_init(&s_vf);
    s_inited = 1;
}

} /* namespace */

extern "C" void fm_shadow_init(void) {
    const char* on = getenv("GENESIS_AUDIO_SHADOW");
    s_enabled = (on && on[0] && on[0] != '0') ? 1 : 0;
    const char* ladder = getenv("GENESIS_FM_LADDER");
    s_ladder_correct = (ladder && !strcmp(ladder, "off")) ? 1 : 0;
    if (s_enabled) {
        lazy_init();
        fprintf(stderr,
                "[AUDIO-SHADOW] FM shadow ARMED (DAC=%s). Canon FM "
                "remains authoritative + oracle; substitution only after a "
                "proven window.\n",
                s_ladder_correct ? "ym3438 ladder-free" : "ym2612 (authentic ladder)");
    }
}

extern "C" int fm_shadow_enabled(void) { return s_enabled; }

extern "C" int fm_shadow_active(void) {
    return s_enabled && shadow_verifier_proven(&s_vf);
}

extern "C" void fm_shadow_write(uint8_t port, uint8_t value) {
    if (!s_enabled) return;
    if (!s_inited) lazy_init();
    if (port <= 3) s_chip->write(port, value);
}

extern "C" void fm_shadow_advance(uint32_t cycles_master) {
    if (!s_enabled) return;
    if (!s_inited) lazy_init();
    if (cycles_master == 0) return;
    s_master_accum += cycles_master;
    while (s_master_accum >= MASTER_PER_SAMPLE) {
        s_master_accum -= MASTER_PER_SAMPLE;
        emit_one_sample();
    }
}

extern "C" void fm_shadow_verify_and_substitute(int16_t* fm_stereo_out,
                                                size_t fm_n) {
    if (!s_enabled || fm_stereo_out == nullptr || fm_n == 0) {
        /* Drop any shadow scratch we accumulated so the rings stay paired. */
        s_scratch_read = s_scratch_write = 0;
        return;
    }

    size_t shadow_avail = s_scratch_write - s_scratch_read;
    size_t n = fm_n < shadow_avail ? fm_n : shadow_avail;

    /* Normalize both streams to roughly [-1,1] for the verifier (int16 / 32768).
     * The verifier is scale-tolerant (auto-gain), but a consistent domain keeps
     * its thresholds meaningful. */
    const float kInv = 1.0f / 32768.0f;
    float gain = shadow_verifier_gain(&s_vf);

    for (size_t i = 0; i < n; ++i) {
        float canon_l = (float)fm_stereo_out[i * 2 + 0] * kInv;
        float canon_r = (float)fm_stereo_out[i * 2 + 1] * kInv;
        float sh_l = s_scratch[(s_scratch_read + i) * 2 + 0] * kInv * gain;
        float sh_r = s_scratch[(s_scratch_read + i) * 2 + 1] * kInv * gain;
        shadow_verifier_judge(&s_vf, canon_l, canon_r, sh_l, sh_r);
    }

    /* Surface a revert loudly (DEGRADED). */
    if (s_vf.reverted[0] != '\0') {
        fprintf(stderr, "[AUDIO-SHADOW] DEGRADED — reverting to canon FM: %s\n",
                s_vf.reverted);
        s_vf.reverted[0] = '\0';
    }

    /* Substitute only when proven. Canon stays authoritative otherwise. */
    if (shadow_verifier_proven(&s_vf)) {
        for (size_t i = 0; i < n; ++i) {
            int32_t l = (int32_t)(s_scratch[(s_scratch_read + i) * 2 + 0] * gain);
            int32_t r = (int32_t)(s_scratch[(s_scratch_read + i) * 2 + 1] * gain);
            if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
            if (r >  32767) r =  32767; else if (r < -32768) r = -32768;
            fm_stereo_out[i * 2 + 0] = (int16_t)l;
            fm_stereo_out[i * 2 + 1] = (int16_t)r;
        }
    }

    s_scratch_read = s_scratch_write = 0;
}

extern "C" void fm_shadow_reset(void) {
    if (!s_enabled) return;
    lazy_init();
}
