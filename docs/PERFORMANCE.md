# Genesis performance burn-down

The goal is uncapped headroom on lower-power hosts, including the original
Xbox, without weakening the shared Genesis hardware model or making behavior
title-specific. A paced 59.94 FPS run is a correctness check, not a performance
measurement.

## Measurement contract

- Use Release builds from the same compiler and source revision.
- Compile at BelowNormal priority with one build job on this machine.
- Use `tools/paired_benchmark.py --workload runtime` for gameplay CPU-work
  comparisons. It selects `--measure-runtime N --runtime-uncapped`, retaining
  synthesis, mixing, device pushes, presentation and rewind while removing
  pacing. Mute unattended runs with a dummy output device. Use a paced run
  separately to check delivery, since uncapped output can overflow a real-time
  audio queue. `--benchmark N ROM` remains useful for core attribution, but
  disables audio-device delivery, presentation and rewind; it cannot establish
  normal gameplay throughput by itself.
- Warm both binaries, then use order-balanced A/B pairs over a workload long
  enough to reach attract/gameplay. Report paired deltas and raw results.
- Run an identical-binary A/A control first. Stop timing when it fails the
  declared noise gate; retain the failed control. A new quiet interval or a
  justified protocol change must be recorded before another attempt.
- Run both sides of a pair on the same fixed logical CPU. Ordinary work on
  other cores is acceptable; reject the measurement when paired deltas spread
  by more than 3 percentage points (or when broader thermal/memory pressure
  makes the pinned results unstable). `tools/paired_benchmark.py` enforces
  affinity, balanced order, title identity, and this variance gate. Its
  per-side environment overrides also permit same-binary feature isolation
  without introducing a code-layout difference.
- Use exactly one title per A/B experiment. Rotate representative experiments
  across Sonic 1, Sonic 2, Sonic 3 & Knuckles, and Rocket Knight Adventures;
  never compare raw throughput between different games.
- Measure native and widescreen configurations separately.
- Pin framebuffer/state hashes, dispatch/interpreter misses, and strict guest
  stack invariants before retaining a candidate.
- Report executable/text/data/BSS changes as well as throughput. Code growth
  matters on the Xbox.

## Correctness prerequisite

The initial Sonic 1 build was already incorrect before performance changes:
terrain could be absent on the first life and reappear after death. The
recompiler's split-function guest-stack counter was global. A scheduler-run
interrupt handler could overwrite it while the game fiber was suspended in
the cold-load decompressor, causing a balanced 13-register restore to be
mistaken for consumed return slots and aborting level initialization.

The fix isolates split-stack state across V-int/H-int re-entry and handles a
nested skip-return that has already consumed its caller's JSR slot. Strict
stack validation passes the former deterministic failure point, and first-life
Green Hill Zone was visually confirmed fixed before performance work resumed.

## Benchmark baseline

`--benchmark` was added because the older `--turbo` path still called a
VSync-backed `SDL_RenderPresent` and therefore was not reliably uncapped.
A 300-frame smoke test reported `vsync=0`, completed with zero dispatch misses,
and produced:

```text
GENESISRECOMP_BENCHMARK {"game":"Sonic1","frames":300,"seconds":0.546607200,"fps":548.840,"ms_per_frame":1.822024}
```

The initial warmed 6,000-frame Sonic 1 baseline samples were 362.193, 368.031,
382.239, 352.423, and 372.899 FPS (median 368.031 FPS). Each sample represents
about 100 seconds of guest time and reaches attract/gameplay.

## Retained experiments

### Adjacent VDP plane-fetch caching

Earlier Sonic 1 CPU sampling attributed about 72% of its workload to
`gvdp_render_scanline`. The retained change inlines the hot VDP helpers and
caches the name-table and pattern-row fetches shared by adjacent pixels. It
does not cull scanlines, DMA, sprites, planes, or widescreen work.

- Sonic 1 retained roughly **+10.5%** across order-reversed paired runs.
- Sonic 2 retained **+9.755%** median across order-reversed 6,000-frame
  gameplay pairs (**+8.896%**, **+10.615%**); the 1.719 percentage-point
  spread passed the 3-point acceptance gate despite unrelated work on another
  core.
- Sonic 3 & Knuckles was positive in both pairs (**+5.685%** and
  **+9.849%**), but the 4.164 percentage-point spread exceeded the 3-point
  acceptance gate. This corroborates direction only; it is not an accepted
  precise effect size.
- Executable growth was 970 bytes for Sonic 1, 1,482 bytes for Sonic 2,
  970 bytes for Sonic 3 & Knuckles, and 1,482 bytes for Rocket Knight
  Adventures.

Timing remains paused whenever the paired-variance gate fails. Deterministic
correctness comparisons do not depend on idle-machine timing and continue
independently.

### Binary-search generated tail dispatch

Fresh Sonic 3 & Knuckles leaf sampling exposed a title-scaling bottleneck
hidden by Sonic 1's much smaller generated program. Its 30,476-entry function
table was scanned linearly on every recompiled tail call. Of 328,636 samples
inside the executable, 71.401% landed in `recomp_drain_tailcalls` and 19.999%
in `gvdp_render_scanline`.

The SCC68070 generator already used binary search for the same sorted-table
problem. Porting that lookup to Genesis reduces an average successful lookup
from roughly 15,000 address comparisons to about 15, while preserving RAM
trampoline/stub resolution, overrides, miss logging, and tail-frame behavior.
The generated table's strict sort order was verified for both Sonic 1 (2,080
entries) and Sonic 3 & Knuckles (30,476 entries).

- Sonic 1's clean dispatch-only pairs were **+2.168%** and **+0.162%**
  (median **+1.165%**, 2.006-point spread): a small accepted win for the small
  table.
- Sonic 3 & Knuckles' 12,000-frame pairs were **+256.150%** and
  **+241.544%**. Baseline and candidate CVs were only 1.060% and 1.034%, but
  ratio amplification produced a 14.606-point spread, so the exact median is
  not accepted under the 3-point gate. The conservative retained claim is
  that both orderings exceeded **+241%**.
- The lookup changes release `.text` by +28 bytes in Sonic 1 and +64 bytes in
  Sonic 3 & Knuckles. The S3K executable file was 101 bytes smaller.

Post-change S3K sampling confirms attribution moved as intended:
`recomp_drain_tailcalls` fell to 2.495% of 91,445 in-executable samples, while
`gvdp_render_scanline` became the new dominant ceiling at 70.969%.

### Scanline attribution

Fresh post-dispatch Sonic 2 sampling collected 42,033 in-executable samples.
`gvdp_render_scanline` accounted for 70.537%. Within that renderer:

- `fetch_plane_pixel` accounted for 65.321%.
- The non-inlined `gvdp_render_scanline` body accounted for 26.659%.
- Inlined `vram_read_word` work accounted for 4.253%.
- `sprite_render_line` accounted for 3.730%.

Line attribution inside `fetch_plane_pixel` put the hottest samples on packed
nibble extraction, the pattern-byte load, pattern-address calculation, and
name-table address calculation. This is enough to direct shared plane-fetch
work, but window, scrolling, palette, DMA, and widescreen-policy costs still
need independent buckets before the broader attribution item is complete.

