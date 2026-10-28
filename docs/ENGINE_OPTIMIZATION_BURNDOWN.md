# Engine optimization burndown

Owner: `beads-3vb.31` under the Genesis engine meta-project. Campaign: `beads-2wy8.1` (`optimization campaign`). Inventory audit: `beads-3vb.30`.

Source audit base: `f422ad52a04b9d253c0e9cbcdb3476aabd563ae8`. Prepared in isolated branch `codex/engine-optimization-burndown-20261027`. Refresh and freeze remote engine/shared-core and representative consumer heads before implementation.

## Scope

Only reusable engine, code-generation, hardware-device, media, state, host and common-build work is in this goal. Sega CD backend work counts as engine work when its contract applies across consumers. A game-specific implementation remains title work even if its file currently resides in this repository.

Title-owned logic is saved separately in [TITLE_OPTIMIZATION_BACKLOG.md](TITLE_OPTIMIZATION_BACKLOG.md) and is excluded. Do not add game-PC checks, asset/layout knowledge, sound-driver semantics or game-specific outcomes to engine code. Consumer changes are limited to supported regeneration, pins/build wiring and mechanical adoption of a generic interface; new title behavior or adapter logic stays deferred.

This list contains **36 pending engine candidates**, split from the earlier42-family inventory. Several are parts or alternatives of the same coherent implementation. Priorities rank investigation, not proven savings. No further speedup has been established for these candidates.

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
  - Disposition/evidence: Core3b7b76f and engine2e58a00/14a58ad move native fault ownership above the entire frame across Main/Sub/device coordination. Every guard, instruction retirement, CPU ordering, IRQ and WAIT remains. Nested CPU faults retain local traps; cleanup-bearing STOP-peer/Z80/floor/external drivers hold the outer scope. Full-machine manual-step/frame parity includes complete CPU/device/clock/partial RAM across five error classes; MSVC/GCC core and machine checks pass, including older-core fallback. Actual new-package game/performance qualification remains.

- [x] **E02: Code identity and invalidation** — P1; original N02.
  - Goal: Use guarded code regions with complete write/remapping invalidation instead of per-instruction byte comparisons where qualified.
  - Boundary: Cover mutable overlays and self-modification through generic memory ownership/versioning.
  - Evidence: `runner/cd/catalog_lookup.h`, `external/m68k-recomp-core/runtime/m68k_step.c`.
  - Disposition/evidence: Fresh owned instruction spans retain every live opcode/extension comparison. Full code-version guard elision is deferred: device arrays are public, title HLE writes PRG/Word RAM directly, and imports bypass bus epochs. A bus-only dirty counter would be unsound. The selected cache retains descriptor discovery without trusting code bytes; no title-specific writer policy introduced.

- [ ] **E03: Owned memory access** — P1; original N03.
  - Goal: Resolve generic RAM/ROM/Word RAM mappings once for safe regions; reduce repeated bus decoding and byte-order bookkeeping.
  - Boundary: Keep guest bytes, aliases, write protection, MMIO and ownership transfers coherent. Game-object redesign is deferred.
  - Evidence: `runner/video/genesis_bus.c`, `runner/cd/segacd.c`.
  - Disposition/evidence: Owned code spans core53fc7b4/engine86d2d0c pass actual SonicCD3000frame Linux and matched Windows reference/candidate gates: full raw memory/pixels/WAV and observations exact. Preserve all live bytes, HINT/MMIO/1M/odd/wrap and ownership WAIT fallback. Updated v7 attribution puts remaining segacd_read8/read16 below1percent each; retain exact public data-bus path instead of adding an unmeasured second memory representation. Runtime-core/generator-core identities remain separate; final combined suite remains.

- [x] **E04: Generated native arithmetic and state** — P2; original N04.
  - Goal: Improve the common 68000 emitter with local registers, proven flag liveness, combined effective addresses and grouped cycle accounting.
  - Boundary: Materialize exact architectural state at every observable exit; no game-specific algorithms.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Deferred larger local-register/flag-liveness regions on qualified generated effect metadata and observable-exit materialization. Current CD descriptors expose exact instruction boundaries, live code guards and arbitrary operation callbacks; treating every callback as a pure opcode would misclassify HLE. A useful next package can annotate canonical generated pure operations, batch Main-only register work within device/Z80 deadlines, and retain unknown callbacks as exits. This is permitted architectural replacement, not a requirement to preserve the old interface. Current selected E01 removes repeated fault setup without making that unsound assumption.

