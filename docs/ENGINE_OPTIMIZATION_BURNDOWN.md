# Engine optimization burndown

Owner: `beads-3vb.31` under the Genesis engine meta-project. Campaign: `beads-2wy8.1` (`optimization campaign`). Inventory audit: `beads-3vb.30`.

Source audit base: `f422ad52a04b9d253c0e9cbcdb3476aabd563ae8`. Prepared in isolated branch `codex/engine-optimization-burndown-20261027`. Refresh and freeze remote engine/shared-core and representative consumer heads before implementation.

## Scope

Only reusable engine, code-generation, hardware-device, media, state, host and common-build work is in this goal. Sega CD backend work counts as engine work when its contract applies across consumers. A game-specific implementation remains title work even if its file currently resides in this repository.

Title-owned logic is saved separately in [TITLE_OPTIMIZATION_BACKLOG.md](TITLE_OPTIMIZATION_BACKLOG.md) and is excluded. Do not add game-PC checks, asset/layout knowledge, sound-driver semantics or game-specific outcomes to engine code. Consumer changes are limited to supported regeneration, pins/build wiring and mechanical adoption of a generic interface; new title behavior or adapter logic stays deferred.

This list contains **36 engine candidates**, split from the earlier42-family inventory. Several are parts or alternatives of the same coherent implementation. Priorities rank investigation, not proven savings. Per-item state is recorded below and in `engine-optimization-results.json`; no additional runtime speedup is qualified yet.

## Execution and acceptance

- Work solo in isolated worktrees. Claim `beads-3vb.31` before implementation and update it and the campaign at meaningful milestones.
- Prioritize coherent replacement of the largest demonstrated repeated work across execution/memory/device or render/audio boundaries. Treat required observable behavior as the contract; existing subsystem interfaces and representations may be replaced.
- Evaluate every item. Record implemented/qualified, neutral, rejected, ruled out, or deferred with a concrete dependency/evidence. An unchecked item remains pending. A checked item means its evaluation is resolved, not necessarily implemented.
- Reuse existing evidence and supported QA. Obtain the smallest current measurement needed to choose a useful implementation, integrate promptly, and reassess after two unsuccessful experiments. Do not add unused infrastructure solely to mark an item complete.
- Accept net-neutral and net-positive outcomes per game. Fix/reject reproducible individual-game regressions; aggregate gains cannot offset them. Treat noisy small mixed results as neutral/inconclusive. Identical baseline/candidate failures are shared defects.
- Use matched built-game workloads/settings/features/hosts. Keep raw per-game samples, source/binary identities, package labels, CPU/frame and relevant startup/memory/delivery costs. Game consumers are validation fixtures; their own optimization backlog is outside scope.
- Keep correctness, current-build save/load/rewind/rollback, native saves, HLE/LLE contracts, required emulation/audio and portable fallbacks. Old save-state backward compatibility is not required. Gate optional assembly/SIMD and negotiate simulation-affecting capabilities once before netplay frame0.
- Keep automated launches muted and isolated. Do not wait indefinitely for quiet/stress benchmark hosts or hold all useful work for one timing slot. Preserve qualification gaps and make no unsupported performance claims.
- Finish with one coherent committed engine branch, a resolved burndown and retained evidence. Prepare representative normal-window/audio builds when owner validation is needed; require owner approval before merging/releasing a new engine batch. Keep one current delivered build per game when release is authorized.

## Candidate checklist

### Execution and memory

- [ ] **E01: Native CD execution regions** — P1; original N01.
  - Goal: Run reusable Main/Sub/Z80 native regions until the next observable interaction or device deadline.
  - Boundary: Keep CPU ordering, interrupts, DMA, Word RAM ownership, raster and audio deadlines exact; no title PC/routine recognition.
  - Evidence: `runner/cd/segacd_coordinator.c`, `runner/cd/machine.c`.
  - Disposition/evidence: Pending.

- [ ] **E02: Code identity and invalidation** — P1; original N02.
  - Goal: Use guarded code regions with complete write/remapping invalidation instead of per-instruction byte comparisons where qualified.
  - Boundary: Cover mutable overlays and self-modification through generic memory ownership/versioning.
  - Evidence: `runner/cd/catalog_lookup.h`, `external/m68k-recomp-core/runtime/m68k_step.c`.
  - Disposition/evidence: Pending.

- [ ] **E03: Owned memory access** — P1; original N03.
  - Goal: Resolve generic RAM/ROM/Word RAM mappings once for safe regions; reduce repeated bus decoding and byte-order bookkeeping.
  - Boundary: Keep guest bytes, aliases, write protection, MMIO and ownership transfers coherent. Game-object redesign is deferred.
  - Evidence: `runner/video/genesis_bus.c`, `runner/cd/segacd.c`.
  - Disposition/evidence: Pending.

