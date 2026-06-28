#!/usr/bin/env python3
"""audio_drift_diff.py — drift-tolerant audio comparison for the Genesis accuracy
burndown (Axis 5, FM/PSG). Compares a recomp WAV against a reference WAV (the
oracle: clownmdemu, or Nuked-OPN2 chip replay) WITHOUT assuming sample alignment.

Bit-exact equality is NOT the bar for Genesis FM across independent synth cores
(ymfm vs Nuked vs clownmdemu differ at the LSB even when perceptually identical;
see GENESIS_ACCURACY_BURNDOWN.md "audio metric verdict"). Instead we report three
drift-tolerant metrics, each of which neutralizes a constant lead/lag the way the
cycle axis neutralizes a cycle offset with per-anchor deltas:

  1. CROSS-CORRELATION ALIGNMENT  — global best lag (max normalized xcorr) +
     post-alignment residual; then per-window local lag to expose timing DRIFT.
  2. ONSET-TIMING HISTOGRAM        — detect note/SFX onsets on both streams via
     spectral flux, match nearest onsets, histogram the onset-time deltas
     (tight zero-centered = good timing; shifted/bimodal = the defect signature).
  3. PER-NOTE PITCH ERROR (cents)  — windowed fundamental estimate on both,
     report cents error where a clear pitch exists (catches divider/LFSR/sweep
     math bugs that energy-only metrics hide).

All comparisons best-fit a single gain first (level is a per-core calibration
constant, not an accuracy signal). Resamples to a common rate if the two WAVs
differ. numpy + scipy only.

Usage:
  audio_drift_diff.py OURS.wav REF.wav [--label-a ours --label-b ref]
                      [--window-ms 500] [--onset-tol-ms 30] [--json out.json]
"""
import sys, json, argparse, wave
import numpy as np

try:
    from scipy.signal import resample_poly, stft
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


def load_wav_mono(path):
    with wave.open(path, "rb") as w:
        nch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw != 2:
        raise SystemExit(f"{path}: only 16-bit PCM supported (got {sw*8}-bit)")
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if nch > 1:
        a = a.reshape(-1, nch).mean(axis=1)   # downmix to mono
    return a, rate


