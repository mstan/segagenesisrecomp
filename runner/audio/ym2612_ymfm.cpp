/*
 * ym2612_ymfm.cpp — AGPL-free YM2612 (FM) implementation behind the stable
 * `ym2612_*` C API, backed by ymfm (Aaron Giles, BSD-3-Clause; vendored under
 * runner/external/ymfm/). Drop-in replacement for the clownmdemu-backed
 * ym2612.c — selected by the PERMISSIVE_FM CMake option. Exactly one of the two
 * TUs is compiled, so the symbols never collide.
 *
 * Architecture parity with ym2612.c (so the mixer/resampler is untouched):
 *   - Same public API (ym2612_init/advance/write/render/samples_available/
 *     sample_rate) declared in ym2612.h.
 *   - Same output sample rate: 53267 Hz. The real YM2612 is clocked at the 68K
 *     clock (master/7 = 7670453 Hz) and emits one stereo sample every 144 chip
 *     clocks, so one output sample = 144 * 7 = 1008 MASTER cycles. ym2612_advance
 *     takes MASTER cycles and emits one ymfm sample per 1008 of them, carrying
 *     the remainder across calls — identical cadence to the old master/7/144 path.
 *
 * Why no timer/busy emulation: this wrapper is a *shadow synth* driven purely by
 * the register-write event stream. Status/timer reads by the SMPS driver are
 * still serviced by the emulator core's own FM, never by this wrapper, so the
 * only audio-visible effect of timers would be CSM mode (timer-A key-on), which
 * the Sonic SMPS drivers do not use. ymfm's own reference player (vgmrender)
 * likewise uses the default no-op timer interface and produces correct FM music.
 */
#include "ymfm_opn.h"