- [x] **E05: Native call/control-flow consolidation** — P2; original N05.
  - Goal: Use proven direct call edges, inlining and larger native control-flow regions to reduce generic call/return/dispatch machinery.
  - Boundary: Preserve dynamic targets, guest stack skips, IRQ reentry and overlapping instruction streams; do not repeat rejected tail-index tuning unchanged.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`, `runner/glue.c`.
  - Disposition/evidence: Existing cartridge generation already emits qualified direct calls/aliases; dynamic targets, guest return-stack changes and interrupt reentry still need real exits. CD larger control-flow regions depend on E04 canonical effect/edge metadata and a bounded scheduler contract, not just additional native function coverage. Unchanged static-tail-index tuning was previously neutral/rejected and is not repeated. Defer new region fusion to that coherent metadata package; retain all direct/dynamic caller behavior.

- [x] **E06: Reusable native Z80 backend** — P2; original N07.
  - Goal: Qualify and improve the common uploaded-code/variant executor and native blocks, with supported build integration.
  - Boundary: Keep reset, BUSREQ, IRQ, self-modification and timed writes. Driver catalogs, semantic SMPS HLE and game rollout are separate title work.
  - Evidence: `runner/z80_recomp.c`, `runner/cd/z80_native.c`, `docs/Z80_STATIC_RECOMP.md`.
  - Disposition/evidence: The reusable uploaded-code/variant executor and generated native Z80 step already exist. Current SonicCD gates retire16241534 native Z80 instructions with zero floor/audio faults; cartridge driver qualification is retained in the earlier campaign. New CD attribution places bus-time coordination above the generated Z80 step itself, so E01/E07 address the reusable overhead. New driver catalogs/semantic SMPS HLE are title rollout T02/T13 and excluded. No new native-coverage or speed gain counted.

- [ ] **E07: Event-driven waits and scheduling** — P2; original N09.
  - Goal: Coalesce safe raster/fiber/CPU scheduling spans and handle proven waits through actual event wakeups.
  - Boundary: Do not bypass meaningful CPU work or add title-specific polling signatures.
  - Evidence: `runner/glue.c`, `runner/video/genesis_machine.c`, `runner/cd/segacd_coordinator.c`.
  - Disposition/evidence: Engine92bb86a derives each scanline deadline once per changed actual line counter, retaining exact restore/overflow behavior and original event ordering. Machine contracts pass. Further CPU/device scheduling consolidation remains under evaluation.

- [x] **E08: Transfer spans and layout mapping** — P2; original N10.
  - Goal: Batch qualified DMA/VDP/CDC/PCM transfers and generic Word RAM cell mappings under one ownership decision.
  - Boundary: Preserve partial progress, contention, FIFO state, PCM lane ordering, status and completion clocks.
  - Evidence: `runner/cd/dma.c`, `runner/cd/segacd.c`, `runner/video/genesis_vdp.c`.
  - Disposition/evidence: DMA is already advanced to exact six-clock word boundaries, including ownership WAIT, source FIFO consumption, PCM lane/+4 addressing, write protection and completion state. Active CPU intervals usually expose only short transfer spans; current1.58percent DMA advance attribution includes inactive calls, not demonstrated bulk-copy cost. Defer new bulk writes until a transfer-heavy built workload establishes long eligible spans. Larger device-advance coalescing belongs in E07 with MMIO/observer synchronization; do not use memcpy to cross observable completion clocks.

### Graphics and rendering

- [ ] **E09: Single image-production ownership** — P1; original N11.
  - Goal: Provide reusable lazy original-image production and exact fallback in the shared cartridge/CD raster pipeline.
  - Boundary: Retain CPU-visible sprite status and whole-frame fallback. Title scene eligibility and its adapter implementation belong in the title backlog. Do not land unused abstraction merely to check this item off.
  - Evidence: `runner/cd/machine.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Implemented and built-game correctness passed at16c14e6. Shared CD captures immutable rows, preserves per-row CRAM and composes exact whole-frame fallback on demand. Frozen current SonicCD ran3000 actually presented frames: all11screenshots, CPU/device observations, raw state/pixels and WAV match;720000rows captured/442800composed,38.5percent original compositions omitted on this route.898600bytes retained. Runtime timing and final Windows/combined gates remain; cartridge already has lazy row ownership.