After consecutive-pixel attribute reuse landed, a fresh 18,000-frame Sonic 3
& Knuckles sample attributed all 29,003 executable leaves. The VDP scanline
renderer still dominated at 19,582 samples (**67.517%**); the next named
functions were YMFM clocking at 4.041%, tail dispatch at 3.007%, Z80 opcode
execution at 2.941%, and frame scheduling at 2.920%.

For line attribution, the optimized VDP source was rebuilt with debug lines
and its `.text` was verified byte-identical to the sampled object
(`1022F406...8585`). Within `gvdp_render_scanline`, the largest individual
source lines were packed-nibble extraction (10.581%), palette-index output
(7.149%), tile/pattern-row address construction (6.475%), adjacency-cache
state (6.281%), and vertical flip (5.745%). These percentages are of scanline
samples, not whole-process samples.

### Scanline-invariant VDP state

Window boundaries, full-screen vertical scroll values, and shadow/highlight
mode are fixed while a completed scanline is rendered. Resolving them once per
row removes redundant register/VSRAM loads and window arithmetic from the
per-pixel loop without skipping any pixels or changing two-cell vertical
scroll behavior.

- Sonic 2's order-reversed 6,000-frame gameplay pairs were **+0.710%** and
  **+0.801%** (median **+0.756%**, 0.091-point spread).
- Baseline and candidate CVs were 0.216% and 0.261%; the result passed the
  3-point acceptance gate while pinned to logical CPU 15.
- Release `.text` grew by 64 bytes; executable file size was unchanged.

### Remove the adjacent pattern-byte cache

The retained plane-fetch cache originally memoized both the name-table
attribute and the packed pattern byte shared by two adjacent pixels. Sampling
showed that the extra pattern-address comparison, branch, and cache state cost
more than directly loading the already-hot VRAM byte. The name-table cache is
still retained; only the two-pixel pattern-byte memoization is removed.

- Sonic 1's order-reversed 12,000-frame attract pairs were **+7.984%** and
  **+8.533%** (median **+8.259%**, 0.548-point spread) on logical CPU 11.
- Baseline and candidate CVs were 0.376% and 0.629%; the result passed the
  3-point acceptance gate.
- Sonic 2 was positive in four isolated orderings (**+3.568%**, **+7.301%**,
  **+13.693%**, and **+8.509%**), but both two-pair runs exceeded the
  3-point spread gate. These runs corroborate direction only; Sonic 1 supplies
  the accepted effect size.
- Release `.text` shrank by 448 bytes in Sonic 1, Sonic 2, Sonic 3 & Knuckles,
  and Rocket Knight Adventures. Executable files shrank by 512 bytes where PE
  section alignment permitted it; Sonic 3 & Knuckles' file size was unchanged.

### Consecutive-pixel plane-attribute reuse

The retained name-table cache used to recompute the name-table address for
every pixel and compare that address to the previous fetch. The renderer now
records only the next packed `(y, x)` coordinate that can reuse the attribute.
Reuse is limited to the immediately adjacent pixel within the same 8-pixel
cell; tile boundaries explicitly invalidate it, and scroll/window/row
discontinuities naturally miss.

- Sonic 1's order-reversed 18,000-frame gameplay pairs were **+5.550%** and
  **+5.636%** (median **+5.593%**, 0.085-point spread) on logical CPU 11.
- A same-binary calibration immediately before the retained run passed with a
  0.687-point spread. Absolute throughput moved with unrelated host activity,
  but the paired candidate/control ratios remained consistent.
- Release `.text`, data, BSS, and executable size were unchanged.
- Sonic 1 matched all 100/100 framebuffer checkpoints in both native and
  widescreen gameplay routes.

## Differential validation

All comparisons below used the same title revision and input on the
pre-change and candidate engines:

- Sonic 1 native: 100/100 framebuffer hashes and the final RAM snapshot were
  exact over 6,000 frames; strict dispatch/stack checks were clean.
- Sonic 1 widescreen Green Hill gameplay: 62/62 448x224 framebuffer hashes
  were exact through frame 3,742; strict checks were clean. The same 62/62
  route was exact again for the binary-dispatch candidate versus its
  current-core linear control, then again for the scanline-invariant
  candidate versus the pre-change binary-dispatch build.
  The pattern-byte-cache removal additionally matched 100/100 hashes over the
  6,000-frame active Green Hill route at both native width and widescreen.
- Sonic 2: the strict 63-checkpoint, 3,780-frame golden route was exact.
  Attract added 200/200 exact hashes and 20/20 exact PNGs through frame
  12,001. Active Emerald Hill fuzz added 67/67 exact hashes and an exact final
  PNG at native width, then another 67/67 exact hashes and exact PNG at
  448x224 widescreen resolution. The binary-dispatch candidate repeated both
  67/67 active-gameplay comparisons, including the native and 448x224 PNGs,
  against the linear control. The scanline-invariant candidate also repeated
  67/67 exact hashes and the exact final PNG at both native width and 448x224,
  with no bad diagnostics.
  The pattern-byte-cache removal additionally matched 100/100 hashes over the
  6,000-frame active Emerald Hill route at both native width and widescreen.
- Sonic 3 & Knuckles attract: 20/20 hashes and 19/19 PNGs were byte-identical.
  Gameplay fuzz added 97/97 exact hashes and 3/3 exact PNGs through frame
  5,856, with identical SRAM. The binary-dispatch candidate repeated the
  97/97, 3/3, and SRAM exact comparison against the linear control. The
  scanline-invariant candidate repeated all 97 hashes, three PNGs, and final
  SRAM exactly against the pre-change binary-dispatch build.
  The pattern-byte-cache removal repeated all 97 gameplay-fuzz hashes at both
  native width and widescreen.
- Rocket Knight Adventures attract: 200/200 hashes and 20/20 PNGs were exact.
  The longer input route added 280/280 exact hashes and 10/10 exact PNGs.
  The binary-dispatch candidate repeated the longer 280/280 and 10/10 exact
  comparison against the linear control with no bad diagnostics. The
  scanline-invariant candidate repeated the same 280/280 and 10/10 exact
  comparison against the pre-change build.
  The pattern-byte-cache removal repeated all 280 hashes exactly.
  That older comparison predated the RKA ROM/corruption fix: both builds
  reached the same white/static screen after the Konami logo, so it proved
  regression neutrality only. The corrected gameplay route below supersedes
  it as the current release gate.

The consecutive-pixel plane-attribute candidate repeated the public gate
against same-revision controls:

- Sonic 1 and Sonic 2 each matched 200/200 attract hashes plus 100/100
  gameplay hashes at both native width and widescreen.
- Sonic 3 & Knuckles matched 200/200 attract hashes plus 97/97 gameplay-fuzz
  hashes at both native width and widescreen.
- Rocket Knight Adventures matched 280/280 hashes over the corrected build's
  combined full-attract and gameplay-fuzz route.
