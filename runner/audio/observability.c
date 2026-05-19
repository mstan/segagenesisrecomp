/*
 * observability.c — boop detector implementation.
 *
 * Per wall frame, computes derivative statistics on the rendered sample
 * stream:
 *   peak |d/dt| : largest single-sample step (click signature)
 *   rms  |d/dt| : overall transient energy
 *
 * Flags frames that exceed:
 *   - a multiplier over the rolling median of recent "quiet" frames, OR
 *   - an absolute peak threshold (hard click)
 *
 * Mono PSG and stereo FM are tracked separately since their clocks and
 * amplitude ranges differ. The mixed output is reconstructed later by
 * paired capture if needed.
 */
#include "observability.h"
#include <stdlib.h>
#include <stdio.h>

/* Per-frame accumulators */
static int32_t  s_fm_peak_delta = 0;
static uint64_t s_fm_sum_sq_delta = 0;
static uint32_t s_fm_sample_count = 0;
static int16_t  s_fm_prev_l = 0;
static int16_t  s_fm_prev_r = 0;
static int      s_fm_first = 1;

static int32_t  s_psg_peak_delta = 0;
static uint64_t s_psg_sum_sq_delta = 0;
static uint32_t s_psg_sample_count = 0;
static int16_t  s_psg_prev = 0;
static int      s_psg_first = 1;

/* Rolling baseline over the last N frames — used to detect spikes above
 * "normal" level. Simple ring of recent peak deltas. */
#define OBS_BASELINE_WIN 120   /* 2 seconds */
static int32_t  s_fm_peaks [OBS_BASELINE_WIN];
static int32_t  s_psg_peaks[OBS_BASELINE_WIN];
static int      s_win_head = 0;
static int      s_win_filled = 0;

static int s_enabled = 1;

void audio_obs_init(void)
{
    s_fm_peak_delta = s_psg_peak_delta = 0;
    s_fm_sum_sq_delta = s_psg_sum_sq_delta = 0;
    s_fm_sample_count = s_psg_sample_count = 0;
    s_fm_first = s_psg_first = 1;
    s_win_head = s_win_filled = 0;
    for (int i = 0; i < OBS_BASELINE_WIN; i++) {
        s_fm_peaks[i] = s_psg_peaks[i] = 0;
    }
}

void audio_obs_set_enabled(int on) { s_enabled = on; }

void audio_obs_ingest_fm(const int16_t *stereo, size_t frames)
{
    if (!s_enabled || !stereo) return;
    for (size_t i = 0; i < frames; i++) {
        int16_t l = stereo[i * 2 + 0];
        int16_t r = stereo[i * 2 + 1];
        if (!s_fm_first) {
            int32_t dl = (int32_t)l - (int32_t)s_fm_prev_l;
            int32_t dr = (int32_t)r - (int32_t)s_fm_prev_r;
            if (dl < 0) dl = -dl;
            if (dr < 0) dr = -dr;
            int32_t d = dl > dr ? dl : dr;
            if (d > s_fm_peak_delta) s_fm_peak_delta = d;
            s_fm_sum_sq_delta += (uint64_t)d * (uint64_t)d;
            s_fm_sample_count++;
        }
        s_fm_prev_l = l; s_fm_prev_r = r; s_fm_first = 0;
    }
}

void audio_obs_ingest_psg(const int16_t *mono, size_t frames)
{
    if (!s_enabled || !mono) return;
    for (size_t i = 0; i < frames; i++) {
        int16_t s = mono[i];
        if (!s_psg_first) {
            int32_t d = (int32_t)s - (int32_t)s_psg_prev;
            if (d < 0) d = -d;
            if (d > s_psg_peak_delta) s_psg_peak_delta = d;
            s_psg_sum_sq_delta += (uint64_t)d * (uint64_t)d;
            s_psg_sample_count++;
        }
        s_psg_prev = s; s_psg_first = 0;
    }
}

/* Return median of the baseline window's peak values. O(N log N) sort
 * each frame — 120 elements, negligible cost. */
static int32_t rolling_median(int32_t *peaks)
{
    if (s_win_filled == 0) return 0;
    int n = s_win_filled;
    int32_t tmp[OBS_BASELINE_WIN];
    for (int i = 0; i < n; i++) tmp[i] = peaks[i];
    /* Tiny insertion sort */
    for (int i = 1; i < n; i++) {
        int32_t v = tmp[i]; int j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = v;
    }
    return tmp[n / 2];
}

void audio_obs_tick_frame(uint64_t frame_num,
                           uint32_t internal_frame,
                           uint8_t  game_mode)
{
    if (!s_enabled) { s_fm_peak_delta = s_psg_peak_delta = 0;
                      s_fm_sum_sq_delta = s_psg_sum_sq_delta = 0;
                      s_fm_sample_count = s_psg_sample_count = 0;
                      return; }

    int32_t fm_peak  = s_fm_peak_delta;
    int32_t psg_peak = s_psg_peak_delta;
    uint64_t fm_rms_sq  = s_fm_sample_count  ? s_fm_sum_sq_delta  / s_fm_sample_count  : 0;
    uint64_t psg_rms_sq = s_psg_sample_count ? s_psg_sum_sq_delta / s_psg_sample_count : 0;

    int32_t fm_median  = rolling_median(s_fm_peaks);
    int32_t psg_median = rolling_median(s_psg_peaks);

    /* Only flag once we've accumulated enough baseline to reason about
     * "unusual" vs "normal" for this content. Up to ~2 seconds of warmup
     * by which point envelope attacks have populated the median. */
    int have_baseline = s_win_filled >= (OBS_BASELINE_WIN / 2);

    /* Two criteria for a likely boop:
     *   A) Peak is much higher than recent median AND absolutely loud
     *   B) RMS derivative (sustained transient energy over many samples)
     *      dwarfs baseline — indicates garbled/buzzy audio, not a single
     *      attack.
     * The absolute floors are tuned to ignore normal note attacks which
     * show fm_peak up to ~8000 on clean runs. */
    #define ABS_PEAK_HARD 20000
    int fm_anom = have_baseline && (
        (fm_peak > fm_median * 8 && fm_peak > 10000)
        || fm_peak > ABS_PEAK_HARD);
    int psg_anom = have_baseline && (
        (psg_peak > psg_median * 8 && psg_peak > 10000)
        || psg_peak > ABS_PEAK_HARD);

    if (fm_anom || psg_anom) {
        fprintf(stderr, "[BOOP] frame=%llu gmode=0x%02X intframe=%u  fm_peak=%d (med=%d)  psg_peak=%d (med=%d)  fm_rms^2=%llu  psg_rms^2=%llu\n",
                (unsigned long long)frame_num, (unsigned)game_mode, (unsigned)internal_frame,
                (int)fm_peak,  (int)fm_median,
                (int)psg_peak, (int)psg_median,
                (unsigned long long)fm_rms_sq, (unsigned long long)psg_rms_sq);
    }

    /* Roll the baseline. */
    s_fm_peaks [s_win_head] = fm_peak;
    s_psg_peaks[s_win_head] = psg_peak;
    s_win_head = (s_win_head + 1) % OBS_BASELINE_WIN;
    if (s_win_filled < OBS_BASELINE_WIN) s_win_filled++;

    /* Reset per-frame accumulators. */
    s_fm_peak_delta = s_psg_peak_delta = 0;
    s_fm_sum_sq_delta = s_psg_sum_sq_delta = 0;
    s_fm_sample_count = s_psg_sample_count = 0;
}