- [x] **E10: Renderer interface independent of aspect ratio** — P1; original N12.
  - Goal: Separate shared renderer ownership/selection from viewport width and elective widescreen policy.
  - Boundary: Keep existing consumers compatible. Stock sprite/scene fidelity and enabling a particular game's custom renderer remain title tasks.
  - Evidence: `runner/main.c`, `runner/game_video.h`.
  - Disposition/evidence: Requires excluded title stock-fidelity/adoption T08; do not add an unused shared selector.

- [ ] **E11: Fused generic VDP raster** — P1; original N14.
  - Goal: Combine tile/window/scroll spans, decode, priority and shadow/highlight composition in the shared renderer.
  - Boundary: Existing BATCHED fetching is partial work, not a new gain. Preserve all supported VDP modes and the universal reference path.
  - Evidence: `runner/video/genesis_vdp.c`.
  - Disposition/evidence: Native row and composition package implemented; Sonic1 current built-game correctness passed, further engine raster ownership work and performance qualification pending.

- [ ] **E12: Shared sprite evaluation jobs** — P2; original N15.
  - Goal: Prepare reusable generic sprite membership/opacity/priority work for both hardware status and image production.
  - Boundary: Preserve limits, masks, raster-visible changes, overflow and collision timing; no replacement of game BuildSprites.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Implemented CD hardware sprite capture once per row; deferred whole-frame fallback reuses captured sprite lanes and never evaluates hardware sprites again. Synthetic provider/width/state and full CPU-frame contracts pass; actual SonicCD3000frame output/state/audio matches. No title SAT/scene algorithm changes. Final integration/performance gate remains.

- [x] **E13: Versioned generic graphics derivation** — P2; original N16.
  - Goal: Cache decoded VDP art, palettes and geometry only at verified VRAM/CRAM/register change boundaries.
  - Boundary: All writer paths must invalidate or take the existing fallback. Game layout, object and private-art caches are separate.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`, `runner/video/genesis_bus.c`.
  - Disposition/evidence: Evaluated both write epochs and full immutable-input keys. Public direct VRAM/private-pattern writes and imports make bus-only epochs incomplete; a complete replacement must own every producer or compare complete inputs. Existing narrow caches were already neutral/rejected. New owned-row composition consumes only0.086percent of the sampled CD route (capture0.487percent); another row/palette cache is not justified there. Defer broader decoded-art ownership until a post-native-raster cartridge profile demonstrates enough affected cost; do not silently assume invalidation.

- [x] **E14: Native Sega CD stamp-device jobs** — P2; original N18.
  - Goal: Optimize the generic affine/stamp device in exact spans/blocks.
  - Boundary: Preserve Word RAM writes, priority, partial completion and IRQs. No Blue Spheres or other game-effect redesign.
  - Evidence: `runner/cd/stamp.c`.
  - Disposition/evidence: Generic stamp is already a native affine line job with explicit asynchronous completion, nibble priority and WordRAM writes. It has no samples in the recorded3000frame CD gameplay attribution. Defer a new block kernel pending a stamp-heavy built consumer/workload; neither synthetic throughput nor unrelated Blue Spheres title algorithms establish a current-game benefit. Preserve existing stamp/dma correctness fixtures for that future comparison.

- [x] **E15: Reuse unchanged hardware render results** — P3; original N19.
  - Goal: Reuse generic render jobs/regions/images when the complete hardware input identity is unchanged.
  - Boundary: Continue guest execution, status and audio; track every palette/VRAM/scroll change. Title-specific image eligibility is deferred.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Considered caching complete captured-row inputs plus CRAM and validating output ownership, rather than relying on unsafe end-of-frame VRAM hashes. Hardware sprite/status evaluation must continue through raster changes; frame-end equality cannot establish equal intermediate rows. Current sampled CD resolve_row cost0.086percent gives negligible ceiling after lazy ownership. Defer cross-frame generic packet reuse until a demonstrated static-render-heavy consumer; no title scene/image eligibility added.

- [ ] **E16: Final pixels and upload ownership** — P2; original N20.
  - Goal: Fuse shared indexed/ARGB conversion, copies, color processing, UI staging and upload preparation.
  - Boundary: Keep raw verification/capture output, filter edges, UI and portable output paths correct.
  - Evidence: `runner/main.c`, `runner/cd/presentation.c`.
  - Disposition/evidence: CD original rows now convert only when requested, using retained CRAM and64/192-entry palette; raw readers use the getter. Exact SonicCD3000frame evidence passes with38.5percent fewer original compositions. Broader cartridge/upload pipeline still requires evaluation; reduced row count is not a runtime speed claim.

- [x] **E17: Optional generic GPU raster backend** — P3; original N21.
  - Goal: Evaluate hardware tile/sprite/priority composition from reusable immutable render packets.
  - Boundary: Keep CPU-visible VDP effects and exact supported output with a complete portable CPU path. Per-title custom GPU adapters are outside scope.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`, `runner/cd/presentation.c`.
  - Disposition/evidence: Immutable raster packets now provide a valid generic CPU-status/GPU-input boundary; the old subsystem split is not a design restriction. Current CD CPU composition is0.086percent and capture0.487percent in the sampled route. SDL software/dummy attribution does not measure a physical GPU. Defer a new shader/upload/backend until a real GPU workload demonstrates a worthwhile affected cost; preserve universal CPU output and raw capture.