- The 6,000-frame benchmark workloads also matched the full architectural and
  audio-state fingerprints for all four titles.

The stored pre-candidate S3K baseline differed only during an early menu
transition, and the stored RKA baseline contained the old broken build's
constant static-screen hash. Fresh controls built from the candidate's exact
parent revision matched the candidate, so those obsolete baselines were not
used as evidence of a renderer regression.

Puyo Puyo is excluded: neither the authenticated repository inventory nor
public-remote search found a title repository. Its engine-local bring-up is
not a reproducible public regression target.

For the pattern-cache gate, `zone_smoke.py --benchmark --max-frames N` ran
these routes through the finite uncapped path. Version 2 of the smoke snapshot
also records and compares interpreter call/miss totals and any strict JSR-stack
mismatch lines. All seven native/widescreen comparisons matched diagnostics
and reported no stack mismatches.

### Benchmark end-state fingerprints

Finite benchmark records now include a pointer-free full architectural-state
FNV-1a hash plus an audio-state hash that folds the complete YM2612, SN76489,
and pending cycle-stamped event-queue state. The existing differential co-sim
hasher supplies both values, avoiding a second serialization surface.

The benchmark timer stops before hashing, so correctness metadata does not
reduce the reported throughput. `paired_benchmark.py` requires both hashes and
rejects a pair on any mismatch; `--allow-missing-hashes` is available only for
deliberate comparisons against historical binaries. Two independent
6,000-frame proof runs per public title produced identical full-state and audio
hashes:

- Sonic 1: state `B73909CF3B280638`, audio `5F095FF7D29387A5`.
- Sonic 2: state `E10EAA6208809C77`, audio `429C24252EAC6445`.
- Sonic 3 & Knuckles: state `A264222BFEA48B7B`, audio
  `0C1CC3CCE3BEE875`.
- Rocket Knight Adventures: state `A992AD2156377560`, audio
  `87031C3CDFDD70C9`.

The Sonic proofs also retained identical interpreter totals and zero true/raw
misses.

## Rejected experiments

### Production bus-access history culling

An experiment compiled the 64-entry last-bus-access forensic ring out of
production while retaining the functional watchdog counter and the complete
ring in trace/dev/co-sim builds. Despite removing two ring stores and index
bookkeeping from every 68K bus access, two order-balanced 6,000-frame pairs
were both negative:

- Pair 1: baseline 385.865 FPS, candidate 369.510 FPS (**-4.24%**).
- Pair 2: candidate 342.221 FPS, baseline 361.020 FPS (**-5.21%**).

This satisfies the two-negative-experiment stop condition. The source change
was reverted and is not retained. The likely explanation is unfavorable code
layout/cache movement, but that hypothesis is not sufficient reason to keep a
measured regression.

### Plane name-table row cache

An experiment cached each plane's `cell_y * width` name-table row offset.
Although Sonic 1 remained exact across 62 Green Hill framebuffer checkpoints,
the added cache comparisons and state traffic outweighed the removed multiply:

- Pair 1: baseline 594.741 FPS, candidate 556.663 FPS (**-6.402%**).
- Pair 2: candidate 539.852 FPS, baseline 595.281 FPS (**-9.311%**).
- The 2.909-point spread passed the variance gate, while release `.text` grew
  by 384 bytes and the executable grew by 512 bytes.

Both orderings were decisively negative, so the row cache was reverted.

### Stateful plane-row geometry cache

A broader follow-up cached plane masks, strides, tile-row offsets, and
vertical-flip coordinates in each A/B/window fetch context. Sonic 1 remained
exact across all 100 checkpoints of the 6,000-frame active Green Hill route,
but the added state and row-change checks outweighed the arithmetic they
removed:

- Pair 1: baseline 572.544 FPS, candidate 549.151 FPS (**-4.086%**).
- Pair 2: candidate 580.637 FPS, baseline 609.895 FPS (**-4.797%**).
- The 0.711-point spread passed the variance gate, while release `.text` and
  the executable each grew by 512 bytes.

The candidate was reverted. A preliminary comparison against an older,
differently configured control was discarded before this result; the retained
numbers use same-configuration binaries with identical data and BSS sizes.

### Adjacent pattern-row address cache

A follow-up to consecutive-pixel attribute reuse also retained the computed
pattern-row address on the existing adjacency hit. It added no comparison or
branch and removed tile-index, vertical-flip, and row-address work from cache
hits. Sonic 3 & Knuckles remained exact across 97 gameplay-fuzz framebuffer
checkpoints, and the benchmark full-state and audio hashes matched, but two
independent order-balanced runs both failed the variance gate:

- Run 1: **+4.484%**, then **-0.288%** (4.772-point spread).
- Run 2: **-0.419%**, then **+4.160%** (4.579-point spread).

The candidate throughput stayed near 630 FPS while the controls varied, so
there is no accepted effect size. Logical CPU 11 passed a same-binary
calibration before the first run; CPU 13 later failed its own calibration and
was not used. Release `.text` grew by 640 bytes and the executable by 512
bytes. The candidate was reverted rather than retain an unproven,
instruction-cache-costly change.

### Algebraic palette-index packing

The plane pixel palette index
`((attribute >> 13) & 3) * 16 + pixel` was rewritten as the equivalent
`((attribute >> 9) & 0x30) | pixel`. This removes one operation in the local
instruction sequence, remained exact across all 97 Sonic 3 & Knuckles
gameplay-fuzz framebuffer checkpoints, and matched the benchmark full-state
and audio hashes. Text, data, BSS, and executable size were unchanged.

The same-binary calibration passed with a 1.391-point spread, but the
18,000-frame order-balanced pairs were both decisively slower:

- Pair 1: **-7.597%**.
- Pair 2: **-6.785%**.
- Median **-7.191%**, 0.812-point spread.

The source-level operation count did not predict the optimized inlined
register/layout result. The two-negative stop rule applies, so the expression
was reverted.

### Address-free adjacent pattern-byte reuse

The consecutive-coordinate attribute hit was also tested as proof that the
second raw pixel of a pair could reuse the first pixel's packed pattern byte.
Unlike the older pattern-byte cache, this variant did not compute or compare a
pattern address before reuse; on eligible hits it skipped tile/row address
construction, vertical flip, and the VRAM load.

Sonic 3 & Knuckles remained exact across 97 gameplay-fuzz framebuffer
checkpoints and matched full-state/audio hashes, but the extra branch and
cache state grew release `.text` by 1,344 bytes and were decisively slower:

- Pair 1: **-10.712%**.
- Pair 2: **-11.185%**.
- Median **-10.949%**, 0.472-point spread.

The candidate was reverted. Together with the earlier pattern-address cache
and pattern-row experiments, this closes additional per-pixel pattern caching
as a diminishing-return family on the current renderer.

### SuperZazu direct low-RAM read window

An optional side-effect-free read window was added to the interpreter and
mapped to the Genesis Z80's mirrored 8 KiB RAM. Reads below `$4000` could then
load RAM directly instead of taking the indirect memory callback; YM2612,
bank-register, and 68K-window reads retained the normal bus path. Writes were
deliberately unchanged so sound tracing and debug watchpoints remained intact.

