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

## Burn-down

### P0 — harness and attribution

- [x] Add a finite uncapped benchmark that excludes host pacing/presentation.
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
- [ ] Measure direct/cached dispatch only where profiles show repeated lookup
  cost. Preserve return capture, split-stack state, and fallback behavior.
- [ ] Track generated code size and reject instruction-cache-hostile wins.

### P3 — VDP, DMA, and widescreen

- [ ] Attribute scanline time among background fetch, sprites, windows,
  scrolling, palette conversion, DMA, and widescreen policy.
- [ ] Hoist invariant tile/row work out of per-pixel loops only where the
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