- [x] **E18: Independent engine work scheduling** — P3; original N22.
  - Goal: Evaluate workers for immutable rendering/audio spans and safe overlap of preparation/upload.
  - Boundary: No guest-state races, reordered observable events or required extra-frame latency; keep single-core/unsupported-host operation.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/audio.c`.
  - Disposition/evidence: Evaluated worker ownership around immutable raster packets and ordered audio intervals. Hardware status and next chip/snapshot state must be ready at their actual simulation boundaries; moving them to a worker cannot add an unqualified frame of latency. CD composition/capture are under1percent combined in current attribution, so dispatch/copy/barrier costs have little demonstrated headroom. Defer workers until an independently owned heavy job is measured after retained native spans.

### Audio

- [ ] **E19: One ordered audio timeline** — P2; original N23.
  - Goal: Consolidate engine timestamps, stable ordering, register pairing and deferred DAC-stream representation.
  - Boundary: Preserve cross-source timing, long bursts and exact paired writes; do not implement game sound-driver semantics.
  - Evidence: `runner/audio/mixer.c`, `runner/audio/event_queue.c`, `runner/cd/audio_native.c`.
  - Disposition/evidence: Stable natural-run merging and reverse FM pairing retain original push indices for optional applied-audio tracing. Reuse a typed union scratch for deferred events. Correction: total static event storage is6MiB versus5MiB originally, an increase of1MiB, rather than the initially estimated saving. Maximum262144-event and1200random oracle cases now compare original indices as well as exact events; MSVC/GCC and ASan/UBSan pass. Actual cartridge state/PCM/diagnostic validation pending.

- [x] **E20: Faithful FM kernels** — P1; original N24.
  - Goal: Optimize actual YM2612 operator/channel kernels beyond the already-shipped sample-block wrapper.
  - Boundary: Keep envelopes, feedback, LFO, DAC, filtering, arbitrary register writes and state exact; retain portable kernels.
  - Evidence: `runner/audio/ym2612_ymfm.cpp`, `runner/external/ymfm/ymfm_fm.ipp`.
  - Disposition/evidence: The faithful FM sample-block wrapper is already shipped; no new gain is credited to it. Replacing or vector-packing the YM2612 operator kernel is allowed, but needs a concrete bit-identical replacement across envelope/LFO/feedback/DAC/register-write and serializer state. No such replacement is qualified by the current vendor API or historical profile alone. E36 compiler optimization and E21 exact PSG spans are the bounded first alternatives; defer a new FM kernel until current post-block attribution and a specific arithmetic/layout design justify that separate chip qualification.

- [ ] **E21: PSG event spans** — P3; original N25.
  - Goal: Generate eligible tone/noise runs in blocks.
  - Boundary: Advance counters, LFSR, phase and filters exactly; historical whole-game cost is small.
  - Evidence: `runner/audio/sn76489.c`.
  - Disposition/evidence: Engine57b4cea advances settled-muted PSG spans with closed-form tone/noise counters and exact periodic/white LFSR jumps, producing zeros in one block. Nonzero filters/active channels retain ordinary per-sample synthesis. MSVC/GCC dual-backend full state/PCM/pending-output restore tests pass; current Windows SonicCD3000frame output and state match. Cartridge combined/actual timing qualification remains.

- [x] **E22: Sega CD PCM/CDDA spans** — P2; original N26.
  - Goal: Advance generic PCM channels, CDDA/fader and queues in timed blocks instead of per-sample callbacks.
  - Boundary: Preserve wave RAM writes, loops, seek/pause, sample boundaries, underruns and status.
  - Evidence: `runner/cd/rf5c164.c`, `runner/cd/cdda.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: PCM/CDDA already advance natively over timed intervals; remaining per-sample sinks expose ordered timestamps, phase/fader state, queue accounting and error boundaries. Replacing these with block publication is viable through a new generic span-sink contract and mechanical native mixer adoption, but cannot simply omit zero samples/callbacks. Current CDDA advance1.43percent includes transport work and establishes no material block-publication saving. Defer that contract until source/mixer attribution demonstrates enough cost; retain the existing exact PCM/CDDA fixtures.