Sonic 3 & Knuckles matched framebuffer, full-state, and audio-state hashes at
1,200, 3,000, and 6,000 frames. Throughput did not justify the extra branch
and interpreter state:

- Eight alternating 1,200-frame pairs had a **-0.364%** median.
- Aggregate baseline and candidate throughput were effectively equal, with
  2.647% baseline CV and 2.249% candidate CV.
- Longer trials were also noise-dominated and failed the strict spread gate;
  total system utilization remained 22-38%, but individual pinned runs saw
  transient interference.

The source change was reverted. The robust median gives no evidence that
adding a conditional fast window improves this MSVC release build, so the
candidate is rejected independently of the unstable longer measurements.

### Conditional sprite-operator clears

An experiment skipped clearing the sprite shadow/highlight operator arrays on
scanlines where shadow/highlight mode was disabled. S3K remained exact across
97 gameplay hashes, three PNGs, and final SRAM, but both timed orderings were
slower:

- Pair 1: baseline 588.425 FPS, candidate 559.738 FPS (**-4.875%**).
- Pair 2: candidate 557.614 FPS, baseline 581.412 FPS (**-4.093%**).
- The 0.782-point spread passed the variance gate. Release `.text` grew by 64
  bytes and the executable grew by 512 bytes.

The optimization was reverted; retaining the unconditional optimized clears
is measurably faster on this workload.

### Static tail-dispatch indices

An experiment attached a precomputed dispatch-table index to static
cross-function tail edges while retaining the normal lookup for dynamic jumps
and RAM trampolines. The Sonic 1 candidate added 59 bytes to the executable.
Its first two 6,000-frame cycle pairs were **+0.901%** and **-0.022%**, too
small to distinguish from the accompanying same-binary calibration.

A later 3,000-frame requalification failed the measurement gate outright:
the candidate pairs were **-5.018%** and **+15.893%** (20.911-point spread).
Same-binary checks also exposed time-position bias, including false medians of
**+2.895%** and **-1.243%** in separate calibrations. The source was reverted
without spending an S3K build. The dynamic binary-search dispatch remains the
retained general solution.

### Inactive Plane-B diagnostic post-process

The disabled widescreen Plane-B diagnostic leaves a predictable condition in
the scanline pixel loop. Moving its output-only marker into a second pass
would remove that condition from normal rendering without changing diagnostic
output and left the Sonic 1 executable size unchanged.

This deliberately small candidate was stopped before measurement because its
same-binary calibration falsely reported **+7.617%** and **+0.789%** (a
6.828-point spread). The source was reverted. This is not evidence that the
change is slower; it is evidence that the current host cannot resolve the
expected sub-percent effect reproducibly, which is sufficient under the
measurement contract's fast-fail rule.

### Z80 and audio attribution

A 12,000-frame Sonic 3 & Knuckles gameplay run was sampled at 4 kHz with the
Visual Studio Diagnostics CPU agent. WPA exported 84,926 process samples, of
which 81,144 (95.55%) were in the game executable. MinGW symbols embedded in
the PE are not resolved by WPA, so `tools/attribute_cpu_samples.py` reverses
the module's ASLR relocation and maps each leaf address to the nearest defined
GNU `nm` text symbol. All 81,144 executable samples were attributed.

Grouping those leaf symbols by subsystem gives:

| Bucket | Executable samples |
| --- | ---: |
| VDP | 68.138% |
| YM2612 | 12.109% |
| Z80 execution and bus access | 5.733% |
| PSG | 1.257% |
| Audio observability | 0.801% |
| Mixer | 0.304% |
| Event routing | 0.149% |
| Resampler and device delivery | 0.000% |

The zero delivery result is expected: the finite uncapped benchmark excludes
host pacing and presentation. It establishes that delivery cannot affect this
throughput result, but does not close the separate audio-enabled correctness
and pacing measurement. The event queue is also not a useful throughput
target at 0.149%; it remains functional state required by Sonic's bursty
one-handler PCM traffic.

The largest named audio/Z80 leaf functions were YMFM chip clocking (3.989%),
YMFM 4-op output (2.727%), FM operator volume calculation (2.684%), Z80 opcode
execution (2.801%), YM2612 sample generation (2.078%), PSG advancement
(1.252%), Z80 stepping (1.028%), and Z80 bus reads (1.038%). The always-on
audio anomaly detector consumed 0.801%, making it the first production
observability candidate to measure independently.

### Release audio anomaly detection

The audio anomaly detector reads every FM and PSG sample and insertion-sorts
two rolling 120-frame windows. It is diagnostic state: it does not feed the
sound chips, mixer, or guest-visible state. A same-binary Sonic 2 experiment
used per-side environment overrides to isolate only the detector state:

- Detector enabled, then disabled: **+10.992%**.
- Detector disabled, then enabled: **+9.197%**.
- Median **+10.094%**, 1.795-point spread.

The first source candidate added a Release environment-variable override.
Although functionally correct, its 128-byte `.text` growth shifted the large
generated executable enough to regress both paired orderings (**-5.919%** and
**-5.750%**). That implementation was rejected.

The retained implementation changes only the detector's static default:
Debug builds remain enabled, while Release builds start disabled and retain
`audio_obs_set_enabled()` for programmatic diagnosis. It adds no executable,
`.text`, `.data`, or `.rdata` size. Sonic 2's final order-balanced
18,000-frame pairs were:

- Baseline 628.355 FPS, candidate 641.613 FPS: **+2.110%**.
- Candidate 628.597 FPS, baseline 624.996 FPS: **+0.576%**.
- Median **+1.343%**, 1.534-point spread; baseline CV 0.268%, candidate CV
  1.025%.

All public-title release routes remained exact: Sonic 1 100/100 native and
100/100 widescreen checkpoints, Sonic 2 100/100 in each mode, Sonic 3 &
Knuckles 97/97 in each mode, and Rocket Knight Adventures 280/280 native
(874/874 total). Fresh control comparisons also matched interpreter totals,
reported zero true/raw misses, and had zero strict-stack mismatch lines.
A 6,000-frame Sonic 2 audio-enabled capture produced byte-identical
89,568,044-byte WAV files on control and candidate
(`BFC0D131189FF4A10B91E739385BE11E86256EE479675771E20F653BD5157431`).

### September 2026 presentation and diagnostic pass

This pass reopened normal runtime overhead that the finite `--benchmark` path
does not time: presentation, audio delivery bookkeeping, and miscellaneous
host diagnostics. The baseline framework source for the Sonic 1 comparison was
an isolated detached worktree at
`997bd1f1c754f38dd598318661b2a52553d33cc0`, built with Visual Studio 17 2022
x64, Release, `GEN_ENABLE_TRACE=OFF`, and the same Sonic 1 ROM/config inputs as
the candidate. The candidate build used the patched canonical framework checkout
with the same compiler and flags.

Retained source changes from the diagnostic side of the pass are deliberately
small:

