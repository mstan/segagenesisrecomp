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
  affinity, balanced order, title identity, and this variance gate.
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

## Differential validation

All comparisons below used the same title revision and input on the
pre-change and candidate engines:

- Sonic 1 native: 100/100 framebuffer hashes and the final RAM snapshot were
  exact over 6,000 frames; strict dispatch/stack checks were clean.
- Sonic 1 widescreen Green Hill gameplay: 62/62 448x224 framebuffer hashes
  were exact through frame 3,742; strict checks were clean. The same 62/62
  route was exact again for the binary-dispatch candidate versus its
  current-core linear control.
- Sonic 2: the strict 63-checkpoint, 3,780-frame golden route was exact.
  Attract added 200/200 exact hashes and 20/20 exact PNGs through frame
  12,001. Active Emerald Hill fuzz added 67/67 exact hashes and an exact final
  PNG at native width, then another 67/67 exact hashes and exact PNG at
  448x224 widescreen resolution. The binary-dispatch candidate repeated both
  67/67 active-gameplay comparisons, including the native and 448x224 PNGs,
  against the linear control. The scanline-invariant candidate also repeated
  67/67 exact hashes and the exact final PNG at both native width and 448x224,
  with no bad diagnostics.
- Sonic 3 & Knuckles attract: 20/20 hashes and 19/19 PNGs were byte-identical.
  Gameplay fuzz added 97/97 exact hashes and 3/3 exact PNGs through frame
  5,856, with identical SRAM. The binary-dispatch candidate repeated the
  97/97, 3/3, and SRAM exact comparison against the linear control.
- Rocket Knight Adventures attract: 200/200 hashes and 20/20 PNGs were exact.
  The longer input route added 280/280 exact hashes and 10/10 exact PNGs.
  The binary-dispatch candidate repeated the longer 280/280 and 10/10 exact
  comparison against the linear control with no bad diagnostics.
  Both builds exhibit the same pre-existing white/static screen after the
  Konami logo, so this proves regression neutrality for the observable route,
  not gameplay coverage.

Puyo Puyo is excluded: neither the authenticated repository inventory nor
public-remote search found a title repository. Its engine-local bring-up is
not a reproducible public regression target.

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

## Burn-down

### P0 — harness and attribution

- [x] Add a finite uncapped benchmark that excludes host pacing/presentation.
- [x] Add a fixed-affinity, order-balanced paired benchmark with a variance
  gate.
- [ ] Add optional timing buckets for 68K, VDP, Z80, sound synthesis, host
  presentation, and non-hardware diagnostics.
- [ ] Add framebuffer/state hashes directly to benchmark output.
- [ ] Establish Sonic 1 native-width and widescreen controls, then add
  representative public workloads for raster effects, DMA-heavy scenes, and
  Z80/audio-heavy titles.

### P1 — production observability

- [x] Confirm the TCP command server and full frame-snapshot history are
  already replaced by stubs in trace-off production.
- [x] Confirm chip, sound-command, and co-simulation histories are already
  compile-time gated.
- [x] Measure production bus-access history removal; reject it as slower.
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
- [ ] Track generated code size and reject instruction-cache-hostile wins.

### P3 — VDP, DMA, and widescreen

- [x] Establish scanline rendering as the dominant sampled bucket.
- [ ] Attribute scanline time further among background fetch, sprites, windows,
  scrolling, palette conversion, DMA, and widescreen policy.
- [x] Hoist invariant tile/row work out of per-pixel loops only where the
  measured path permits it.
- [ ] Keep the faithful shared VDP as the floor. Do not replace it with
  title-specific rendering.
- [ ] Gate inactive enhancement work early while proving native and enabled
  widescreen behavior independently.

### P4 — Z80 and audio

- [ ] Attribute Z80 stepping, YM2612, PSG, event routing, mixing, and resampling
  independently.
- [ ] Benchmark host-throughput mode separately from audio-enabled correctness.
- [ ] Preserve the large functional event queue required by Sonic's one-handler
  PCM burst unless a differential test proves a different representation.

### P5 — regression sweep

- [ ] Enumerate title repositories with public remotes; local-only titles are
  skippable.
- [ ] Build each public title in a linked worktree at BelowNormal priority with
  one job.
- [ ] Run complete attract/demo coverage plus deterministic basic input fuzz.
- [ ] Compare framebuffer/state/audio hashes, strict-stack results, and
  dispatch/interpreter misses. Investigate every new miss.
- [ ] Include native width and enabled widescreen in the release gate.

## Stop conditions

Stop a line after two well-formed neutral/negative experiments, when profiling
shows the bucket cannot repay the complexity, or when exactness would be
weakened. Keep the faithful implementation and record rejected experiments so
the same attractive-looking change is not repeated later.