- [x] **E23: Synthesis-to-device pipeline** — P1; original N27.
  - Goal: Combine generic chip staging, multi-source mixing, interpolation, filters and device-rate conversion.
  - Boundary: Maintain required samples, DRC, phase, latency and capture contracts; no quality reduction or disabled synthesis.
  - Evidence: `runner/audio.c`, `runner/audio/mixer.c`, `runner/cd/audio_native.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: Examined synthesis through staging/mixing/resampling as one pipeline. Integer PSG division, clipping, the deterministic generated-WAV tap, extra-source interpolation, master volume and DRC phase make moving mixing after resampling behavior-changing unless the complete computation is reproduced. Copies alone move small bounded frame buffers; current CD interpolation attribution1.89percent is not evidence for dropping required synthesis or output samples. Retain shared E19 ordering and E21/E25 exact span wins; defer fused chip-to-device production pending a concrete equivalent stream/kernel design and matched host-output costs.

- [x] **E24: Audio buffer ownership** — P3; original N28.
  - Goal: Reduce avoidable callback handoff locks, copies and temporary storage.
  - Boundary: Preserve recovery/concealment, race-free callbacks and device deadlines; distinguish paced delivery from throughput.
  - Evidence: `runner/audio.c`.
  - Disposition/evidence: The host bridge already uses one bounded mixed frame and one producer handoff, with the audio-device lock protecting SPSC ring/DRC/callback state. Replacing this with atomics/borrowed buffers requires ownership of all shared callback state, not just a lock deletion. Lock/callback timing instrumentation already exists; dummy-device throughput does not establish speaker contention. Defer a new handoff on a demonstrated physical-device lock/jitter problem, preserving concealment/recovery and callback deadlines.

- [ ] **E25: Exact constant/silent spans** — P3; original N29.
  - Goal: Represent provably constant output efficiently without per-sample queue work where valid.
  - Boundary: Timers, phase, filters, fader and future sample state must still evolve exactly.
  - Evidence: `runner/audio/sn76489.c`, `runner/cd/cdda.c`, `runner/cd/rf5c164.c`.
  - Disposition/evidence: Exact settled PSG silence is implemented under E21; counters and LFSR never freeze, and later unmute/restore remains exact. CDDA/PCM zero samples still carry observable queue/timestamp/fader/underflow state, so silence alone does not allow dropping them. A compressed timed-run source would require a new mixer/observer contract plus a measured cost; current CDDA advance attribution1.43percent is the entire transport, not just zero publication. Keep these sources faithful and defer unmeasured run compression.

### Generic media and console firmware

- [ ] **E26: Bounded generic media prefetch** — P2; original N32.
  - Goal: Read upcoming raw CDDA/movie/nonresident sectors off the simulation path using actual device requests/transport information.
  - Boundary: Bound memory and preserve emulated delivery, seeks, cancellation, errors and synchronous fallback; no title preload tables.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/cdd.c`, `runner/cd/cdc.c`.
  - Disposition/evidence: Engine96a2e2f preserves bounded1MiB stdio read-ahead by avoiding seeks for consecutive nonresident raw reads. Actual Linux and matched Windows3000frame CD gates preserve complete state/pixels/WAV. Emulated delivery/seek/pause remain unchanged; reader and injected-error tests retain buffers and synchronous fallback. Speculative asynchronous workers are deferred because no host read stall is demonstrated by current CPU attribution; final combined gate remains.