- Cache `SDL_RenderSetLogicalSize` inputs and only remember the new logical size
  after SDL reports success, so transient failures retry on the next frame.
- Remove dead `SDL_GetTicks` frame bookkeeping that was written but never read.
- Cache `SDL_GetPerformanceFrequency` after successful audio initialization and
  reuse it for audio-delivery `dt_us` ring entries.
- Upload only the active VDP rectangle for the frame texture when the actual SDL
  texture scale mode is nearest. A normal 320x224 frame uploads 286,720 bytes
  instead of the 512x480 backing texture's 983,040 bytes, a 696,320-byte
  reduction per presented frame (**70.833% fewer bytes**) on that path.

The active-rectangle transfer is intentionally nearest-only. Validation with
`tests/runtime/sdl_texture_rect_test.c` reproduced stale right/bottom edge
texels on the Direct3D renderer when raw active-rectangle uploads were combined
with linear filtering after a shrink. The runner therefore tracks the actual
SDL texture scale mode: it initializes the cache from `SDL_GetTextureScaleMode`,
falls back to "filtered" if that query fails, and updates the cache only after a
successful `SDL_SetTextureScaleMode`. Any filtered/non-nearest mode keeps the
full texture upload. The synthetic SDL upload timing sanity check reported
20,000 320x224 updates at 0.0755 s for active-rectangle upload versus 0.7769 s
for full upload, but that is an isolated transfer test, not a whole-game speedup
claim.

The Sonic 1 same-binary calibration failed before any baseline-vs-candidate
timing was accepted:

- CPU 11, baseline vs the same baseline, 6,000 frames: phantom deltas
  **+0.278%** and **+8.594%**, spread **8.316 percentage points**.
- CPU 15, baseline vs the same baseline, 6,000 frames: phantom deltas
  **+12.839%** and **+5.489%**, spread **7.350 percentage points**.

Both calibration runs preserved the expected benchmark fingerprints
(`state_fnv1a64=B73909CF3B280638`,
`audio_state_fnv1a64=5F095FF7D29387A5`), so they are evidence of host timing
noise rather than state divergence. Raw logs and executable hashes are under
`scratch/perf_reports/`. Because the calibration gate failed twice, no
end-to-end speedup is accepted from the finite benchmark, and no original Xbox
hardware validation has been performed locally.

A duplicated native renderer fastpath was rejected from retention. The
replacement structural renderer keeps one compositor path: it pre-fills pillar
bars, renders only the authentic+widescreen content columns, and preserves the
asymmetric right bar case. The VDP renderer differential harness in
`tests/runtime/` passed for this shape. In the MinGW `-O2 -DNDEBUG` harness,
the VDP object `.text` fell from 8,080 to 7,320 bytes (-760),
`gvdp_render_scanline` fell from 4,752 to 4,048 bytes (-704), and BSS stayed
396,384 bytes. In the final MSVC Sonic 1 Release build, total executable size
stayed 4,558,848 bytes; `.text` virtual size moved from `0x3E333F` to
`0x3E332F` (-16 bytes), `.text` raw size stayed `0x3E3400`, `.data` virtual
grew by 32 bytes with raw size unchanged, and `.rdata` virtual shrank by 6
bytes with raw size unchanged.

Final Sonic 1 cross-state validation passed with
`tests/runtime/run_sonic1_cross_state.py`; the retained summary is
`build/runtime-validation/sonic1-cross-state-final2/summary.txt`. The matrix
created baseline and candidate save states, loaded each save state in both
executables, and then repeated the candidate-on-baseline load with linear
filtering and with `linear_filter = 0` plus `SDL_RENDER_SCALE_QUALITY=1`. All
main runs ended `PASS` with matching final RAM (`149d63...e87a`) and screenshot
(`fc63b3...6420`) hashes; the Green Hill screenshot was also visually reviewed.
A strict `GENESIS_STRICT_JSR_STACK=1` bidirectional pair also passed from
`build/runtime-validation/sonic1-cross-state-strict3`: candidate loading the
baseline save and baseline loading the candidate save both exited successfully,
matched post-input RAM and screenshots, reported zero true/raw dispatch misses,
and reported no JSR stack errors. The candidate strict run used 18,503
interpreter calls. These are desktop validation results; no original Xbox
hardware validation was available locally. The SDL texture-rectangle and VDP
renderer harness commands are documented in `tests/runtime/README.md`.

Manual desktop review also passed on the current canonical-framework Release
builds for Sonic 1, Sonic 2, Sonic 3 & Knuckles, and Rocket Knight Adventures:
the user verified recomp-ui launch, gameplay visuals, and audio for all four.
This is a visual/audio acceptance check, not an original Xbox performance
measurement.

## Final burndown closure

The remaining families were closed from measured ceilings and static census
rather than by adding low-confidence code:

- The post-cache S3K profile attributes **1.076%** to all named
  `m68k_read*`/`m68k_write*` wrappers and **1.007%** to lower
  `gbus_read*`/`gbus_write*` functions. Removing per-access production
  history had already made both measured orderings slower, so broad wrapper
  or bus-path surgery cannot justify its semantic and code-layout risk.
- Generated-source census found 13,534 memory callsites in Sonic 1 but only
  eight absolute accesses in plainly direct-map ROM/WRAM ranges. S3K had
  103,218 callsites and only 30 such absolutes. Most effective addresses are
  dynamic, while most other absolute accesses target MMIO/Z80 or unusual
  mappings that require the shared path. Static direct mapping is therefore
  a code-growth trade for a tiny callsite fraction under an approximately
  two-percent total sampled ceiling.
- `gvdp_write_control` and `gvdp_write_data`, which contain DMA launch,
  transfer, CRAM, and palette-update work, total **0.286%** in the same S3K
  profile. The renderer's line attribution instead concentrates on pattern
  nibble extraction, palette-index output, tile/pattern addressing,
  adjacency-cache state, and vertical flip. Window and full-screen scroll
  state were already hoisted once per scanline for a retained **+0.756%**.
- Native presentation already sets both widescreen content margin and canvas
  width to zero before rendering. Frame-policy memory reads are
  short-circuited while widescreen is not requested. The remaining inactive
  diagnostic condition was the only narrow candidate found and failed the
  calibration gate above.
- S3K sampling attributes **12.388%** to faithful YMFM/YM2612 synthesis,
  **2.714%** to Z80 execution/bus access, **1.279%** to PSG, and only
  **0.214%** to audio routing/mixing/delivery helpers. The narrow Z80
  low-RAM fast-read experiment was neutral, event routing is 0.149%, and the
  finite benchmark correctly records zero device-delivery work.
- Host-throughput and audio correctness are intentionally separate: finite
  uncapped pairs measure synthesis without presentation/device pacing, while
  the audio-enabled 6,000-frame capture above proved byte-identical WAV
  output. Delivery depth/anomaly rings execute only on real-time flushes and
  preserve information the generated WAV cannot contain.