extern "C" {
#include "ym2612.h"
}

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace {

/* 68K / YM2612 chip clock (NTSC master / 7). sample_rate = clock / 144 = 53267. */
constexpr uint32_t YM_CLOCK = 7670453u;
/* MASTER cycles per emitted stereo sample: 144 chip clocks * 7 (master/68K). */
constexpr uint32_t MASTER_PER_SAMPLE = 144u * 7u; /* = 1008 */

/* Output gain (level match vs the reference render — audio.c mixes FM at
 * FM_VOL_DIV=1, so the match happens here). The reference scales each 9-bit
 * channel sample by 128/8 = x16; ymfm::ym2612 scales the 6-channel sum by
 * 128*64/(6*65) = x21.005 effective per channel (ymfm_opn.cpp generate()).
 * At unity gain that left every FM+DAC voice a flat +2.3 dB over the
 * correctly-leveled PSG — measured as a uniform 1.30-1.32x in all bands with
 * envelope correlation 1.000 (tools/synth_replay), heard as the GHZ bass
 * "boop". Correction: 16 / (8192/390) = 6240/8192 = 195/256 exactly. */
constexpr int32_t YM_GAIN_NUM = 195;
constexpr int32_t YM_GAIN_DEN = 256;

/* Minimal interface: all-default (no timers, no busy, no external memory). */
struct YmfmIface : public ymfm::ymfm_interface {};

YmfmIface       s_iface;
ymfm::ym2612   *s_chip = nullptr;
int             s_inited = 0;

/* Output scratch ring, mirroring ym2612.c's so ym2612_render semantics match. */
constexpr size_t FM_SCRATCH_STEREO_SAMPLES = 8192;
int16_t s_scratch[FM_SCRATCH_STEREO_SAMPLES * 2];
size_t  s_scratch_write = 0; /* next free stereo-sample index */
size_t  s_scratch_read  = 0; /* next unread stereo-sample index */

uint32_t s_master_accum = 0; /* leftover MASTER cycles below one sample */

/* Hardware FM output low-pass — the real Genesis filters the FM DAC output, and
 * clownmdemu models it (low-pass-filter.c, coefficients 6.910, 4.910) after
 * FM_OutputSamples. ymfm emits the bare chip output (excess >8 kHz energy); this
 * filter both matches the reference render and is hardware-correct. A cutoff
 * sweep confirmed these coefficients minimise spectral distance to the
 * reference (~6.7 dB; the remainder is the ymfm-vs-clownmdemu core itself).
 *   out = ((in + prevIn)*SAMPLE_MAGIC + prevOut*OUTPUT_MAGIC) >> 16 */
constexpr int32_t FM_LPF_SAMPLE_MAGIC = 9484;  /* round(1.000 * 65536 / 6.910) */
constexpr int32_t FM_LPF_OUTPUT_MAGIC = 46568; /* round(4.910 * 65536 / 6.910) */
int32_t s_fm_lpf_prev[2] = {0, 0};
int32_t s_fm_lpf_out[2]  = {0, 0};

inline int16_t clamp16(int32_t v) {
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

inline int32_t fm_lpf(int ch, int32_t in) {
    int64_t o = ((int64_t)(in + s_fm_lpf_prev[ch]) * FM_LPF_SAMPLE_MAGIC
               + (int64_t)s_fm_lpf_out[ch] * FM_LPF_OUTPUT_MAGIC) / 65536;
    s_fm_lpf_prev[ch] = in;
    int32_t out = clamp16((int32_t)o);
    s_fm_lpf_out[ch] = out;
    return out;
}

void emit_one_sample() {
    if (s_scratch_write >= FM_SCRATCH_STEREO_SAMPLES) {
        /* Scratch full — consumer didn't drain. Shouldn't happen at
         * ~888 samples/frame into an 8192 buffer; drop to stay safe. */
        return;
    }
    ymfm::ym2612::output_data out;
    s_chip->generate(&out, 1);
    int32_t l = out.data[0] * YM_GAIN_NUM / YM_GAIN_DEN;
    int32_t r = out.data[1] * YM_GAIN_NUM / YM_GAIN_DEN;
    s_scratch[s_scratch_write * 2 + 0] = (int16_t)fm_lpf(0, l);   /* hardware FM output LPF (matches reference; tames ymfm's excess highs) */
    s_scratch[s_scratch_write * 2 + 1] = (int16_t)fm_lpf(1, r);
    s_scratch_write++;
}

} /* namespace */

extern "C" void ym2612_init(void) {
    if (!s_chip)
        s_chip = new ymfm::ym2612(s_iface);
    s_chip->reset();
    s_scratch_write = s_scratch_read = 0;
    s_master_accum = 0;
    s_fm_lpf_prev[0] = s_fm_lpf_prev[1] = 0;
    s_fm_lpf_out[0]  = s_fm_lpf_out[1]  = 0;
    s_inited = 1;
}

extern "C" void ym2612_advance(uint32_t cycles_master) {
    if (!s_inited) ym2612_init();
    if (cycles_master == 0) return;
    s_master_accum += cycles_master;
    while (s_master_accum >= MASTER_PER_SAMPLE) {
        s_master_accum -= MASTER_PER_SAMPLE;
        emit_one_sample();
    }
}

extern "C" void ym2612_write(uint8_t port, uint8_t value) {
    if (!s_inited) ym2612_init();
    /* port 0/1 = part-1 address/data, port 2/3 = part-2 address/data —
     * exactly ymfm::ym2612::write()'s offset convention. */
    if (port <= 3)
        s_chip->write(port, value);
}

extern "C" size_t ym2612_render(int16_t *out, size_t sample_count) {
    if (!out || sample_count == 0) return 0;
    size_t available = s_scratch_write - s_scratch_read;
    size_t copy = sample_count < available ? sample_count : available;
    if (copy > 0) {
        std::memcpy(out, &s_scratch[s_scratch_read * 2], copy * 2 * sizeof(int16_t));
        s_scratch_read += copy;
    }
    if (s_scratch_read >= s_scratch_write)
        s_scratch_read = s_scratch_write = 0;
    return copy;
}

extern "C" size_t ym2612_samples_available(void) {
    return s_scratch_write - s_scratch_read;
}

extern "C" uint32_t ym2612_sample_rate(void) {
    /* ymfm: ym2612 emits clock/144. With YM_CLOCK = 7670453 -> 53267 Hz,
     * matching ym2612.c so audio.c's FM->PSG resampler is unchanged. */
    if (!s_chip) return YM_CLOCK / 144u;
    return s_chip->sample_rate(YM_CLOCK);
}

/* Save-state hooks (own-backend snapshots): full ymfm chip state via
 * ymfm_saved_state (size-prefixed blob; the size is whatever the vendored
 * ymfm version serializes — the format is private to a build), plus this
 * wrapper's sample-clock remainder and output-LPF state. The output scratch
 * ring is transient (drained every frame) and is reset on load. */
extern "C" int ym2612_save_state(FILE *f) {
    if (!s_inited) ym2612_init();
    std::vector<uint8_t> buf;
    ymfm::ymfm_saved_state st(buf, true);
    s_chip->save_restore(st);
    uint32_t n = (uint32_t)buf.size();
    if (fwrite(&n, sizeof n, 1, f) != 1) return 0;
    if (n && fwrite(buf.data(), 1, n, f) != n) return 0;
    if (fwrite(&s_master_accum, sizeof s_master_accum, 1, f) != 1) return 0;
    if (fwrite(s_fm_lpf_prev, sizeof s_fm_lpf_prev, 1, f) != 1) return 0;
    if (fwrite(s_fm_lpf_out,  sizeof s_fm_lpf_out,  1, f) != 1) return 0;
    return 1;
}

#include "cosim.h"
/* Full FM chip state hashed for benchmark regression and differential co-sim.
 * Same surface as
 * ym2612_save_state (the opaque ymfm blob via save_restore + wrapper LPF /
 * sample-clock remainder) but folded into an FNV-1a hash instead of a FILE.
 * save_restore in save-mode is a pure read — side-effect-free on the chip. */
extern "C" uint64_t ym2612_cosim_hash(void) {
    if (!s_inited) ym2612_init();
    std::vector<uint8_t> buf;
    ymfm::ymfm_saved_state st(buf, true);
    s_chip->save_restore(st);
    uint64_t h = cosim_fnv_bytes(cosim_fnv_init(), buf.data(), buf.size());
    h = cosim_fnv_u32(h, s_master_accum);
    h = cosim_fnv_bytes(h, s_fm_lpf_prev, sizeof s_fm_lpf_prev);
    h = cosim_fnv_bytes(h, s_fm_lpf_out,  sizeof s_fm_lpf_out);
    return h;
}

extern "C" int ym2612_load_state(FILE *f) {
    if (!s_inited) ym2612_init();
    uint32_t n = 0;
    if (fread(&n, sizeof n, 1, f) != 1 || n > (1u << 20)) return 0;
    std::vector<uint8_t> buf(n);
    if (n && fread(buf.data(), 1, n, f) != n) return 0;
    ymfm::ymfm_saved_state st(buf, false);
    s_chip->save_restore(st);
    if (fread(&s_master_accum, sizeof s_master_accum, 1, f) != 1) return 0;
    if (fread(s_fm_lpf_prev, sizeof s_fm_lpf_prev, 1, f) != 1) return 0;
    if (fread(s_fm_lpf_out,  sizeof s_fm_lpf_out,  1, f) != 1) return 0;
    s_scratch_read = s_scratch_write = 0;
    return 1;
}