- [x] **E27: Verified media handoff contract** — P1; original N33.
  - Goal: Provide a safe common way to reuse completed media verification within one launch instead of independent redundant payload reads.
  - Boundary: Validate actual complete identity every launch; never substitute timestamps. Changes to a game's launcher/converter are deferred.
  - Evidence: `runner/cd/disc_image.c`.
  - Disposition/evidence: Shared image open already verifies payload once; duplicate launcher pass requires excluded T12 integration and safe cross-process ownership proof.

- [x] **E28: Portable full-media validation** — P2; original N34.
  - Goal: Evaluate accelerated portable/platform SHA and safe immutable-sector validation reuse.
  - Boundary: Windows CNG already ships. Keep complete payload checks, corruption detection, CPU/OS gating and full portable restart/fallback.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/sector.c`.
  - Disposition/evidence: Evaluated/implemented: optional OpenSSL1.1+ EVP full-payload validation outside Windows, with CNG retained and universal C fallback. WindowsCNG/portable and LinuxOpenSSL/missing-provider/portable plus injected init/midstream/final failures pass. Six matched SonicCD one-frame launches preserve raw state/pixels/WAV/native summaries. Two balanced Linux warm-host A/B pairs observed12.4/13.8percent less full-launch wall time and40.2/41.1percent less processCPU; A/A drift2.0percent wall/1.7percent CPU. Raw samples in cd-media-startup.json. This is a bounded shared-host launch result, not a runtime throughput claim; final combined/Windows qualification and dependency packaging remain.

- [ ] **E29: Resident-media representation** — P2; original N35.
  - Goal: Evaluate lower-copy/raw-versus-user-data residency, bounded mapped/shared storage and memory footprint.
  - Boundary: Preserve generic raw/audio/movie/sector reader contracts and allocation fallback. Memory savings are distinct from speedups.
  - Evidence: `runner/cd/disc_image.c`.
  - Disposition/evidence: Read-only mapped residency96a2e2f passes full mandatory payload comparison, actual Linux and matched Windows3000frame game parity, and forced mapping-failure copied fallback. Existing256MiB cap retained. Record published mapped/private byte counters and actual host memory separately; mapping size is not an RSS saving. Final combined game/cost summary remains.

- [x] **E30: Reusable console-firmware services** — P2; original N36.
  - Goal: Assess closed standard Sega CD BIOS services that can be replaced once for supported firmware profiles across games.
  - Boundary: Require a real reusable firmware ABI and complete state/BRAM/UI/IRQ behavior. Game SP/file-engine routines and game-PC exceptions are deferred; do not generalize a title handler merely by moving it into the engine.
  - Evidence: `runner/cd/segacd_coordinator.c`, `runner/cd/standalone_program.c`.
  - Disposition/evidence: The native BIOS instruction catalog is reusable, but a ROM hash/entry PC is not a closed high-level BIOS service ABI. Disc command timing, ownership, IRQs, BRAM and BIOS UI remain observable. The current loading HLE is a game-owned file/SP service, so moving it into the engine would not generalize it. No standard BIOS service is demonstrated hot in the current gameplay profile. Defer whole-service HLE until a named reusable firmware ABI and its complete effects/workload are established; title services stay in T01/T05/T12.

### State and replay

- [x] **E31: Engine snapshot representation and identity** — P2; original N37.
  - Goal: Evaluate generic authoritative/derived state separation, dirty pages/deltas/checkpoints and exact incremental state identity.
  - Boundary: Cover all writes and restore/replay behavior. Title-owned snapshot payload reduction is separate; do not repeat the already-shipped checksum change.
  - Evidence: `runner/rb_state.c`, `runner/frame_snapshots.c`.
  - Disposition/evidence: Authoritative sections and complete-byte identity already share one table; the portable full-byte XXH64 change and discarded replay work are shipped. Dirty-page/delta identity needs every native/direct/import writer or protected-page ownership, not bus-only epochs. Title fiber/snapshot payload dominates historical dense-replay cost and is excluded. Defer a new generic delta/page representation until post-XXH64 per-section costs and a complete writer/restore model justify it. Current-build snapshots/native saves remain required; old state compatibility is not a dependency.

- [x] **E32: Serializer and snapshot storage** — P3; original N38.
  - Goal: Reuse bounded buffers, serialize into owned storage and reduce generic snapshot allocation/copy/thumbnail work.
  - Boundary: Keep rewind cadence, atomic save replacement and invalid-input rejection; old save-state compatibility is not required.
  - Evidence: `runner/local_states.c`, `runner/rb_state.c`.
  - Disposition/evidence: Verified snapshot ownership: rbe_snap_ring_store takes the serialized blob on success, so the local-state malloc is retained storage, not a leak or an avoidable immediate-copy buffer. Reuse would require a transactional capacity/borrow API in shared rbengine so a failed serialization does not destroy the prior rewind entry. Generic thumbnails are already bounded; ordinary snapshot cost was historically small after the shipped digest improvement. Defer pooling until a current allocation/copy cost demonstrates benefit; preserve atomic saves and invalid-input checks.

- [x] **E33: Discarded-replay engine work** — P2; original N39.
  - Goal: Remove remaining purely derived shared rendering/publication preparation from replay that produces no visible output.
  - Boundary: Continue all authoritative game/device/audio state. Title-specific capture changes require separate work and cannot be silently skipped.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/main.c`.
  - Disposition/evidence: Additional shared cartridge replay image work ruled out at this base: s_resim_hooks has no sink/producer/post-drain observation; machine_run_frame_ex only evaluates sprite status on discarded rows; genesis_sim_step continues chip evolution. Current Sonic1 rollback probes pass. Remaining pre-raster work sets simulation-affecting viewport/RAM parameters and cannot be removed. Title publication algorithms remain outside scope; new CD deferred work is tracked under E09.