The representative public workload matrix is Sonic 1 Green Hill gameplay,
Sonic 2 Emerald Hill gameplay, Sonic 3 & Knuckles gameplay fuzz, and Rocket
Knight Adventures' corrected combined attract/gameplay route, with the Sonic
attract routes retained as controls. Together they exercise per-line
scrolling, DMA/art loading, the Z80 sound drivers, YM2612/PSG synthesis,
native rendering, and enabled widescreen where supported. Puyo Puyo remains
excluded because no reproducible public title repository was found.

No runtime candidate from this final pass survived. Consequently the existing
874-checkpoint public differential gate remains the current release evidence and
does not need to be repeated for a documentation-only closeout.

## Burn-down

### P0 — harness and attribution

- [x] Add a finite uncapped benchmark that excludes host pacing/presentation.
- [x] Add a fixed-affinity, order-balanced paired benchmark with a variance
  gate.
- [x] Attribute 68K, VDP, Z80, sound synthesis, host presentation, and
  non-hardware diagnostics with 4 kHz sampling and exact symbol mapping.
  Runtime timing buckets were not added because all executable leaves were
  attributed without perturbing the measured path.
- [x] Allow framebuffer hashes and regression diagnostics to run through the
  finite benchmark path.
- [x] Add state and audio hashes directly to benchmark output.
- [x] Establish native-width and widescreen controls for every capable public
  title.
- [x] Tag the public Sonic 1/2/3K and RKA attract/gameplay routes as the
  representative raster, DMA/art-loading, and Z80/audio workload matrix.

### P1 — production observability

- [x] Confirm the TCP command server and full frame-snapshot history are
  already replaced by stubs in trace-off production.
- [x] Confirm chip, sound-command, and co-simulation histories are already
  compile-time gated.
- [x] Measure production bus-access history removal; reject it as slower.
- [x] Disable the sampled audio anomaly detector by default in Release while
  preserving Debug defaults and programmatic diagnosis.
- [x] Attribute audio delivery separately: it is absent from finite uncapped
  throughput and executes once per real-time flush. Preserve the functional
  queue/underrun/pacing state.

### P2 — generated 68K and bus paths

- [x] Profile generated/runtime memory and dispatch paths, inventory static
  direct-map callsites, and retain strict-stack/interpreter/watchdog
  diagnostics in the differential gate.
- [x] Reject direct-map inlining under the measured ceiling and static
  callsite census; retain the shared path for dynamic addresses, MMIO,
  tracing, RAM trampolines, and unusual mappings.
- [x] Replace the profiled Genesis linear tail-dispatch scan with the
  SCC68070 backend's binary-search pattern. Preserve RAM dispatch, return
  capture, split-stack state, overrides, and fallback behavior.
- [x] Track generated and runtime code size alongside throughput; reject
  instruction-cache-hostile wins.

### P3 — VDP, DMA, and widescreen

- [x] Establish scanline rendering as the dominant sampled bucket.
- [x] Attribute scanline time further between background fetch, renderer body,
  VRAM reads, and sprites.
- [x] Close windows, scrolling, palette conversion, DMA, and widescreen policy
  from line attribution, retained invariant hoists, VDP-port ceilings, and
  native/widescreen differential controls.
- [x] Hoist invariant tile/row work out of per-pixel loops only where the
  measured path permits it.
- [x] Keep the faithful shared VDP as the floor. Do not replace it with
  title-specific rendering.
- [x] Confirm native widescreen margin/canvas and policy memory reads are
  gated early; fast-fail the remaining diagnostic-only branch when its
  calibration could not resolve a sub-percent effect.

### P4 — Z80 and audio

- [x] Attribute Z80 stepping, YM2612, PSG, event routing, mixing, and resampling
  independently.
- [x] Benchmark finite host throughput separately from the audio-enabled,
  byte-identical WAV correctness capture.
- [x] Preserve the large functional event queue required by Sonic's one-handler
  PCM burst; its 0.149% sampled share cannot justify representation risk.

### P5 — regression sweep

- [x] Enumerate title repositories with public remotes; local-only titles are
  skippable.
- [x] Build each public title in a linked worktree at BelowNormal priority with
  one job.
- [x] Run complete attract/demo coverage plus deterministic basic input fuzz.
- [x] Compare framebuffer/state/audio hashes, strict-stack results, and
  dispatch/interpreter misses. Investigate every new miss.
- [x] Include native width and enabled widescreen in the release gate for every
  capable public title.

## Stop conditions

Stop a line after two well-formed neutral/negative experiments, when profiling
shows the bucket cannot repay the complexity, or when exactness would be
weakened. Keep the faithful implementation and record rejected experiments so
the same attractive-looking change is not repeated later.

## 2026-10-22 accuracy-preserving investigation

Work is tracked centrally in **beads-8r88**, with rejected-load correctness in
**beads-kbp9**. The task burndown and approval state live in Beads. This section
records engineering evidence, not an integration approval. Nothing from this
investigation has been committed or merged.

The source boundary is the nested shared-engine repository, not its parent
collection directory. The coherent starting engine is `64c1ffbd11`; Sonic 2
is `0df1890409`, and Rocket Knight Adventures is `3e07af8`. All work uses the
isolated `codex/accuracy-perf-20261022` worktree on G:. The canonical engine
and Sonic 2 worktrees remain clean. The owner's existing RKA clean-display-name
edit is preserved and copied into the qualification worktree.

Sonic 2's gameplay route is the primary fixture because it has strong state,
audio, strict-stack, pixel and rollback checks. RKA follows with its Konami
engine, opening cinematic, stage transition, native SAT traversal, and custom
scroll/actor renderer. Both routes use ordinary inputs and reach gameplay;
neither writes success flags. Native and 16:9 views are checked separately.
This is representative regression coverage, not a whole-game hardware audit.

### Attribution and candidate selection

Owned-child main-thread RIP samples use the exact MSVC linker map, with a 2 ms
attempted interval. Sampling suspends that thread briefly and perturbs timing;
these runs are attribution only. Inlined work belongs to its containing symbol,
and audio callback threads are excluded. Do not equate sample shares with an
exact tile-decoder or guest-instruction breakdown.

The dense Sonic 2 replay sample (`--benchmark 12000`, probe `16:8`) collected
3,207 samples without sampler errors: `digest_bytes` 40.35%,
`gvdp_render_scanline` 31.40%, and sprite traversal 1.62%. It completed 749 probe
passes and 5,992 replay ticks with no divergence/asymmetry or dispatch misses.
Sonic 2's snapshot was roughly 1.75 MB: the game section alone was 1,590,965
bytes, versus 1,492 EXEC bytes, 90,600 machine bytes and 65,536 RAM bytes.
Hashing the large game image, rather than copying the small EXEC image, explains
the low ceiling of the rejected digest experiment.