- [ ] **E04: Generated native arithmetic and state** — P2; original N04.
  - Goal: Improve the common 68000 emitter with local registers, proven flag liveness, combined effective addresses and grouped cycle accounting.
  - Boundary: Materialize exact architectural state at every observable exit; no game-specific algorithms.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Pending.

- [ ] **E05: Native call/control-flow consolidation** — P2; original N05.
  - Goal: Use proven direct call edges, inlining and larger native control-flow regions to reduce generic call/return/dispatch machinery.
  - Boundary: Preserve dynamic targets, guest stack skips, IRQ reentry and overlapping instruction streams; do not repeat rejected tail-index tuning unchanged.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`, `runner/glue.c`.
  - Disposition/evidence: Pending.

- [ ] **E06: Reusable native Z80 backend** — P2; original N07.
  - Goal: Qualify and improve the common uploaded-code/variant executor and native blocks, with supported build integration.
  - Boundary: Keep reset, BUSREQ, IRQ, self-modification and timed writes. Driver catalogs, semantic SMPS HLE and game rollout are separate title work.
  - Evidence: `runner/z80_recomp.c`, `runner/cd/z80_native.c`, `docs/Z80_STATIC_RECOMP.md`.
  - Disposition/evidence: Pending.

- [ ] **E07: Event-driven waits and scheduling** — P2; original N09.
  - Goal: Coalesce safe raster/fiber/CPU scheduling spans and handle proven waits through actual event wakeups.
  - Boundary: Do not bypass meaningful CPU work or add title-specific polling signatures.
  - Evidence: `runner/glue.c`, `runner/video/genesis_machine.c`, `runner/cd/segacd_coordinator.c`.
  - Disposition/evidence: Pending.

- [ ] **E08: Transfer spans and layout mapping** — P2; original N10.
  - Goal: Batch qualified DMA/VDP/CDC/PCM transfers and generic Word RAM cell mappings under one ownership decision.
  - Boundary: Preserve partial progress, contention, FIFO state, PCM lane ordering, status and completion clocks.
  - Evidence: `runner/cd/dma.c`, `runner/cd/segacd.c`, `runner/video/genesis_vdp.c`.
  - Disposition/evidence: Pending.

### Graphics and rendering

- [ ] **E09: Single image-production ownership** — P1; original N11.
  - Goal: Provide reusable lazy original-image production and exact fallback in the shared cartridge/CD raster pipeline.
  - Boundary: Retain CPU-visible sprite status and whole-frame fallback. Title scene eligibility and its adapter implementation belong in the title backlog. Do not land unused abstraction merely to check this item off.
  - Evidence: `runner/cd/machine.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Shared CD owned raster capture, exact lazy original-image getter and mechanical existing adapter adoption committed at16c14e6. Palette/live-state replacement, partial frame, narrowed tail, observer detach and unsupported-provider fallback checks pass. Real-game correctness/performance remain pending.

- [x] **E10: Renderer interface independent of aspect ratio** — P1; original N12.
  - Goal: Separate shared renderer ownership/selection from viewport width and elective widescreen policy.
  - Boundary: Keep existing consumers compatible. Stock sprite/scene fidelity and enabling a particular game's custom renderer remain title tasks.
  - Evidence: `runner/main.c`, `runner/game_video.h`.
  - Disposition/evidence: Deferred on title fidelity/adoption T08. `runner/main.c:own_custom_row_enabled` selects a custom producer only when `s_custom_width` is set; existing custom adapters can intentionally lift stock sprite/object limits. Enabling them at320px would change title policy without the excluded title qualification. An unused extra engine selector would not improve delivered runtime.

- [ ] **E11: Fused generic VDP raster** — P1; original N14.
  - Goal: Combine tile/window/scroll spans, decode, priority and shadow/highlight composition in the shared renderer.
  - Boundary: Existing BATCHED fetching is partial work, not a new gain. Preserve all supported VDP modes and the universal reference path.
  - Evidence: `runner/video/genesis_vdp.c`.
  - Disposition/evidence: Implementation in progress. Portable complete tile-row expansion and eight-byte priority/shadow composition are committed at `34b08ee` and `705129b`. Windows4000-case hardware differential,20000-pattern and12000-composition checks pass; matched built Sonic1 stock/16:9 images, machine/audio state, emitted work and rollback probes pass. Raw evidence: `G:/Temp/Codex/genesis-engine-burndown-20261027`. Performance unqualified: SERVER admission found no idle physical core; no timing comparison was run.

