# Genesis Audio Fidelity Scorecard — 2026-07-05

Replication of the snesrecomp audio exercise (SNES_ACCURACY_BURNDOWN Axis 5 +
issue-#4 sample-drop hunt + snes-cosim audio verdict) on segagenesisrecomp.
Everything here is measured on **clean engine `master` (1dc6efe)** builds made
in dedicated worktrees (`_wt-audio` engine / `_wt-audio-s1|s2|s3k` games),
`-DGEN_DEV_TRACE=ON` (chip rings; WAV-proven zero-perturbation). Native =
own backend (recompiled 68K + clean-room runner + ymfm/SN76489). Oracle =
same runner shell rendering audio via clownmdemu (dev-only `_oracle` exe).
Both dump the SAME tap: generated mix at 223721 Hz, pre-master-volume,
pre-DRC-bridge (`--wav`), so there is no cross-process resample asymmetry and
no rate-label trap (the two systematic-error classes that plagued the SNES
measurements).

Capture recipe (per game):
```
<exe> <rom> --no-launcher --wav out.wav --max-frames 7300 [--framelog f.flog]
            [--snd-dump-frame N] [--input-script jump_run.script]
# TCP (needs debug.ini next to exe): audio_stats / audio_delivery_dump
```
Analyzers: snesrecomp `tools/audio_ab_diff.py` + `audio_spectral_ab.py`
(system-agnostic, used unchanged at --rate 44100), engine `wav_boop_scan.py`,
`wav_band_compare.py`, `_wt-accuracy/tools/audio_drift_diff.py`, plus direct
ordered chip-write-sequence diffs (difflib) from `chip_ring.txt` dumps.

The measurement checklist mirrors snesrecomp:
1. Determinism gate (paired-capture validity)
2. Sample drops / underruns at the delivery layer
3. Command loss at the chip-write surface (SNES: "APU port commands lost")
4. Off-cue: drift ppm + onset-timing match vs oracle
5. Off-tune: pitch
6. Off-tone: timbre (log-spectral distance, third-octave bands)
7. Clicks / crackle (offline scan + live [BOOP] detector)
8. Chip-level synthesis proofs (prior accuracy-spike results, cited)

---

## Sonic 1 — MEASURED (attract 2 min + scripted jump run)

| # | Measurement | Result | Verdict |
|---|---|---|---|
| 1 | Determinism | native WAV bit-identical 3×, oracle 2× (SHA256) | **PASS** |
| 2 | Drops/underruns | `dropped_flushes=0, underrun_flushes=0` both sides, whole run | **PASS** |
| 3 | Command loss | **79,152 consecutive FM writes bit-identical** (boot→demo fork); totals equal (132,744 = 132,744); PSG identical except transition-tick extras (below) | **PASS** (no loss/reorder/dup) |
| 4 | Off-cue | drift −35 ppm (title) / −64 ppm (demo); onsets 96% @ 0.0 ms median (title), 98% @ 0.0 ms (jump run), 93% @ 17 ms (diverged-state demo) | **PASS** |
| 5 | Off-tune | 495.3 Hz vs 495.3 Hz → **+0 cents** (demo window; the per-tool "tuning" line is the dominant-bin polyphony artifact snesrecomp also documented — not citable) | **PASS** |
| 6 | Off-tone | full-run LSD **1.52 dB** (snesrecomp SMW final: 2.7 dB); all 28 third-octave bands within ±1.4 dB; centroid −35 Hz; levels equal (−27.4 vs −27.8 dBFS) | **PASS** |
| 7 | Clicks | 0.00/s both sides (ab_diff), CLEAN (wav_boop_scan, both captures), live [BOOP] silent | **PASS** |
| 8 | Chip synthesis | prior spike: ymfm vs Nuked-OPN2 faithful; PSG ÷2 noise fixed (a858264); pairing#1 cosim: fm/psg/evq bit-exact recomp≡interp | **PASS** (cited) |

Notes: both backends generate exactly 3732 samples/frame (nominal 3733.45 →
both ≈ −390 ppm vs real hardware; shared trait, ≈0.7 cents, not a differential
defect). Oracle wall-clock runs ~2 frames ahead of native for the same music
tick (boot-path difference) — benign, but it is why naive per-vint chip-ring
comparison (chip_stream_diff.py) reports false 0% matches cross-backend; the
ordered-sequence diff is the valid instrument.

### FINDING S1-1 — the jump "boop": drill results (2026-07-05, second session)

The deep drill reframed this finding significantly. Instruments: wall-aligned
frame-ring WRAM diffs, `GENESIS_AUDIO_WRITEDUMP` push-stream capture, oracle
Tier-3 instruction trace (`SONIC_REVERSE_DEBUG` oracle build, driver-PC
filtered), S1 sound-driver disassembly.

**What is now PROVEN:**
1. The boop's byte content is **authentic Sonic 1 driver output, emitted by
   BOTH backends**: the oracle emits the identical `$BF $FF $9F … $8F $0E $90`
   sequence at its own wall time (wf≈1452–57), where the instruction trace
   shows `PlaySoundID → Sound_PlaySpecial` — the **GHZ waterfall special SFX
   ($D0-class)** starting (its tone layer opens at ~468 Hz, full volume, by
   design; `WSnd_PlaySnd` re-queues it every 64 vblanks while a waterfall
   object is on screen). There is no foreign sound and no synthesis defect.
2. The native and oracle **vblank counters are phase-offset by 124 vints** at
   the same wall frame (measured directly at $FFFE0F: native $67 vs oracle
   $E3), accumulated during boot/loads — so vblank-phase-gated game events
   (like the waterfall re-trigger) fire at different wall times per backend.
   This is the runner's known boot-timing residual, not an audio bug.
3. The 68K game code pushes the extra writes itself (push-stream proof:
   scanlines 238–241 inside one V-int), and **the entire SMPS driver RAM
   ($FFF000–$F740, including SFX + special tracks) is byte-identical between
   backends at every frame boundary through the window** — so the divergence
   is carried by something with no RAM footprint at frame boundaries.

**What remains OPEN:** the exact gating mechanism for the native-side
occurrence at wf1298 (its trigger frames don't line up with the plain
`vbl & $3F == 0` arithmetic; candidates: an input read inside the V-int
(`z80_bus_request` / `zDAC_Status` spin at the top of `UpdateMusic`),
vint-per-wall-frame binning, or apply-layer write filtering on the
clownmdemu side). Blocking tooling gap discovered: **native Tier-1 rdb
capture records nothing** (rdb_count=0) even with `SONIC_REVERSE_DEBUG=ON`
and a `--reverse-debug` regen — fixing that hook gives caller-attributed
sound-queue writes and settles the trigger identity in one run.

**Practical read:** every measurable property of the audio pipeline is
faithful; what shifts is *when* an authentic, periodic game sound fires
relative to gameplay, because of the backends' vint-phase divergence. A "fix"
would be runner boot-timing alignment (vint-count parity with an accurate
machine through boot), not an audio-code change — high regression risk
(pacing sensitivity precedent: operand-keyed cycle costs broke S3) for a
phase-only deviation. Recommend: fix the Tier-1 hook first, confirm the
native trigger identity, then decide if vint-phase alignment is worth it.

### FINDING S1-1 (original statement) — the jump "boop", root-caused to the driver write stream

At music/SFX transition ticks the native side's SMPS execution emits an
**extra, premature PSG channel-0 note-on** the oracle never emits:
`$8F $0E $90` = period 0xEF (≈468 Hz) at **attenuation 0 (full volume)**.
It sounds for ~6 frames (~100 ms) until the real SFX PSG part starts
(which is then byte-identical on both sides).

- Systematic: jump-run occurrences at wf 1388 / 1568 / 1688 / 1808 / 2048
  (≈ one per jump/land event) + one at the title→demo transition (wf 494).
- The WAV-side signature matches exactly: `wav_band_compare` flags 5–10×
  native-vs-oracle energy in the 438–849 Hz band for ~1 STFT window at those
  moments (the measured "boop" in the audio itself).
- One longer fork also observed: wf 1452–1537 (~1.4 s) where the two sides
  render entirely different PSG SFX content (diverged SMPS PSG track state),
  then re-converge.
- Localization: this is **upstream of the mixer/synth/output** — the chip
  write stream itself differs. It is also **not the recompiled 68K**
  (pairing #1 proved recomp≡interp bit-exact). The remaining mechanism is a
  **timing-sensitive 68K read** (YM2612 busy/status, HV counter, or Z80
  mailbox readback) that lands differently vs clownmdemu at the SFX-trigger
  tick and forks an SMPS branch — the same mechanism class the cosim campaign
  identified at frame 54 (z80ram/WRAM sound scratch).
- Repro: `s1/jump_run.script`, dump `--snd-dump-frame 1330` (or 2250) on both
  exes, then ordered PSG sequence diff. Regression gate after any fix: the
  difflib PSG diff must show no native-only note-on inserts, and
  `wav_band_compare --auto-align` must show no one-sided 438–849 Hz flags.
- Caveat: clownmdemu is not cycle-perfect; in principle the extra write could
  be the hardware-correct behavior. The ear evidence (user clip A/B: native
  boops, oracle doesn't, real game doesn't) says native is the wrong side.
  BlastEm (cycle-accurate, write-ring instrumented in `_wt-accuracy`) can
  arbitrate if needed.

**Everything else about S1 audio measures faithful.** The complaint surface
reduces to FINDING S1-1.

---

## Sonic 2 — MEASURED (attract 2 min)

| # | Measurement | Result | Verdict |
|---|---|---|---|
| 1 | Determinism | native bit-identical 2×, oracle 2× | **PASS** |
| 2 | Drops/underruns | 0 / 0 both sides | **PASS** |
| 3 | Command loss | 27,127 consecutive FM writes identical from window start (all of SEGA+title); fork at the title→demo transition tick is a write-ORDER difference (voice-load vs DAC bytes around the same key-off), not loss — totals track, PSG differs by 4 window-edge writes | **PASS** |
| 4 | Off-cue | demo window: onsets 97% @ 0.0 ms median, p90 17 ms; drift +573 ppm (26 ms over 45 s, sub-audible; larger than S1's −64 ppm — noted) | **PASS** |
| 5 | Off-tune | LSD 0.3 dB rules out any real pitch shift (the tool's dominant-bin "cents" line is polyphony noise, as on SNES) | **PASS** |
| 6 | Off-tone | LSD **0.3 dB** (demo) / 0.8 dB (title) / **0.88 dB full-run** (CLOSE); centroid ±8 Hz | **PASS** |
| 7 | Clicks | 0.00/s both; wav_boop_scan CLEAN both | **PASS** |
| 8 | Chip synthesis | shared engine — same cited proofs as S1 | **PASS** |

S2 notes: native↔oracle boot offset ≈5–7 frames (global lag +120 ms by the
demo — larger than S1's 2 frames). The transition-tick fork at native wf635 ≡
oracle wf640 is the same divergence family as S1 (driver action ordering vs
the DAC stream at a music-mode change) but produced no premature note-on and
nothing audible in this capture.

## Sonic 3 & Knuckles (combined, Sonic3KRecomp) — MEASURED (attract 2 min)

| # | Measurement | Result | Verdict |
|---|---|---|---|
| 1 | Determinism | native bit-identical 2×; oracle bit-identical 3× **once stale `sonic3k.srm` is removed** (see note) | **PASS** |
| 2 | Drops/underruns | 0 / 0 both sides | **PASS** |
| 3 | Command loss | offset-aligned FM streams: **17,591 consecutive identical writes**; PSG: oracle's entire window stream is a positional prefix of native's (full match) | **PASS** |
| 4 | Off-cue | title: onsets 96% @ 0.0 ms median; demo: 88% @ 17 ms (demo-state divergence, expected) | **PASS** |
| 5 | Off-tune | title dominant 333.8 Hz vs 333.8 Hz → **+0 cents** | **PASS** |
| 6 | Off-tone | LSD 0.5 dB (title) / 0.4 dB (demo) / **1.01 dB full-run** (CLOSE); centroid +0 / −11 Hz | **PASS** |
| 7 | Clicks | 0.00/s both; wav_boop_scan CLEAN both | **PASS** |
| 8 | Chip synthesis | shared engine — same cited proofs | **PASS** |

S3K notes:
- **Stale-state trap re-confirmed:** the oracle exe writes `sonic3k.srm` at
  exit even during an input-less attract run; a subsequent boot with that file
  diverges from wall frame ~61. With the `.srm` deleted before each run the
  oracle reproduces bit-exactly (orc_a = orc_c = orc_d). Follow-up worth
  considering: don't flush SRAM when the game never wrote it.
- FM fork after the 17.6k-write identical run is the same transition-tick
  ordering family as S1/S2 (native begins a note's freq setup where the oracle
  is still mid-DAC-stream), sub-audible in this capture per the WAV metrics.

---

## Overall verdict

**Genesis audio is measured faithful at the same bar the snesrecomp exercise
set — and passes it more cleanly than SNES did** (full-run LSD 0.9–1.5 dB vs
SMW's final 2.7 dB; onset medians 0.0 ms vs 8 ms; zero drops vs a fixed
237k-sample drop bug; zero command loss vs a fixed 30/140 loss bug). Across
all three games: bit-deterministic captures, zero delivery drops/underruns,
zero clicks, equal levels, tempo drift at or below hundreds of ppm
(event-timing, not sample clock), timbre within ~1 dB of the oracle, and
chip write streams identical for tens of thousands of consecutive writes.

The one real, user-audible defect found is **FINDING S1-1** (premature
full-volume PSG note-on at SFX transition ticks — the reported jump "boop"),
now with a digital fingerprint, a scripted reproducer, and a localization:
driver-execution level, caused by a timing-sensitive 68K read, not the
synth/mixer/output. S2/S3K show the same benign transition-tick *ordering*
family but no premature note-on and nothing audible in these captures.

Next steps (in order of value):
1. Drill FINDING S1-1's forking read via the always-on bus ring (native) vs
   oracle_trace (oracle) at a triplet write; fix; regression-gate with the
   jump-pair PSG diff + band compare.
2. Optionally arbitrate "who is right" with the BlastEm write-ring harness.
3. Consider not flushing SRAM from the oracle when the game never wrote it
   (S3K stale-`.srm` trap).

## Instruments built/validated this campaign
- Headless paired-capture recipe (above); WAV tap determinism proven 3×/2×.
- snesrecomp analyzers validated on Genesis WAVs unchanged.
- Ordered chip-write-sequence diff = the valid cross-backend command-loss
  test (vint-keyed chip_stream_diff.py is invalid cross-backend).
- chip_ring capacity note: 262144 events ≈ 330 DAC-heavy frames — place
  `--snd-dump-frame` within ~300 frames of the window of interest.