A separate full-runtime sample without replay collected 5,136 main-thread
samples: the VDP renderer body contributed 44.10%, sprites 3.29%, and 22.70%
fell outside the executable. An expanded 5,051-sample run resolved the latter
by DLL: SDL 13.86%, VCRUNTIME 4.34%, ntdll 3.21%, MSVCRT 1.64% and UCRT 0.20%
of all samples. Nearest-export estimates put 3.68% in memmove and 2.12% in
memcmp; these are estimates, not private DLL symbols or call-stack attribution.
Generated 68000 code and dispatch, Z80 stepping, scanline synchronization,
YM2612 operators/channels, PSG generation, mixing/resampling, device pushes,
rewind snapshots and runtime bookkeeping remain active. No audio-disabled
result is used for gameplay claims.

Map-object attribution gives the following workload-specific shares. The dense
sample uses the core benchmark for localization; the normal sample uses the
complete runtime. The different feature scopes are not a throughput comparison.

| Containing-source bucket | Normal runtime | Dense core replay |
| --- | ---: | ---: |
| VDP decode, sprite traversal and composition | 46.47% | 33.12% |
| Generated 68000 bodies, dispatch and interpreter | 4.49% | 3.68% |
| Z80 execution and dispatch | 3.19% | 2.62% |
| Scanline scheduling and fiber coordination | 2.14% | 1.28% |
| Mixed 68000 glue, memory and synchronization | 1.54% | 1.50% |
| Bus, memory and chip coordination | 1.15% | 1.34% |
| YM2612, PSG, emulated audio and mixing | 12.20% | 9.23% |
| Host audio delivery | 3.17% | disabled by core scope |
| Snapshot, digest, rewind and probe bookkeeping | 0.65% | 42.31% |
| Main loop and scanline host output | 1.11% | 0.59% |
| External DLLs | 23.24% | 3.49% |
| Other game/library/unresolved | 0.65% | 0.84% |

YMFM alone is 9.86%, dispatch 2.51%, the scanline scheduler 1.96%, PSG 1.76%
and rewind capture 0.61% in the normal sample. `profile-attribution.json`
enumerates the individual objects. Inlined work is charged to its containing
symbol/object; these are not independent per-instruction counters.

The shared scalar plane path already keeps separate A/B/window name-entry
caches within a draw. The optional batched path fetches spans bounded by tile,
window and two-cell-scroll changes. Sprite evaluation traverses the SAT and
reads pattern opacity to set collision/overflow. Native VDP state is live per
scanline; title renderers also retain art/scroll/actor views, with Sonic 2
palettes kept live for HBlank changes. RKA surveys patterns for its own custom
view. Rewind allocates and submits exact execution snapshots to rbengine.
Those contracts were inspected rather than replaced with SNES-style caches.

The retained generic candidate removes repeated plane fetching and composition
when a replay requests no scanline sink. `gvdp_evaluate_scanline` calls the
existing sprite evaluator with the identical complete canvas width, centered
origin, sprite limits, masks and pattern reads. It evaluates both IM2 output
rows. All CPU/Z80 scheduling, FIFO/DMA/ports, status/interrupt timing and chip
draining remain on the existing path. Visible output still uses the original
renderer. No persistent cache, VRAM version tag, game specialization, new ISA
requirement or negotiated host profile is introduced.

The independent streaming-section-digest experiment retained exact hash bytes
while avoiding the full serialized staging copy. Its paired result was neutral
and it was rejected. `runner/rb_state.c` is restored to the reference; the
experiment survives only as a scratch patch. Further tile-row/palette/SAT
deduplication was not implemented: its invalidation and comparison cost needs
fresh evidence, especially with direct native/private-pattern writes.

The existing optional BATCHED plane implementation was also tested as an
independent candidate, prompted by fresh normal-runtime VDP dominance. Its
previous Sonic 1 timing in `ENGINE_VDP_BATCHING.md` failed a 40-point noise
spread under foreign compiler load, so it did not establish a speed result.
The new Sonic 2 candidate uses the coherent reference plus only that fixed
build selection. It is excluded from the final bundle: the native view passed
its timing control, but the 16:9 candidate pair exceeded the declared variance
limit. The harness now
requires an explicit expected candidate backend for implementation comparisons,
retaining fixed per-side identity, all state/audio fingerprints and emitted-work
guards; explicit matching view selection is available as `--widescreen`.

### Controlled measurements

Local identical-binary measurements failed under other windows' benchmarks
(paired deltas +0.55% and -13.68%). They are disqualified. With owner permission,
SERVER was accessed through `F:\NAS`, checked for other work and disk pressure,
and reserved using `G:\Temp\Codex\.benchmark.lock`. No service was stopped.
Its host is an i5-4670 with four logical CPUs; timing children use CPU 2 and
normal priority. Builds use one job at BelowNormal. All generated data,
temporary files, profiles and saves stay on G:.

The first NAS turn used MSVC Release, SCALAR, 6,000 full-runtime frames,
600 warmup frames, two order-balanced A/B pairs, normal audio synthesis/mixing,
SDL dummy video/audio output and the software renderer. The full host pipeline
executes, but this is not a physical GPU or speaker performance measurement.
Dense replay uses probe `16:8`. The metric is total process
cycles per frame; the table's throughput delta is `baseline/candidate - 1`.
State/audio fingerprints and emitted-work counts match in every pair.

| Experiment | Pair deltas | Median cycle-throughput delta | Pair spread |
| --- | --- | --- | --- |
| Sonic 2 identical binary | +0.206%, +0.417% | +0.311% | 0.211 pp |
| Sonic 2 discarded composition | +10.868%, +11.982% | +11.425% | 1.114 pp |
| Sonic 2 streaming digest, rejected | -0.339%, +1.758% | +0.710% | 2.097 pp |
| RKA identical binary | -0.083%, +0.393% | +0.155% | 0.476 pp |
| RKA discarded composition | +19.808%, +18.440% | +19.124% | 1.368 pp |

The retained result corresponds to about 10.25% fewer process cycles for Sonic
2 and 16.05% for RKA **in this dense rollback stress workload**. It does not
claim an ordinary single-player FPS improvement. Uncapped queue overflow counts
are retained in the raw logs; they are not evidence of paced audible delivery.

After adding the cold-load correctness guard below, a fresh final-bundle A/A
control failed: -0.498% and -3.781%, median -2.139%, spread 3.283 pp against the
3 pp gate. Timing stopped before A/B; that failed turn cannot qualify a final
speed claim. The historical controlled results above identify the candidate;
final-bundle measurement remained pending until the headroom-2 control below.
One follow-up attempt changed warmup to the complete 6,000-frame route and
required a measured 60-second idle interval before any benchmark. It aborted
during the first interval (13.60% aggregate host CPU against the declared 10%
ceiling), before running another control. There is no retry-until-pass loop;
the owner's other benchmark windows retain priority.

The subsequent headroom-1 control used complete-route warmups and allowed
ordinary work on other cores, with measured memory/disk/core headroom. Its
+0.455%/-2.021% A/A deltas passed the existing 3 pp project limit but failed
an additional 1 pp diagnostic screen; no A/B ran. Before seeing any final A/B
data, headroom-2 returned to the established 3 pp resolution, retaining an
absolute A/A median limit of 1%. It used physical CPU 3, measured 60 seconds of
headroom, 6,000-frame warmups and measurements, ABBA order, and separate
matching controls with and without the dense replay probe. No service was
stopped. All previous failed controls remain in the evidence.