- [ ] **E12: Shared sprite evaluation jobs** — P2; original N15.
  - Goal: Prepare reusable generic sprite membership/opacity/priority work for both hardware status and image production.
  - Boundary: Preserve limits, masks, raster-visible changes, overflow and collision timing; no replacement of game BuildSprites.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: The CD raster packet evaluates hardware sprites at capture, retains all sprite lanes and composes whole-frame fallback without evaluating sprites again. No title scene/SAT algorithm changes. Component and CPU-frame contracts pass; actual per-game qualification pending.

- [ ] **E13: Versioned generic graphics derivation** — P2; original N16.
  - Goal: Cache decoded VDP art, palettes and geometry only at verified VRAM/CRAM/register change boundaries.
  - Boundary: All writer paths must invalidate or take the existing fallback. Game layout, object and private-art caches are separate.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`, `runner/video/genesis_bus.c`.
  - Disposition/evidence: Pending.

- [ ] **E14: Native Sega CD stamp-device jobs** — P2; original N18.
  - Goal: Optimize the generic affine/stamp device in exact spans/blocks.
  - Boundary: Preserve Word RAM writes, priority, partial completion and IRQs. No Blue Spheres or other game-effect redesign.
  - Evidence: `runner/cd/stamp.c`.
  - Disposition/evidence: Pending.

- [ ] **E15: Reuse unchanged hardware render results** — P3; original N19.
  - Goal: Reuse generic render jobs/regions/images when the complete hardware input identity is unchanged.
  - Boundary: Continue guest execution, status and audio; track every palette/VRAM/scroll change. Title-specific image eligibility is deferred.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Pending.

- [ ] **E16: Final pixels and upload ownership** — P2; original N20.
  - Goal: Fuse shared indexed/ARGB conversion, copies, color processing, UI staging and upload preparation.
  - Boundary: Keep raw verification/capture output, filter edges, UI and portable output paths correct.
  - Evidence: `runner/main.c`, `runner/cd/presentation.c`.
  - Disposition/evidence: Deferred CD original rows convert only when requested, using retained per-row CRAM and a64/192-entry palette instead of per-pixel DAC work. Existing raw dump/screenshot readers use the getter. Broader cartridge/upload pipeline remains to be evaluated; no new speedup claim.

- [ ] **E17: Optional generic GPU raster backend** — P3; original N21.
  - Goal: Evaluate hardware tile/sprite/priority composition from reusable immutable render packets.
  - Boundary: Keep CPU-visible VDP effects and exact supported output with a complete portable CPU path. Per-title custom GPU adapters are outside scope.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`, `runner/cd/presentation.c`.
  - Disposition/evidence: Pending.

- [ ] **E18: Independent engine work scheduling** — P3; original N22.
  - Goal: Evaluate workers for immutable rendering/audio spans and safe overlap of preparation/upload.
  - Boundary: No guest-state races, reordered observable events or required extra-frame latency; keep single-core/unsupported-host operation.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/audio.c`.
  - Disposition/evidence: Pending.

### Audio

- [ ] **E19: One ordered audio timeline** — P2; original N23.
  - Goal: Consolidate engine timestamps, stable ordering, register pairing and deferred DAC-stream representation.
  - Boundary: Preserve cross-source timing, long bursts and exact paired writes; do not implement game sound-driver semantics.
  - Evidence: `runner/audio/mixer.c`, `runner/audio/event_queue.c`, `runner/cd/audio_native.c`.
  - Disposition/evidence: Pending.

- [ ] **E20: Faithful FM kernels** — P1; original N24.
  - Goal: Optimize actual YM2612 operator/channel kernels beyond the already-shipped sample-block wrapper.
  - Boundary: Keep envelopes, feedback, LFO, DAC, filtering, arbitrary register writes and state exact; retain portable kernels.
  - Evidence: `runner/audio/ym2612_ymfm.cpp`, `runner/external/ymfm/ymfm_fm.ipp`.
  - Disposition/evidence: Pending.

- [ ] **E21: PSG event spans** — P3; original N25.
  - Goal: Generate eligible tone/noise runs in blocks.
  - Boundary: Advance counters, LFSR, phase and filters exactly; historical whole-game cost is small.
  - Evidence: `runner/audio/sn76489.c`.
  - Disposition/evidence: Pending.

- [ ] **E22: Sega CD PCM/CDDA spans** — P2; original N26.
  - Goal: Advance generic PCM channels, CDDA/fader and queues in timed blocks instead of per-sample callbacks.
  - Boundary: Preserve wave RAM writes, loops, seek/pause, sample boundaries, underruns and status.
  - Evidence: `runner/cd/rf5c164.c`, `runner/cd/cdda.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: Pending.