### Host and built program

- [x] **E34: Host presentation/deadline pipeline** — P2; original N40.
  - Goal: Consolidate shared pacing, upload/present queue behavior and remaining compositor/CD coarse-wait costs.
  - Boundary: Reuse existing D3D11/deadline work and beads-hj4v; keep NTSC audio cadence and report delivery jitter separately from throughput.
  - Evidence: `runner/main.c`, `runner/cd/standalone.c`, `runner/cd/presentation.c`, `runner/host_wait.c`.
  - Disposition/evidence: Cartridge high-resolution deadline/D3D11 work already ships. CD keeps exact rational NTSC pacing with a coarse sleep; blindly reusing the shared sleep/spin helper would add a roughly1ms spin on portable hosts, so it is not a free improvement. Raw indexed/ARGB output and UI copies have different ownership; mutating the simulation image for UI would corrupt raw capture/fallback. Defer further pacing/GPU-present consolidation to measured delivery jitter/physical GPU costs and a suitable nonspinning fallback/shared UI output contract. Reuse beads-hj4v; no new unmeasured host pipeline adopted.

- [x] **E35: Asynchronous engine persistence and diagnostics** — P3; original N41.
  - Goal: Buffer immutable save/evidence/capture data and use bounded rings for enabled tracing instead of hot-path file I/O.
  - Boundary: Keep transactional native saves, diagnostics and clean shutdown. Default yield logging is already opt-in; reuse beads-3vb.11.
  - Evidence: `runner/runtime_evidence.c`, `runner/local_states.c`, `runner/glue.c`.
  - Disposition/evidence: Default runtime evidence is buffered/dirty-tracked and rate-limited to at most one write per minute; yield tracing is already opt-in. Native saves and quickstates are transactional cold operations. No default hot-path file stall is demonstrated. Defer asynchronous persistence/trace rings to an enabled diagnostic workload or measured save stall, reusing beads-3vb.11 and preserving shutdown/error/atomic-replacement semantics. The audio ordering change explicitly preserves the optional applied trace and its original indices.

- [x] **E36: Whole-program native build optimization** — P2; original N42.
  - Goal: Evaluate common LTO/PGO, cross-TU inlining, hot/cold layout and qualified optional kernels on real built consumers.
  - Boundary: Keep arithmetic precise, public build portability and fixed qualified netplay capabilities; supported consumer build wiring is allowed, title logic is not.
  - Evidence: `cmake`, `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Standard CMake IPO is already available as a build experiment; no extra unused engine switch is needed. Current code comparisons refresh both Sonic1 arms with the same MSVC19.44 toolset rather than relabeling the old19.41 binary. New LTO/PGO adoption remains deferred: no matched built-game evidence establishes a gain, and current local/NAS shared reservations reject timing admission. Benchmark availability does not block the retained engine batch. Keep precise arithmetic, portable compilation and fixed compatibility identity; no fast-math/host-only ISA or unsupported whole-program claim introduced.

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