The exact final SCALAR bundle (same cold-load guard in both sides) completed:

| Title / workload | A/A pair deltas | Candidate pair deltas | Candidate median cycle-throughput | Candidate spread |
| --- | --- | --- | ---: | ---: |
| Sonic 2 dense replay | +1.385%, -0.428% | +7.372%, +9.303% | +8.337% | 1.931 pp |
| Sonic 2 ordinary runtime | +1.116%, -1.479% | +0.998%, -0.989% | +0.005% | 1.987 pp |
| RKA dense replay | +0.161%, -0.935% | +18.919%, +18.444% | +18.681% | 0.475 pp |
| RKA ordinary runtime | +1.433%, +0.273% | +0.513%, -1.226% | -0.356% | 1.739 pp |

The paired median reductions in **total process cycles** are **7.69% Sonic 2**
and **15.74% RKA** in dense replay. Ordinary gameplay is neutral within its
matching A/A noise range. These final-bundle measurements supersede the earlier
candidate-only point estimates for handoff claims; they do not promise those
gains for ordinary single-player play. `qualified-timing.json` and the
`*-headroom2-*.log` files retain the complete results and work fingerprints.

The independent BATCHED experiment matched the coherent reference's complete
state, audio, pixels and work in both native and 16:9 routes. Its serialized
third timing turn selected CPU 2 from a measured 60-second headroom interval
before starting any timing. Native A/A deltas were +1.247%/+0.112%; native A/B
was +22.777%/+21.798%, median +22.287% cycle-throughput, spread 0.979 pp.
The 16:9 A/A was -0.904%/-0.344%, but A/B was +10.678%/+6.285%, spread
4.393 pp against the 3 pp limit. That view's timing is inconclusive, so the
backend is not promoted or included in the owner bundle. No secondary-title
or combined-bundle BATCHED qualification is claimed. Earlier turns stopped at
the declared CPU-headroom gates before timing. `batched-disposition.json` and
the `sonic2-batched-primary3-*` logs preserve all four timing records.

### Accuracy and lifecycle evidence

The candidate matched the frozen renderer for directed cases, 4,000 random
states, odd canvases, IM2, shadow/highlight, independent scroll modes, imports,
reset/restore, live palette/scroll writes and repeated views. Each discarded
evaluation is compared against full rendering over the complete GVDP state.
Sprite tests cover count/pixel limits, intentional masks, collisions, direct
VRAM writes and direct private-pattern transparent-to-opaque edits. Both
SCALAR and BATCHED compile paths pass under MinGW; MSVC runtime tests also pass.
No newly added assembly or optional CPU path needs host feature selection.

The 39-test CTest suite passed, including fiber snapshots, audio, rewind,
sprite/host rendering and native campaign file tests. Both title measurement
CLIs passed their 13 pre-initialization cases. Individual reference/candidate
runs matched complete WAV bytes, architectural/audio fingerprints, 100 pixel
checkpoints, native-save outputs and work counts per 6,000-frame native/wide
arm. Each arm completed 374 probes and 2,992 replay ticks without divergence,
asymmetry or dispatch misses. The final coherent bundle, with the load fix in
both reference and candidate, repeated and passed these checks for both titles
in both views. Exact final EXE identities are in `artifacts.json` and the
`*-qualified-diff.json` reports.

Both current builds pass repeated save/load branches, fresh-process continuation
and invalid-input rejection in native and wide views. Actual F8 rewind was
exercised through owned SDL windows: Sonic 2's guest counter went
1101 -> 997 -> 1034; RKA's went 4615 -> 4513 -> 4550, proving restore followed
by continued execution. Gameplay captions are exactly the clean game names
with debug ports supplied. Actual launcher captions also match. Build/backend
identity remains in diagnostics and About.

Actual two-process Sonic 2 direct-LAN rollback, with 20 ms simulated latency,
4 ms jitter and forced prediction errors, completed 19 changed replays per
peer, zero desyncs/refusals and 28 matching confirmed-state samples. This is
same-build/same-host LAN evidence; production lobby, Internet/NAT and cross-host
qualification are not claimed. Production was not used.
RKA's existing build does not link the netplay driver; its dense replay probe
and actual local rewind are qualified, without adding a netplay feature.

Paced 6,000-frame native-code runs with the 16:9 view also match their coherent
references in guest/audio fingerprints, emitted work and 100 pixel checkpoints.
Both versions report dummy-device queue drops: Sonic 2 reference/candidate
487,901/469,145 and RKA 507,629/497,470, each from 22,392,000 source frames.
Those scheduling-dependent device counters are not an exactness gate or proof
of drop-free speaker output. Chip execution and captured pre-device PCM are
qualified; normal speaker delivery remains an explicit owner playtest item.

Qualification exposed an existing RKA fallback-loader bug: it rejected a short
file after overwriting the live guest. The cold-path fix snapshots current
execution before the legacy in-place readers and restores it on a failed or
trailing-data load. Failed loads preserve queued chip writes. The new
`run_legacy_state_failure.py` compares RAM, full guest/audio fingerprints and
all 30 frame hashes against a no-load control for seven header/CPU/RAM/machine/
tail/trailing/bad-magic cases. All pass. This is a current-build safety fix;
no migration or compatibility with old saves is added. Owner progress files
are untouched, and private profiles isolate all automated and handoff saves.

The replay helper adds 128 virtual `.text` bytes relative to its corresponding
control. With the same cold-load guard in both sides, the final Sonic 2 EXE is
512 bytes larger due to PE section alignment; RKA's EXE size is unchanged.
This is portable C with the existing visible renderer as the normal output
path. Other OS/CPU builds have not been claimed from Windows compilation alone.

### Evidence and handoff boundary

Local evidence root:
`G:\Temp\Codex\segagenesisrecomp\accuracy-perf-20261022`.
NAS evidence root:
`G:\Temp\Codex\segagenesisrecomp-accuracy-perf-20261022` on SERVER.
Important records include `artifacts.json`, `nas-results`, the individual and
final differential reports, VDP logs, lifecycle/LAN reports, malformed-load
reports, complete per-arm logs/WAV files, and the two experiment patches.
`nas-evidence-final-v2.zip` contains bounded log/report/image records, excluding
ROMs, executables and bulk WAV data; it is also extracted as
`nas-evidence-final-v2`. The earlier `nas-evidence.zip` remains preserved.
Original WAVs remain in the NAS task directory. `review-engine.patch` contains
tracked source changes; `review-new-run_legacy_state_failure.py` carries the
new ROM-dependent regression script alongside it.

Private owner playtests used `playtest\Sonic2` and `playtest\RKA` with normal
audio, normal windows and adaptive Fit to screen. Automated checks used dummy
audio output. The owner accepted both combined playtests ("sonic 2 is good",
"rka ok") and then explicitly approved integration of the validated replay
optimization and failed-load protection. No rejected digest or inconclusive
BATCHED experiment is included. The isolated qualified builds remain available;
this integration does not publish a game release or alter owner progress files.