def to_common_rate(a, ra, b, rb):
    """Resample the higher-rate signal down to the lower common rate."""
    target = min(ra, rb)
    def rs(x, r):
        if r == target:
            return x
        if HAVE_SCIPY:
            from math import gcd
            g = gcd(int(r), int(target))
            return resample_poly(x, int(target)//g, int(r)//g)
        # numpy fallback: linear resample
        n_out = int(round(len(x) * target / r))
        return np.interp(np.linspace(0, len(x), n_out, endpoint=False),
                         np.arange(len(x)), x)
    return rs(a, ra), rs(b, rb), target


def best_gain(a, b):
    """Least-squares scalar gain g minimizing |a - g*b| (level calibration)."""
    denom = float(np.dot(b, b))
    return float(np.dot(a, b) / denom) if denom > 0 else 1.0


def xcorr_best_lag(a, b, max_lag):
    """Global best lag (samples) maximizing normalized cross-correlation, via FFT.
    Positive lag => b lags a (b must shift later to align)."""
    n = len(a) + len(b)
    nfft = 1 << (n - 1).bit_length()
    fa = np.fft.rfft(a, nfft)
    fb = np.fft.rfft(b, nfft)
    cc = np.fft.irfft(fa * np.conj(fb), nfft)
    # cc[k] = sum a[i]*b[i-k]; arrange lags [-max_lag, +max_lag]
    cc = np.concatenate((cc[-max_lag:], cc[:max_lag + 1]))
    lags = np.arange(-max_lag, max_lag + 1)
    norm = np.sqrt(float(np.dot(a, a)) * float(np.dot(b, b))) + 1e-9
    k = int(np.argmax(cc))
    return int(lags[k]), float(cc[k] / norm)


def aligned_residual(a, b, lag):
    """Residual RMS (after best-fit gain) with b shifted by `lag`, / ref RMS."""
    if lag > 0:
        a2, b2 = a[lag:], b[:len(b) - lag]
    elif lag < 0:
        a2, b2 = a[:len(a) + lag], b[-lag:]
    else:
        a2, b2 = a, b
    n = min(len(a2), len(b2))
    a2, b2 = a2[:n], b2[:n]
    g = best_gain(a2, b2)
    resid = a2 - g * b2
    rms_ref = np.sqrt(np.mean(a2 ** 2)) + 1e-9
    return float(np.sqrt(np.mean(resid ** 2)) / rms_ref), g, n


def windowed_drift(a, b, rate, win, max_lag):
    """Per-window local best lag — exposes timing drift over the clip."""
    rows = []
    for s in range(0, min(len(a), len(b)) - win, win):
        aw, bw = a[s:s + win], b[s:s + win]
        if np.sqrt(np.mean(aw ** 2)) < 30:   # skip near-silence
            continue
        lag, c = xcorr_best_lag(aw, bw, max_lag)
        res, _, _ = aligned_residual(aw, bw, lag)
        rows.append((s / rate, lag * 1000.0 / rate, c, res))
    return rows


def spectral_flux_onsets(x, rate, hop=256, win=1024, thresh_k=1.5):
    """Onset times (s) via half-wave-rectified spectral flux + adaptive peak pick."""
    if HAVE_SCIPY:
        f, t, Z = stft(x, fs=rate, nperseg=win, noverlap=win - hop, boundary=None)
        mag = np.abs(Z)
    else:
        nfr = 1 + (len(x) - win) // hop
        mag = np.empty((win // 2 + 1, max(nfr, 1)))
        wnd = np.hanning(win)
        for i in range(nfr):
            mag[:, i] = np.abs(np.fft.rfft(x[i*hop:i*hop+win] * wnd))
        t = np.arange(mag.shape[1]) * hop / rate
    flux = np.maximum(0.0, np.diff(mag, axis=1)).sum(axis=0)
    if flux.size == 0:
        return np.array([])
    flux = flux / (flux.max() + 1e-9)
    # adaptive threshold = local mean + k*std over a ~0.2 s window
    w = max(3, int(0.2 * rate / hop))
    onsets = []
    for i in range(1, len(flux) - 1):
        lo, hi = max(0, i - w), min(len(flux), i + w)
        loc = flux[lo:hi]
        thr = loc.mean() + thresh_k * loc.std()
        if flux[i] > thr and flux[i] >= flux[i-1] and flux[i] > flux[i+1]:
            onsets.append(t[i + 1])   # +1: diff offset
    return np.array(onsets)


def onset_histogram(oa, ob, tol):
    """Match each A onset to nearest B onset within tol (s); return delta stats."""
    if len(oa) == 0 or len(ob) == 0:
        return None
    deltas, matched = [], 0
    for ta in oa:
        j = int(np.argmin(np.abs(ob - ta)))
        d = ta - ob[j]
        if abs(d) <= tol:
            deltas.append(d * 1000.0)   # ms
            matched += 1
    deltas = np.array(deltas)
    return {
        "onsets_a": int(len(oa)), "onsets_b": int(len(ob)),
        "matched": matched,
        "match_rate": round(matched / len(oa), 3),
        "mean_ms": round(float(deltas.mean()), 2) if len(deltas) else None,
        "median_ms": round(float(np.median(deltas)), 2) if len(deltas) else None,
        "std_ms": round(float(deltas.std()), 2) if len(deltas) else None,
        "p90_abs_ms": round(float(np.percentile(np.abs(deltas), 90)), 2) if len(deltas) else None,
    }


def _fft_autocorr(x):
    """Unbiased-ish autocorrelation via FFT (O(n log n))."""
    n = len(x)
    nfft = 1 << (2 * n - 1).bit_length()
    f = np.fft.rfft(x, nfft)
    ac = np.fft.irfft(f * np.conj(f), nfft)[:n]
    return ac


def yin_pitch(x, rate, fmin=60, fmax=4000):
    """Fundamental estimate (Hz) for a short frame via FFT-autocorr CMNDF (YIN).
    Vectorized cumulative-mean step so it stays O(n log n)."""
    x = x - x.mean()
    if np.sqrt(np.mean(x ** 2)) < 30:
        return None
    n = len(x)
    tau_max = min(int(rate / fmin), n - 1)
    tau_min = max(1, int(rate / fmax))
    if tau_max <= tau_min + 1:
        return None
    ac = _fft_autocorr(x)
    d = ac[0] + ac[0] - 2 * ac          # difference function d(tau)=2*(ac0-ac[tau])
    d = d[:tau_max + 1]
    run = np.cumsum(d[1:])              # vectorized cumulative mean normalization
    taus = np.arange(1, len(d))
    cmnd = np.empty_like(d)
    cmnd[0] = 1.0
    cmnd[1:] = d[1:] * taus / (run + 1e-9)
    tau = tau_min
    while tau < len(cmnd) - 1:
        if cmnd[tau] < 0.15 and cmnd[tau] <= cmnd[tau + 1]:
            break
        tau += 1
    else:
        tau = int(np.argmin(cmnd[tau_min:]) + tau_min)
    if cmnd[tau] > 0.5:
        return None
    # parabolic interpolation around the minimum for sub-sample tau
    if 1 < tau < len(cmnd) - 1:
        y0, y1, y2 = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        denom = (y0 + y2 - 2 * y1)
        if denom != 0:
            tau = tau + 0.5 * (y0 - y2) / denom
    return rate / tau


def pitch_error_cents(a, b, rate, win, hop):
    """Per-frame cents error where BOTH streams have a clear pitch. Uses a short
    ~46 ms analysis frame (independent of the coarse correlation window) so the
    FFT-autocorr stays cheap and pitch is local."""
    pf = 1 << int(np.ceil(np.log2(0.046 * rate)))   # ~46 ms, power of two
    ph = pf // 2
    cents = []
    for s in range(0, min(len(a), len(b)) - pf, ph):
        fa = yin_pitch(a[s:s + pf], rate)
        fb = yin_pitch(b[s:s + pf], rate)
        if fa and fb:
            cents.append(1200.0 * np.log2(fa / fb))
    if not cents:
        return None
    c = np.array(cents)
    return {
        "frames_voiced": int(len(c)),
        "mean_cents": round(float(c.mean()), 2),
        "median_abs_cents": round(float(np.median(np.abs(c))), 2),
        "p90_abs_cents": round(float(np.percentile(np.abs(c), 90)), 2),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav_a"); ap.add_argument("wav_b")
    ap.add_argument("--label-a", default="ours"); ap.add_argument("--label-b", default="ref")
    ap.add_argument("--window-ms", type=float, default=500)
    ap.add_argument("--onset-tol-ms", type=float, default=30)
    ap.add_argument("--max-lag-ms", type=float, default=50)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    a, ra = load_wav_mono(args.wav_a)
    b, rb = load_wav_mono(args.wav_b)
    a, b, rate = to_common_rate(a, ra, b, rb)
    win = int(args.window_ms * rate / 1000)
    max_lag = int(args.max_lag_ms * rate / 1000)

    glag, gcorr = xcorr_best_lag(a, b, max_lag)
    gres, ggain, gn = aligned_residual(a, b, glag)
    drift = windowed_drift(a, b, rate, win, max_lag)
    oa = spectral_flux_onsets(a, rate)
    ob = spectral_flux_onsets(b, rate)
    onset = onset_histogram(oa, ob, args.onset_tol_ms / 1000.0)
    pitch = pitch_error_cents(a, b, rate, win, win // 2)

    la, lb = args.label_a, args.label_b
    print(f"== drift-tolerant audio diff: {la} vs {lb} ==")
    print(f"  rate={rate} Hz  dur={min(len(a),len(b))/rate:.2f}s  (best-fit gain {la}/{lb} = {ggain:.3f})")
    print(f"\n[1] cross-correlation alignment")
    print(f"    global best lag = {glag*1000/rate:+.2f} ms   peak xcorr = {gcorr:.3f}")
    print(f"    post-align residual = {gres*100:.1f}% of {la} RMS   (0% = identical after gain+lag)")
    if drift:
        lags = np.array([d[1] for d in drift]); cors = np.array([d[2] for d in drift])
        res = np.array([d[3] for d in drift])
        print(f"    per-window ({len(drift)} win): lag {lags.mean():+.2f}±{lags.std():.2f} ms "
              f"(drift span {lags.max()-lags.min():.2f} ms) | xcorr {cors.mean():.3f} | residual {res.mean()*100:.1f}%")
    print(f"\n[2] onset-timing histogram (tol ±{args.onset_tol_ms:.0f} ms)")
    if onset:
        print(f"    onsets {la}={onset['onsets_a']} {lb}={onset['onsets_b']} | matched {onset['matched']} "
              f"({onset['match_rate']*100:.0f}%)")
        print(f"    delta ({la}-{lb}): median {onset['median_ms']} ms  mean {onset['mean_ms']} ms  "
              f"std {onset['std_ms']} ms  p90|.| {onset['p90_abs_ms']} ms")
    else:
        print("    (insufficient onsets)")
    print(f"\n[3] per-note pitch error (cents)")
    if pitch:
        print(f"    voiced frames={pitch['frames_voiced']} | median|err| {pitch['median_abs_cents']} cents "
              f"| p90 {pitch['p90_abs_cents']} | mean {pitch['mean_cents']}")
    else:
        print("    (no clearly-voiced frames in both)")

    if args.json:
        out = {"a": la, "b": lb, "rate": rate,
               "duration_s": round(min(len(a), len(b)) / rate, 3),
               "gain_a_over_b": round(ggain, 4),
               "xcorr": {"global_lag_ms": round(glag*1000/rate, 3), "peak": round(gcorr, 4),
                         "residual_pct": round(gres*100, 2),
                         "windows": [{"t": round(d[0], 3), "lag_ms": round(d[1], 3),
                                      "xcorr": round(d[2], 4), "residual_pct": round(d[3]*100, 2)} for d in drift]},
               "onset": onset, "pitch": pitch}
        with open(args.json, "w") as f:
            json.dump(out, f, indent=2)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