- [ ] **E23: Synthesis-to-device pipeline** — P1; original N27.
  - Goal: Combine generic chip staging, multi-source mixing, interpolation, filters and device-rate conversion.
  - Boundary: Maintain required samples, DRC, phase, latency and capture contracts; no quality reduction or disabled synthesis.
  - Evidence: `runner/audio.c`, `runner/audio/mixer.c`, `runner/cd/audio_native.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: Pending.

- [ ] **E24: Audio buffer ownership** — P3; original N28.
  - Goal: Reduce avoidable callback handoff locks, copies and temporary storage.
  - Boundary: Preserve recovery/concealment, race-free callbacks and device deadlines; distinguish paced delivery from throughput.
  - Evidence: `runner/audio.c`.
  - Disposition/evidence: Pending.

- [ ] **E25: Exact constant/silent spans** — P3; original N29.
  - Goal: Represent provably constant output efficiently without per-sample queue work where valid.
  - Boundary: Timers, phase, filters, fader and future sample state must still evolve exactly.
  - Evidence: `runner/audio/sn76489.c`, `runner/cd/cdda.c`, `runner/cd/rf5c164.c`.
  - Disposition/evidence: Pending.

### Generic media and console firmware

- [ ] **E26: Bounded generic media prefetch** — P2; original N32.
  - Goal: Read upcoming raw CDDA/movie/nonresident sectors off the simulation path using actual device requests/transport information.
  - Boundary: Bound memory and preserve emulated delivery, seeks, cancellation, errors and synchronous fallback; no title preload tables.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/cdd.c`, `runner/cd/cdc.c`.
  - Disposition/evidence: Pending.

- [x] **E27: Verified media handoff contract** — P1; original N33.
  - Goal: Provide a safe common way to reuse completed media verification within one launch instead of independent redundant payload reads.
  - Boundary: Validate actual complete identity every launch; never substitute timestamps. Changes to a game's launcher/converter are deferred.
  - Evidence: `runner/cd/disc_image.c`.
  - Disposition/evidence: Deferred on excluded launcher integration T12. `runner/cd/disc_image.c` performs one full payload verification per open; its second pass is only the required complete portable restart after platform-provider failure. The independent duplicate validation is in the title launcher. A safe cross-process proof/handle handoff needs that consumer integration; adding an unused trust API alone cannot remove runtime work and timestamp-only validation remains unacceptable.

- [ ] **E28: Portable full-media validation** — P2; original N34.
  - Goal: Evaluate accelerated portable/platform SHA and safe immutable-sector validation reuse.
  - Boundary: Windows CNG already ships. Keep complete payload checks, corruption detection, CPU/OS gating and full portable restart/fallback.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/sector.c`.
  - Disposition/evidence: Pending.

- [ ] **E29: Resident-media representation** — P2; original N35.
  - Goal: Evaluate lower-copy/raw-versus-user-data residency, bounded mapped/shared storage and memory footprint.
  - Boundary: Preserve generic raw/audio/movie/sector reader contracts and allocation fallback. Memory savings are distinct from speedups.
  - Evidence: `runner/cd/disc_image.c`.
  - Disposition/evidence: Pending.

- [ ] **E30: Reusable console-firmware services** — P2; original N36.
  - Goal: Assess closed standard Sega CD BIOS services that can be replaced once for supported firmware profiles across games.
  - Boundary: Require a real reusable firmware ABI and complete state/BRAM/UI/IRQ behavior. Game SP/file-engine routines and game-PC exceptions are deferred; do not generalize a title handler merely by moving it into the engine.
  - Evidence: `runner/cd/segacd_coordinator.c`, `runner/cd/standalone_program.c`.
  - Disposition/evidence: Pending.

### State and replay

- [ ] **E31: Engine snapshot representation and identity** — P2; original N37.
  - Goal: Evaluate generic authoritative/derived state separation, dirty pages/deltas/checkpoints and exact incremental state identity.
  - Boundary: Cover all writes and restore/replay behavior. Title-owned snapshot payload reduction is separate; do not repeat the already-shipped checksum change.
  - Evidence: `runner/rb_state.c`, `runner/frame_snapshots.c`.
  - Disposition/evidence: Pending.

- [ ] **E32: Serializer and snapshot storage** — P3; original N38.
  - Goal: Reuse bounded buffers, serialize into owned storage and reduce generic snapshot allocation/copy/thumbnail work.
  - Boundary: Keep rewind cadence, atomic save replacement and invalid-input rejection; old save-state compatibility is not required.
  - Evidence: `runner/local_states.c`, `runner/rb_state.c`.
  - Disposition/evidence: Pending.

- [x] **E33: Discarded-replay engine work** — P2; original N39.
  - Goal: Remove remaining purely derived shared rendering/publication preparation from replay that produces no visible output.
  - Boundary: Continue all authoritative game/device/audio state. Title-specific capture changes require separate work and cannot be silently skipped.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/main.c`.
  - Disposition/evidence: Additional shared cartridge replay image work ruled out at this base: s_resim_hooks has no sink/producer/post-drain observation; machine_run_frame_ex only evaluates sprite status on discarded rows; genesis_sim_step continues chip evolution. Current Sonic1 rollback probes pass. Remaining pre-raster work sets simulation-affecting viewport/RAM parameters and cannot be removed. Title publication algorithms remain outside scope; new CD deferred work is tracked under E09.

