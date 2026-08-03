# Genesis performance burn-down

The goal is uncapped headroom on lower-power hosts, including the original
Xbox, without weakening the shared Genesis hardware model or making behavior
title-specific. A paced 59.94 FPS run is a correctness check, not a performance
measurement.

## Measurement contract

- Use Release builds from the same compiler and source revision.
- Compile at BelowNormal priority with one build job on this machine.
- Use `--benchmark N ROM` for finite uncapped runs. Benchmark mode disables
  pacing, VSync presentation, audio-device delivery, autosave, and the launcher
  while retaining guest CPU, VDP, Z80, and sound-chip execution.
- Warm both binaries, then use order-balanced A/B pairs over a workload long
  enough to reach attract/gameplay. Report paired deltas and raw results.
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

## Burn-down

### P0 — harness and attribution

- [x] Add a finite uncapped benchmark that excludes host pacing/presentation.
- [x] Add a fixed-affinity, order-balanced paired benchmark with a variance
  gate.
- [ ] Add optional timing buckets for 68K, VDP, Z80, sound synthesis, host
  presentation, and non-hardware diagnostics.
- [x] Allow framebuffer hashes and regression diagnostics to run through the
  finite benchmark path.
- [x] Add state and audio hashes directly to benchmark output.
- [x] Establish native-width and widescreen controls for every capable public
  title.
- [ ] Add explicitly tagged representative workloads for raster effects,
  DMA-heavy scenes, and Z80/audio-heavy titles.

### P1 — production observability

- [x] Confirm the TCP command server and full frame-snapshot history are
  already replaced by stubs in trace-off production.
- [x] Confirm chip, sound-command, and co-simulation histories are already
  compile-time gated.
- [x] Measure production bus-access history removal; reject it as slower.
- [x] Disable the sampled audio anomaly detector by default in Release while
  preserving Debug defaults and programmatic diagnosis.
- [ ] Attribute the small always-on audio delivery rings before changing them.
  Preserve functional queue/underrun/pacing state.

### P2 — generated 68K and bus paths

- [ ] Profile generated instruction accounting, direct ROM/WRAM accesses,
  MMIO routing, JSR/tail dispatch, strict-stack-disabled production flow,
  interpreter fallback, and watchdog checks separately.
- [ ] Inline only mappings whose semantics remain identical; retain the shared
  slow path for MMIO, tracing, RAM trampolines, and unusual mappings.
- [x] Replace the profiled Genesis linear tail-dispatch scan with the
  SCC68070 backend's binary-search pattern. Preserve RAM dispatch, return
  capture, split-stack state, overrides, and fallback behavior.
- [x] Track generated and runtime code size alongside throughput; reject
  instruction-cache-hostile wins.

### P3 — VDP, DMA, and widescreen

- [x] Establish scanline rendering as the dominant sampled bucket.
- [x] Attribute scanline time further between background fetch, renderer body,
  VRAM reads, and sprites.
- [ ] Finish independent attribution for windows, scrolling, palette
  conversion, DMA, and widescreen policy.
- [x] Hoist invariant tile/row work out of per-pixel loops only where the
  measured path permits it.
- [x] Keep the faithful shared VDP as the floor. Do not replace it with
  title-specific rendering.
- [ ] Gate inactive enhancement work early while proving native and enabled
  widescreen behavior independently.

### P4 — Z80 and audio

- [x] Attribute Z80 stepping, YM2612, PSG, event routing, mixing, and resampling
  independently.
- [ ] Benchmark host-throughput mode separately from audio-enabled correctness.
- [ ] Preserve the large functional event queue required by Sonic's one-handler
  PCM burst unless a differential test proves a different representation.

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