### Host and built program

- [ ] **E34: Host presentation/deadline pipeline** — P2; original N40.
  - Goal: Consolidate shared pacing, upload/present queue behavior and remaining compositor/CD coarse-wait costs.
  - Boundary: Reuse existing D3D11/deadline work and beads-hj4v; keep NTSC audio cadence and report delivery jitter separately from throughput.
  - Evidence: `runner/main.c`, `runner/cd/standalone.c`, `runner/cd/presentation.c`, `runner/host_wait.c`.
  - Disposition/evidence: Pending.

- [ ] **E35: Asynchronous engine persistence and diagnostics** — P3; original N41.
  - Goal: Buffer immutable save/evidence/capture data and use bounded rings for enabled tracing instead of hot-path file I/O.
  - Boundary: Keep transactional native saves, diagnostics and clean shutdown. Default yield logging is already opt-in; reuse beads-3vb.11.
  - Evidence: `runner/runtime_evidence.c`, `runner/local_states.c`, `runner/glue.c`.
  - Disposition/evidence: Pending.

- [ ] **E36: Whole-program native build optimization** — P2; original N42.
  - Goal: Evaluate common LTO/PGO, cross-TU inlining, hot/cold layout and qualified optional kernels on real built consumers.
  - Boundary: Keep arithmetic precise, public build portability and fixed qualified netplay capabilities; supported consumer build wiring is allowed, title logic is not.
  - Evidence: `cmake`, `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Pending.

## Coherent implementation packages

Use packages to reduce repeated work rather than treating every row as a separate patch. Effects overlap.

- Native execution/memory/device ownership: E01–E08; firmware E30 only where its reusable ABI is established.
- Single graphics producer and reusable raster data: E09–E16. GPU/worker alternatives E17–E18 require evidence that they repay their complexity.
- Ordered audio through device output: E19–E25.
- Verified media and bounded residency: E26–E29.
- State representation and discarded replay: E31–E33.
- Host delivery/I/O/build behavior: E34–E36.

## Already delivered and previous experiments

Do not count already-shipped lazy cartridge raster, tile spans, FM sample-block wrapper, host deadline waits, full-byte checksum change, discarded-plane replay work, CD descriptor lookup cache, or CD residency/CNG/ReadSectors as new gains. Backend choices vary by title; the historical Sonic1 SCALAR16:9 result does not establish the final package or another game's effect size.

Unchanged negative/neutral variants are not queued again: production bus-history removal, narrow plane/name/geometry/pattern caches, algebraic palette packing, Z80 low-RAM fast reads, conditional sprite clears, static tail indices and streaming digest of the same large payload. Revisit a mechanism only with materially new attribution or a broader design. Existing BATCHED selection needs package-specific qualification rather than being presented as a new implementation.

Historical evidence: [PERFORMANCE.md](PERFORMANCE.md), [NATIVE_OPTIMIZATION_BURNDOWN.md](NATIVE_OPTIMIZATION_BURNDOWN.md), [native-optimization-opportunities.json](native-optimization-opportunities.json), [Z80_STATIC_RECOMP.md](Z80_STATIC_RECOMP.md), [ENGINE_VDP_BATCHING.md](ENGINE_VDP_BATCHING.md). Raw audit inventory retained at `C:/Temp/Codex/sonic-cd-loading-20261027/remaining-candidates.json`; owning Beads metadata retains the machine-readable full list.

## Evaluation record

For each E-ID, record the chosen package and implementation/reference heads, built-game evidence, per-game positive/neutral/regression/inconclusive outcome, correctness, material costs and decision in `beads-3vb.31` and the campaign. Keep untested and deferred entries explicit. Do not close the implementation owner merely because this document exists.
