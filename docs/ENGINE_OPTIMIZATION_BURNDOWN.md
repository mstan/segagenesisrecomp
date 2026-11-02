# Engine optimization burndown

Historical evaluation/release owner: `beads-3vb.31`. Remaining implementation: **`beads-3z5k`** under the Genesis engine meta-project. Completion audit: `beads-1o1d`. Campaign: `beads-2wy8.1` (`optimization campaign`). Inventory audit: `beads-3vb.30`.

Source audit base: `f422ad52a04b9d253c0e9cbcdb3476aabd563ae8`. Prepared in isolated branch `codex/engine-optimization-burndown-20261027`. Refresh and freeze remote engine/shared-core and representative consumer heads before implementation.

**35 of 37 engine families are resolved; 2 remain open: E36 and E37.** The original 36-family released-base audit left 30 families open; later implementation and independent reviews resolve their bounded contracts or documented terminal dispositions. E02 now accepts qualified immutable BIOS ownership as neutral/inconclusive while rejecting physical-page and short-interval mechanisms; mutable/opaque owners keep measured-correct ordinary guards. See the [completion audit](ENGINE_OPTIMIZATION_COMPLETION_AUDIT.md) for the historical released-base findings. Resolution counts are not speedup counts, and owner combined play/merge/release approval remains pending.

## Scope

Only reusable engine, code-generation, hardware-device, media, state, host and common-build work is in this goal. Sega CD backend work counts as engine work when its contract applies across consumers. A game-specific implementation remains title work even if its file currently resides in this repository.

Title-owned logic is saved separately in [TITLE_OPTIMIZATION_BACKLOG.md](TITLE_OPTIMIZATION_BACKLOG.md) and is excluded. Do not add game-PC checks, asset/layout knowledge, sound-driver semantics or game-specific outcomes to engine code. Consumer changes are limited to supported regeneration, pins/build wiring and mechanical adoption of a generic interface; new title behavior or adapter logic stays deferred.

This list contains **36 engine candidate families**, split from the earlier42-family inventory. Several are parts or alternatives of the same coherent implementation. Priorities rank investigation, not proven savings. Dispositions below distinguish implemented work, shared existing work, rejected experiments and concrete deferrals; counts are not performance claims.

## Execution and acceptance

Current owner instruction (2026-10-28): work serially on one original goal. A validation-only subagent audits each result, requires a PR for a successful package or a Beads record for an unsuccessful one, and explicitly instructs the next task. Deferral, partial delivery and rejection of one mechanism do not exhaust the broader goal. Historical evaluation dispositions remain below; only the original implemented/qualified scope counts as implementation completion.

- Implement in isolated worktrees; the owner explicitly authorizes one independent validation-only subagent per serial task. Claim the remaining-work owner `beads-3z5k` before new implementation and update it and the campaign at meaningful milestones.
- Prioritize coherent replacement of the largest demonstrated repeated work across execution/memory/device or render/audio boundaries. Treat required observable behavior as the contract; existing subsystem interfaces and representations may be replaced.
- Evaluate every item. Record implemented/qualified, neutral, rejected, ruled out, or deferred with a concrete dependency/evidence. Keep evaluation disposition separate from implementation. After the 2026-10-28 audit, a checked item means its selected bounded contract is delivered, a whole bounded original opportunity is decisively rejected, or an existing terminal finding is verified. Partial delivery, deferral and rejection of only one mechanism remain unchecked; they do not close the broader original goal.
- Reuse existing evidence and supported QA. Obtain the smallest current measurement needed to choose a useful implementation, integrate promptly, and reassess after two unsuccessful experiments. Do not add unused infrastructure solely to mark an item complete.
- Accept net-neutral and net-positive outcomes per game. Fix/reject reproducible individual-game regressions; aggregate gains cannot offset them. Treat noisy small mixed results as neutral/inconclusive. Identical baseline/candidate failures are shared defects.
- Use matched built-game workloads/settings/features/hosts. Keep raw per-game samples, source/binary identities, package labels, CPU/frame and relevant startup/memory/delivery costs. Game consumers are validation fixtures; their own optimization backlog is outside scope.
- Keep correctness, current-build save/load/rewind/rollback, native saves, HLE/LLE contracts, required emulation/audio and portable fallbacks. Old save-state backward compatibility is not required. Gate optional assembly/SIMD and negotiate simulation-affecting capabilities once before netplay frame0.
- Keep automated launches muted and isolated. Do not wait indefinitely for quiet/stress benchmark hosts or hold all useful work for one timing slot. Preserve qualification gaps and make no unsupported performance claims.
- Finish with one coherent committed engine branch, a resolved burndown and retained evidence. Prepare representative normal-window/audio builds when owner validation is needed; require owner approval before merging/releasing a new engine batch. Keep one current delivered build per game when release is authorized.

## Implementation checklist (evaluation dispositions retained)

### Execution and memory

- [x] **E01: Native CD execution regions** â€” P1; original N01.
  - Goal: Run reusable Main/Sub/Z80 native regions until the next observable interaction or device deadline.
  - Boundary: Keep CPU ordering, interrupts, DMA, Word RAM ownership, raster and audio deadlines exact; no title PC/routine recognition.
  - Evidence: `runner/cd/segacd_coordinator.c`, `runner/cd/machine.c`.
  - Disposition/evidence: Implemented and qualified by engine `382678d` / core `34e5aee`: effect-qualified Main/Sub regions and continuous Z80 intervals preserve original CPU order, full live guards, actual hardware deadlines, bus/WAIT/HLE exits and completed-state publication. Final v5 built-game checkpoints/memory/pixels/full WAV/execution totals match the reference; MSVC/GCC region/fault/reentry contracts pass. Earlier fault-scope-only and v1/v2/v4 experiments remain rejected, with raw evidence. Final Windows SonicCD pairs show 3.1/8.7 percent lower process CPU and 8.6/7.2 percent fewer process cycles on the matched route; no universal FPS claim; the final balanced v5 pairs are retained separately from older variants. Binary/effect-storage costs recorded. See [ENGINE_NATIVE_REGIONS.md](ENGINE_NATIVE_REGIONS.md) and [engine-native-region-results.json](engine-native-region-results.json). Owner check/merge handoff remains; no other E-ID closed.

- [x] **E02: Code identity and invalidation** â€” P1; original N02.
  - Goal: Use guarded code regions with complete write/remapping invalidation instead of per-instruction byte comparisons where qualified.
  - Boundary: Cover mutable overlays and self-modification through generic memory ownership/versioning.
  - Evidence: `runner/cd/catalog_lookup.h`, `external/m68k-recomp-core/runtime/m68k_step.c`.
  - Disposition/evidence: Independent root review accepts immutable BIOS ownership (engine caadb72/PR60, core6ec41e8/PR11) for integration as neutral/inconclusive: first-use/range/serial/callback revocation and actual BIOS read/write mapping reviewed; dynamic0x72 hint vector excluded. 10MSVC+10GCC and four exact3000-frame CD arms pass; 11,467,628 real reuses. Whole-run CPU +1.60/+2.48percent and cycles +0.68/+0.90percent remain explicit accepted observations, with no speedup claim. Mutable PRG/Word/host RAM and opaque/unqualified owners retain measured-correct ordinary guards. Physical-page package c9db7a1/core5520820 and short exclusive intervals d0b3e17/core21169f0 remain rejected as separately retained history (the latter only53 actual reuses with both timing orders worse). Original bounded E02 contract resolved; no universal-impossibility claim. See [ENGINE_CODE_IDENTITY.md](ENGINE_CODE_IDENTITY.md), [results](engine-code-identity-results.json), beads-3z5k.1/beads-rxq.3. Owner combined play/merge/release gate remains; E36/E37 are the only open families.

- [x] **E03: Owned memory access** â€” P1; original N03.
  - Goal: Resolve generic RAM/ROM/Word RAM mappings once for safe regions; reduce repeated bus decoding and byte-order bookkeeping.
  - Boundary: Keep guest bytes, aliases, write protection, MMIO and ownership transfers coherent. Game-object redesign is deferred.
  - Evidence: `runner/video/genesis_bus.c`, `runner/cd/segacd.c`.
  - Disposition/evidence: Implemented generic operation-scoped owned data regions (engine899939b/coreb2d95f2) for qualified generated context-bus operations, with distinct read/write permissions, full live instruction guards, and exits across callbacks/MMIO/WAIT/protection/noncontiguous1M. Complete word-bus mapping is resolved once; cartridge path audited as already single-decode. Two independently found callback/aperture holes reproduced before fixes. 22MSVC+20GCC gates, current-build restore and four exact3000-frame game arms pass. Sonic CD timing is neutral/inconclusive: CPU reductions-3.99/+17.26percent,cycles-0.179/+18.60percent with recorded variation; no proven speedup. Original owned-data contract delivered; no claim cross-instruction designs exhausted. See [ENGINE_OWNED_MEMORY.md](ENGINE_OWNED_MEMORY.md), [results](engine-owned-memory-results.json), beads-3z5k.2/beads-rxq.4. PR/owner handoff pending.

- [x] **E04: Generated native arithmetic and state** â€” P2; original N04.
  - Goal: Improve the common 68000 emitter with local registers, proven flag liveness, combined effective addresses and grouped cycle accounting.
  - Boundary: Materialize exact architectural state at every observable exit; no game-specific algorithms.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Implemented common-emitter local D/A/SR spans, backward CCR liveness and exact grouped cycles, plus single-memory-RMW EA/read-write consolidation under complete managed dual ownership. Original bus/control/HLE/WAIT/fault/trace/unsupported exits remain exact. Engine132b84d/core4479e7e; final MSVC/GCC and all three built-game state/pixel/full-WAV routes and cartridge rollback pass. All final game timing neutral/inconclusive, no claimed speedup; r2/r3 regressions rejected, targeted reverse RKA off run does not reproduce the original24percent slowdown. Body proof131573 symbols/36089 redirects; current bytes and descriptor effects/priority unchanged. Cartridge observable bus limits documented. Independent final PR/record gate accepted; validator instructed E05 next. Owner visual handoff pending. See ENGINE_NATIVE_STATE.md, engine-native-state-results.json, beads-3z5k.3/beads-rxq.5.

- [x] **E05: Native call/control-flow consolidation** â€” P2; original N05.
  - Goal: Use proven direct call edges, inlining and larger native control-flow regions to reduce generic call/return/dispatch machinery.
  - Boundary: Preserve dynamic targets, guest stack skips, IRQ reentry and overlapping instruction streams; do not repeat rejected tail-index tuning unchanged.
  - Evidence: `external/m68k-recomp-core/profiles/genesis/code_generator.c`, `runner/glue.c`.
  - Disposition/evidence: Implemented inline complete resumable native-call frames, qualified native tail continuations, bounded same-TU callgraph/alias placement, and canonical live-guarded CD arithmetic regions ending in exact variable-charge pure control (including indirect JMP). Dynamic/RAM/override, nested skip, IRQ/restore and hook boundaries preserved; promoted-RAM qualification defect fixed. MSVC/GCC and all3 built-game gates pass; cartridge actual save/load/rollback and exact state/images/fullWAV match. Per-game performance neutral/inconclusive. Live CD whole pure-leaf folding has only0Main/19Sub upper-budget fits among849238calls and is rejected as unproductive on the measured route. Full inventory: ENGINE_NATIVE_CONTROL_FLOW.md and machine results. EnginePR39/corePR10 stacked onE04; independent final gate accepted and explicitly instructed fresh E06 verification next. Owner combined visual merge/release approval remains pending.

- [x] **E06: Reusable native Z80 backend** â€” P2; original N07.
  - Goal: Qualify and improve the common uploaded-code/variant executor and native blocks, with supported build integration.
  - Boundary: Keep reset, BUSREQ, IRQ, self-modification and timed writes. Driver catalogs, semantic SMPS HLE and game rollout are separate title work.
  - Evidence: `runner/z80_recomp.c`, `runner/cd/z80_native.c`, `docs/Z80_STATIC_RECOMP.md`.
  - Disposition/evidence: Fresh diligence repaired missing shared build integration and actual HALT/EI/IRQ/Windows-cycle/producer live-operand defects, then consolidated cartridge native scanline state publication while keeping each instruction guard, stamp, IRQ and fallback; trace retains per-step observations. Engine424a990/producerdc775df, PR40/producerPR3.11/11 MSVC+GCC adapter and2/2 CD targets each pass; supported RKA/S1 builds,16 exact matched runtime arms/full WAV/pixels/rollback/current-build saves and CD A/B plus9-input exact regeneration qualify the original bounded contract. All timing neutral/inconclusive, no claimed speedup. Independent validate_e06 accepted and explicitly instructed E07 next. Owner batch merge/release pending. See [ENGINE_Z80_BACKEND.md](ENGINE_Z80_BACKEND.md), [results](engine-z80-backend-results.json), beads-3z5k.5/beads-xecp.1.5.

- [x] **E07: Event-driven waits and scheduling** â€” P2; original N09.
  - Goal: Coalesce safe raster/fiber/CPU scheduling spans and handle proven waits through actual event wakeups.
  - Boundary: Do not bypass meaningful CPU work or add title-specific polling signatures.
  - Evidence: `runner/glue.c`, `runner/video/genesis_machine.c`, `runner/cd/segacd_coordinator.c`.
  - Disposition/evidence: Current serial candidate2079c51/PR41 uses real event deadlines and omits only redundant qualified pre-instruction clock publication.6/6 MSVC+GCC gates and4 exact3000-frame CD runs pass; actual service calls98,309,446to80,262,842(18.3569percent fewer), with identical72,191,897clock advances and16,241,534Z80 operations. Timing+7.14/-1.86percent CPU reductions is neutral/inconclusive, not a proven speedup. Full original-family inventory includes same-boundary game-fiber service (measured transition estimates0.3986/0.4583percent CPU in RKA/S1), bounded virtual WAIT attempts and qualified machine HALT (zero actual CD eligibility). Existing public/opaque/WAIT/third-master behavior is preserved. Independent validate_e07 FINAL ACCEPTED production2079c51/ready PR41 head98550e7, with no unresolved objective findings, and explicitly instructed fresh serial E08 next. Selected original bounded contract delivered; combined owner visual/merge/release handoff remains pending. See [ENGINE_EVENT_SCHEDULING.md](ENGINE_EVENT_SCHEDULING.md), [results](engine-event-scheduling-results.json), beads-3z5k.6.

- [x] **E08: Transfer spans and layout mapping** â€” P2; original N10.
  - Goal: Batch qualified DMA/VDP/CDC/PCM transfers and generic Word RAM cell mappings under one ownership decision.
  - Boundary: Preserve partial progress, contention, FIFO state, PCM lane ordering, status and completion clocks.
  - Evidence: `runner/cd/dma.c`, `runner/cd/segacd.c`, `runner/video/genesis_vdp.c`.
  - Disposition/evidence: Independently accepted engine11a55fd / ready PR42 review headf462b05: phase-only inactive DMA spans preserve the original post-callback ordering, all status fields and all clock/phase carry cases; native bit permutation covers all five cell layouts and both banks. Complete VDP/CDC/PCM/linear/cell/dot/stamp inventory has measured/source-supported dispositions, including possible no-new-ABI FIFO pair and ring-copy variants. 16MSVC+16GCC gates, 72,478phase/full-state cases, exhaustive cell byte/word cases and four exact3000-frame built-CD BAAB runs pass. Actual72,259,107full inactive-controller calls become phase spans; observed CD CPU reductions21.90/10.72percent and cycles18.73/8.13percent, AA drift-5.58/-6.78percent, shared-host variation retained. RKA/S1 structurally unaffected. No cell/startup/universalFPS gain claimed. See [ENGINE_TRANSFER_SPANS.md](ENGINE_TRANSFER_SPANS.md), [results](engine-transfer-span-results.json), beads-3z5k.7. Reviewer explicitly instructed E10 next; owner combined handoff pending.

### Graphics and rendering

- [x] **E09: Single image-production ownership** â€” P1; original N11.
  - Goal: Provide reusable lazy original-image production and exact fallback in the shared cartridge/CD raster pipeline.
  - Boundary: Retain CPU-visible sprite status and whole-frame fallback. Title scene eligibility and its adapter implementation belong in the title backlog. Do not land unused abstraction merely to check this item off.
  - Evidence: `runner/cd/machine.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Shared CD immutable raster inputs and lazy exact fallback16c14e6 pass final Windows/Linux3000-frame state/pixel/WAV gates.720000rows captured and442800composed:38.5percent original compositions omitted on this route, with898600retained bytes. CPU-visible hardware sprite status remains at its original deadline; per-row CRAM survives fallback. Cartridge already had lazy original rows. Omitted work is not a whole-game FPS claim.

- [x] **E10: Renderer interface independent of aspect ratio** â€” P1; original N12.
  - Goal: Separate shared renderer ownership/selection from viewport width and elective widescreen policy.
  - Boundary: Keep existing consumers compatible. Stock sprite/scene fidelity and enabling a particular game's custom renderer remain title tasks.
  - Evidence: `runner/main.c`, `runner/game_video.h`.
  - Disposition/evidence: Independently accepted production96be781/readyPR43 reviewed a75cd9f and mechanicalSonic1pin6ecb59e/PR7. Shared frame plan separates row ownership, logical viewport and local output; width publishes before selection, legacy/eager/lazy/native/overlay and renderer-only controls remain qualified. Any local limit below the logical request chooses exact native fallback, closing independently found supported-clamp coupling for all legacy consumers without title logic changes. 7MSVC+7GCC+4consumer gates, exact RKA/S1 built state/fullWAV/pixels/rollback/current-state evidence, actual nativeownerH32/H40 and supported360=>native320/request398 pass. All precursor/final source identities and harness corrections retained; timing neutral/inconclusive, no gain claim. See [ENGINE_RENDERER_OWNERSHIP.md](ENGINE_RENDERER_OWNERSHIP.md), [results](engine-renderer-ownership-results.json), beads-3z5k.8. Reviewer explicitly instructed E11 next; combined owner merge/release handoff pending.

- [x] **E11: Fused generic VDP raster** â€” P1; original N14.
  - Goal: Combine tile/window/scroll spans, decode, priority and shadow/highlight composition in the shared renderer.
  - Boundary: Existing BATCHED fetching is partial work, not a new gain. Preserve all supported VDP modes and the universal reference path.
  - Evidence: `runner/video/genesis_vdp.c`.
  - Disposition/evidence: Independent validate_e11 FINAL ACCEPTED complete bounded fusion rejection plus useful existingBATCHED default adoption/provider fixes, production50b0854/readyPR44 reviewedc405c7e. Full fusion96decaf/9337e36/36d5c18 is implemented, qualified and reached, then rejected versus bestexistingBATCHED: RKA incremental neutral, S1reproducibly worse. No partial/deferral or universal algorithm exhaustion claim. Final nativedefaultBAAB RKA CPU29.86/30.33percent lower/cycles29.21/30.09percent lower from REUSE/DEFAULT ADOPTION; S1neutral/inconclusive. All final state/fullWAV/pixels/rollback/currentstates and3/3 eachMSVCx64/actualx86/GCC pass. Complete inventory/rejectedvariants/census/raw/host/PE identities in [ENGINE_RASTER_FUSION.md](ENGINE_RASTER_FUSION.md) and [results](engine-raster-fusion-results.json), beads-3z5k.9.22familiesopen; reviewer explicitly instructed E13 next with freshvalidator. Combined owner handoff pending.

- [x] **E12: Shared sprite evaluation jobs** â€” P2; original N15.
  - Goal: Prepare reusable generic sprite membership/opacity/priority work for both hardware status and image production.
  - Boundary: Preserve limits, masks, raster-visible changes, overflow and collision timing; no replacement of game BuildSprites.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Hardware sprite capture occurs once per CD raster row; deferred fallback reuses captured lanes and never repeats sprite evaluation. Status, limits, priority and per-row palette remain exact in synthetic full-frame/provider contracts and final Windows/Linux3000-frame memory/pixel/WAV gates. No title SAT/scene policy changed. Further sprite caches are separately deferred under E13/E15.

- [x] **E13: Versioned generic graphics derivation** â€” P2; original N16.
  - Goal: Cache decoded VDP art, palettes and geometry only at verified VRAM/CRAM/register change boundaries.
  - Boundary: All writer paths must invalidate or take the existing fallback. Game layout, object and private-art caches are separate.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`, `runner/video/genesis_bus.c`.
  - Disposition/evidence: Fresh validate_e13 FINAL ACCEPTED complete bounded art/palette/geometry architecture evaluation/rejection and useful immutableDAC simplification, production999f392/readyPR45 reviewed5576577. Whole1d architecture correctlyimplemented/qualified/actuallyused with highrealcachehits, butno demonstratedgain overbestexistingBATCHED, so mutableart/geometrycaches rejected/removed; VDP source restoredexactbase. Finite512x3DACdomain andsupportedall65536CRAMx6oracle retained, identicalplatformqualifiedcode.4/4 eachMSVCx64/actualx86/GCC initialgates, finalexactRKA/S1 native/custom/state/rollback/fullWAV andCDcheckpoints/RAM/WAV pass; allpergametimingneutral/inconclusive, finalS1negativeAB reversesfocusedBA. Allsource/PE/raw/census/metadata corrections retained; no partial/deferral/allfuturecacheexhaustion orspeedupclaim. See [ENGINE_GRAPHICS_DERIVATION.md](ENGINE_GRAPHICS_DERIVATION.md), [results](engine-graphics-derivation-results.json), beads-3z5k.10.21familiesopen, reviewer explicitly instructed freshserialE14; combinedownerhandoffpending.

- [x] **E14: Native Sega CD stamp-device jobs** â€” P2; original N18.
  - Goal: Optimize the generic affine/stamp device in exact spans/blocks.
  - Boundary: Preserve Word RAM writes, priority, partial completion and IRQs. No Blue Spheres or other game-effect redesign.
  - Evidence: `runner/cd/stamp.c`.
  - Disposition/evidence: Freshvalidate_e14 FINAL ACCEPTED complete originalboundedopportunity evaluation/rejection. R1ec9 geometryjobs/cursor andR2d0 all8fixedmode exactcellspans fullyimplemented/qualified6/6eachx64/Win32/GCC/reachedactual400stampjobs perSS1window. All8gamearms fullRAM/state/pixels/WAVexact; R1repeatedadversecost andR2no gain/adverseCPU, bothrejected; originalstamprestoredbyteexact. UsefulWin32size_tguardfix+3080joboracle retained, production452f52a/readyPR46 reviewed8029d30. No newgain/partialdeferral/universalfutureexhaustionclaim. See [ENGINE_STAMP_JOBS.md](ENGINE_STAMP_JOBS.md), [results](engine-stamp-jobs-results.json), [independentverdict](ENGINE_STAMP_JOBS_REVIEW.md), beads-3z5k.11.20originalfamiliesopen; freshserialE15next aftercleanup/sync. Combinedownerhandoffpending.

- [x] **E15: Reuse unchanged hardware render results** â€” P3; original N19.
  - Goal: Reuse generic render jobs/regions/images when the complete hardware input identity is unchanged.
  - Boundary: Continue guest execution, status and audio; track every palette/VRAM/scroll change. Title-specific image eligibility is deferred.
  - Evidence: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`.
  - Disposition/evidence: Fresh validate_e15_resumed accepted the complete original bounded opportunity evaluation/rejection. R1 native/capture reuse repeated RKA adverse cost in both orders; R2 captured-plane-only lazy ownership reached496192/616816 real CD capture hits but showed no useful gain on PC/SERVER and added about2MiB. Both implementations fully qualified; no partial/deferral or universal future-algorithm exhaustion claim. All runner sources restored byte exact426253e; useful MSVC19.29 generated-layout fix plus actual emitted-C compile/value regression fixture retained, productiona20ecc5/readyPR47. Exact per-game state/pixels/full WAV, cartridge current-build save/load/rollback and CD11checkpoints pass; small mixed costs neutral/inconclusive, no new speedup claim. See [ENGINE_RENDER_REUSE.md](ENGINE_RENDER_REUSE.md), [results](engine-render-reuse-results.json), beads-3z5k.12.19originalfamiliesopen; reviewer explicitly instructed fresh serial E16 after bookkeeping/sync. Combined owner handoff pending.

- [x] **E16: Final pixels and upload ownership** â€” P2; original N20.
  - Goal: Fuse shared indexed/ARGB conversion, copies, color processing, UI staging and upload preparation.
  - Boundary: Keep raw verification/capture output, filter edges, UI and portable output paths correct.
  - Evidence: `runner/main.c`, `runner/cd/presentation.c`.
  - Disposition/evidence: Whole bounded shared indexed/raw/color/UI/upload ownership delivered in ea16061/ready PR #48 and independently accepted. Actual indexed destination delivery, exact inverse-DAC lookup and synchronous staging replace repeated work across main/CD. All platform/frozen-original/game/current-state/actualSDL gates pass. CRT CPU observations RKA 56.48% and Sonic 1 66.26% lower; raw/default and explicitly mixed CD neutral/inconclusive, substantial adverse CD samples retained, no proven universal CD nonregression or speedup. See ENGINE_FINAL_PIXELS.md, engine-final-pixels-results.json and immutable final review. 18 original families remain open; combined owner visual/play handoff pending.

- [x] **E17: Optional generic GPU raster backend** - P3; original N21.
  - Goal: Evaluate hardware tile/sprite/priority composition from reusable immutable render packets.
  - Boundary: Preserve CPU-visible status/effects, exact raw output/history and complete portable CPU fallback; no per-title adapters.
  - Disposition/evidence: Both expanded-lane and compact-pattern raw GPU architectures implemented, built, physically correct and used by actual native games. Corrected sparse preview ownership removed initial readback costs. Eleven matched game pairs retain all source/binary/config/host/raw costs and exact state/pixels/full WAV/rollback. Expanded neutral/inconclusive without useful gain; compact repeatedly adverse RKA wall/cycles. Entire selected bounded opportunity rejected; all production and build/test wiring restored byte-exact c94. Fresh independent gate accepted full scope/evidence/Beads/cleanup; no PR for discarded infrastructure. See [ENGINE_GPU_RASTER.md](ENGINE_GPU_RASTER.md), [results](engine-gpu-raster-results.json), [review](ENGINE_GPU_RASTER_REVIEW.md), beads-3z5k.14. No all-future-GPU impossibility claim. Explicit next E18 with fresh validator after bookkeeping/sync.

- [x] **E18: Independent engine work scheduling** â€” P3; original N22.
  - Goal: Evaluate workers for immutable rendering/audio spans and safe overlap of preparation/upload.
  - Boundary: No guest-state races, reordered observable events or required extra-frame latency; keep single-core/unsupported-host operation.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/audio.c`.
  - Disposition/evidence: Three independent worker architectures implemented and evaluated. Final-color/audio overlap and independent FM/PSG jobs rejected and removed. Same-drain opted-in FM shadow retained with automatic available worker, exact serial/aliased-output fallback and join before verification/state boundaries. Full independent gate accepted:15 distinct matched native pairs/30 runs, exact state/raw/full WAV/rollback, six final Win64/Win32 unit passes, actual cost/source/PR/Beads/cleanup evidence. RKA wall observations beneficial; Sonic1 overall mixed/inconclusive; increased CPU/cycles explicit, no universal nonregression/energy claim. Ready PR #50, beads-3z5k.15; see ENGINE_WORKERS.md, engine-workers-results.json and immutable ENGINE_WORKERS_REVIEW.md. Original16 plus E37 =17 open. Explicit next E19 with fresh validator after bookkeeping/sync.

### Audio

- [x] **E19: One ordered audio timeline** â€” P2; original N23.
  - Goal: Consolidate engine timestamps, stable ordering, register pairing and deferred DAC-stream representation.
  - Boundary: Preserve cross-source timing, long bursts and exact paired writes; do not implement game sound-driver semantics.
  - Evidence: `runner/audio/mixer.c`, `runner/audio/event_queue.c`, `runner/cd/audio_native.c`.
  - Disposition/evidence: Existing delivered cartridge ordering/pairguard remains unchanged. Entire selected remaining timestamp/deferred-stream opportunity implemented and evaluated: two owned cartridge interval layouts, simpler flat/bulk/common-kernel reassessment and CD cursor/stable-sort pipeline.16 distinct matched pairs/32 real runs, all outputs exact; repeated supported Sonic1 shadow and CD CPU/cycle regressions reject new packages, RKA gains cannot offset. Approx2MiB owned memory cost retained; flat removed it but still regressed. All production/build/test wiring restored byteexact5d; no PR for discarded code. Full independent source/runtime/cost/caller/fallback/Beads/cleanup gate accepted. See ENGINE_AUDIO_TIMELINE.md, engine-audio-timeline-results.json, immutable review and beads-3z5k.16. Original15 plus E37=16 open; next originalE20 with new validator after bookkeeping/sync.

- [x] **E20: Faithful FM kernels** â€” P1; original N24.
  - Goal: Optimize actual YM2612 operator/channel kernels beyond the already-shipped sample-block wrapper.
  - Boundary: Keep envelopes, feedback, LFO, DAC, filtering, arbitrary register writes and state exact; retain portable kernels.
  - Evidence: `runner/audio/ym2612_ymfm.cpp`, `runner/external/ymfm/src/ymfm_fm.ipp`.
  - Disposition/evidence: Whole selected bounded opportunity rejected after actual implementation of fixed OPN2 routes5e42063 and compact generic channel scan4adfc38. Four independent frozen-vendor/wrapper native/fallback tests pass;18 matched cartridge pairs and2 CD pairs preserve all actual outputs/state/current cartridge rollback. Global variants fail per-game acceptance; compact Sonic1 shadowoff repeatedly worsens cycles/wall despite shadowon gains. CD CPU/cycles are neutral/inconclusive across both orders, so no CD-only compiler/profile selection is retained or proven8percent gain claimed. Production/test/build restored exactly by02c0fa8 to10ace05. No new runtime/PR; prototypes and raw samples preserved. See ENGINE_FM_KERNELS.md, engine-fm-kernel-results.json, beads-3z5k.18. Evidence-backed rejection, not deferral or every-future-algorithm impossibility. Independent whole source/record/cleanup gate accepted, owning issue closed and central push12.4s exit0. See ENGINE_FM_KERNELS_REVIEW.md and engine-fm-kernel-review.json. Validator explicitly instructed original E21 next.

- [x] **E21: PSG event spans** â€” P3; original N25.
  - Goal: Generate eligible tone/noise runs in blocks.
  - Boundary: Advance counters, LFSR, phase and filters exactly; historical whole-game cost is small.
  - Evidence: `runner/audio/sn76489.c`.
  - Disposition/evidence: Whole selected bounded opportunity rejected after active constant-run11e2bac and whole-interval input/filter/timingb95058e architectures were implemented and measured. Six independent frozen-scalar native/fallback checks pass;17cart+3CD pairs preserve complete recorded audio/pixels/state/current cartridge rollback. Both show repeated adverse Sonic1 shadow costs; RKA and CD mixed observations cannot offset them, and whole-interval CD costs are adverse. Production/test/build restored exactly byd96ca6b tof4b10d7; existing settled-muted spans unchanged, no new runtime/PR. See ENGINE_PSG_EVENT_SPANS.md, engine-psg-event-span-results.json and beads-3z5k.19. Evidence-backed rejection, not deferral or all-future-design impossibility. Independent final source/record/cleanup gate accepted; owning issue closed and central push16.7s exit0. See ENGINE_PSG_EVENT_SPANS_REVIEW.md and engine-psg-event-span-review.json. Next original E22 after evidence-copy verification; E25 PCM/CDDA constant spans remain open.

- [x] **E22: Sega CD PCM/CDDA spans** â€” P2; original N26.
  - Goal: Advance generic PCM channels, CDDA/fader and queues in timed blocks instead of per-sample callbacks.
  - Boundary: Preserve wave RAM writes, loops, seek/pause, sample boundaries, underruns and status.
  - Evidence: `runner/cd/rf5c164.c`, `runner/cd/cdda.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: Qualified synchronous PCM/CDDA source-to-queue ownership at a8f45ad, including actual single-sample cadence. Frozen complete-state/sample/queue and native binding/failure checks pass x64/Win32; four matched Sonic CD pairs preserve11 checkpoints,21 artifacts/full WAV and prove both paths active. Performance is neutral/inconclusive with lower CPU observations; the adverse reverse pair remains explicit. PR51, beads-3z5k.20. Independent source/records/cleanup gate accepted; central durability precedes E23. Combined owner handoff pending; E23/E25 remain open. See ENGINE_CD_AUDIO_SPANS.md, engine-cd-audio-span-results.json and ENGINE_CD_AUDIO_SPANS_REVIEW.md.

- [x] **E23: Synthesis-to-device pipeline** â€” P1; original N27.
  - Goal: Combine generic chip staging, multi-source mixing, interpolation, filters and device-rate conversion.
  - Boundary: Maintain required samples, DRC, phase, latency and capture contracts; no quality reduction or disabled synthesis.
  - Evidence: `runner/audio.c`, `runner/audio/mixer.c`, `runner/cd/audio_native.c`, `runner/cd/audio_mix.c`.
  - Disposition/evidence: Whole selected bounded pipeline opportunity rejected after coherent owned generated-frame/CD interpolation plus gain/float-ring publication and generic unity-bypass correction. Five focused contracts/ten passing x64 executions and20 actual game runs preserve recorded output, including nonzero host samples; RKA default cycle costs remain adverse/unresolved across paired orders and final balanced check. Other games do not offset. Complete production/test/build tree restored exactly to acceptedfe668 by4191f56; allsixSERVERoverlays byte-exact and originalcachedgraph rebuilt. No new runtime/PR. See ENGINE_AUDIO_DEVICE_PIPELINE.md, engine-audio-device-pipeline-results.json, review and beads-3z5k.21. Independent source/records/cleanup gate accepted; durable sync/reviewer instruction precedes E24. Future algorithms not claimed impossible.

- [x] **E24: Audio buffer ownership** ? P3; original N28.
  - Goal: Reduce avoidable callback handoff locks, copies and temporary storage.
  - Boundary: Preserve recovery/concealment, race-free callbacks and device deadlines; distinguish paced delivery from throughput.
  - Evidence: `runner/audio.c`, `runner/recomp_audio_drc.h`, `runner/cd/audio_native.c`.
  - Disposition/evidence: Qualified CD frame-slot ownership and immutable normalized F32 publication with conservative original-push fallback; callback/controller lock retained, racy telemetry snapshot fixed. Five focused contracts pass x64/Win32;16 actual game runs/8pairs preserve recorded correctness including actual WASAPI/reverse. Initial adverse RKA observations do not reproduce; whole-game neutral/inconclusive, component reduction separately measured,128KiB extra scratch explicit. PR52, beads-3z5k.22, ENGINE_AUDIO_BUFFER_OWNERSHIP.md and engine-audio-buffer-ownership-results.json. Final source/records/cleanup durability review precedes E25; combined owner visual/play/merge remains. Not a claim that every future lock-free algorithm is exhausted.

- [x] **E25: Exact constant/silent spans** ? P3; original N29.
  - Goal: Represent provably constant output efficiently without per-sample queue work where valid.
  - Boundary: Timers, phase, filters, fader and future sample state must still evolve exactly.
  - Evidence: `runner/cd/audio_owned_span.h`, `runner/cd/audio_mix.c`, `runner/cd/cdda.c`, `runner/cd/rf5c164.c`.
  - Disposition/evidence: Qualified compressed rational source runs, logical queue observer and constant-window rendering; every logical timestamp/sample/counter/capacity and future state preserved, physical/opaque fallback retained. Five contracts pass x64/Win32;4 real CD runs/2 matched SERVER pairs preserve11checkpoints/21artifacts/full WAV. Observed Sonic CDcycles2.24?3.86% lower andCPU0?3.61% lower; no universal FPS/RAM gain. Existing affinity/headroom retained and timing scope distinguished from frozen correctness. PSG/E21 unchanged. PR53, beads-3z5k.23, ENGINE_SOURCE_CONSTANT_SPANS.md and engine-source-constant-span-results.json. Final consolidated source/record/cleanup/durable gate precedes E26; combined owner handoff remains.

### Generic media and console firmware

- [x] **E26: Bounded generic media prefetch** â€” P2; original N32.
  - Goal: Read upcoming raw CDDA/movie/nonresident sectors off the simulation path using actual device requests/transport information.
  - Boundary: Bound memory and preserve emulated delivery, seeks, cancellation, errors and synchronous fallback; no title preload tables.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/cdd.c`, `runner/cd/cdc.c`.
  - Disposition/evidence: Implemented/qualified f88e90b bounded async raw-media windows using actual adjacent requests, same-verified-file positioned I/O, two512-sector buffers, cancellation and exact synchronous fallback. Source/byte/lifetime/error gates pass x64 and Win32 (shared baseline Win32 media assertion retained); six actualCD arms match11checkpoints/21artifacts/fullWAV. Four timed arms in BA/AB pairs show observedCPU3.17/4.68percent and cycles2.59/6.69percent lower, with ambient variation. Synchronous reads2139 become10/12 plus2129/2127hits;6jobs/no failures. Lazy2,408,448B plusworker cost; startupwallmixed, no startup/cold-disk/universalFPS claim. See [ENGINE_MEDIA_PREFETCH.md](ENGINE_MEDIA_PREFETCH.md), [results](engine-media-prefetch-results.json), beads-3z5k.24, PR54. Combined owner handoff remains.

- [x] **E27: Verified media handoff contract** ? P1; original N33.
  - Goal: Provide a safe common way to reuse completed media verification within one launch instead of independent redundant payload reads.
  - Boundary: Actual complete identity every launch, immutable ownership/no TOCTOU and portable full validation. Mechanical consumer adoption is engine scope; converter/cache redesign stays in T12.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/standalone.c` and Sonic CD cached-CUE launchers.
  - Disposition/evidence: Full expected-image contract2276caa verifies actual whole/payload in one pass on owned storage, with complete provider restart/fallback; mechanical consumer adoption actually used. Six Windows plus two Linux3000-frame runs preserve11checkpoints/21artifacts/fullWAV; Windows loading CPU6.16?10.02percent lower in the bounded same-affinity scope, gameplay neutral/inconclusive; Linux neutral with436.64MiB added peakRSS. File snapshot preferred path rejected as slower, RAM path qualified. Win64 12/12, Win32 11/12 with identical baseline media assertion (retained individual log), Linux/provider and real launcher gates pass. Ready enginePR55 and SonicCDPR2; beads-3z5k.25; ENGINE_VERIFIED_MEDIA_HANDOFF.md/results. Combined owner handoff remains.

- [x] **E28: Portable full-media validation** â€” P2; original N34.
  - Goal: Evaluate accelerated portable/platform SHA and safe immutable-sector validation reuse.
  - Boundary: Windows CNG already ships. Keep complete payload checks, corruption detection, CPU/OS gating and full portable restart/fallback.
  - Evidence: `runner/cd/disc_image.c`, `runner/cd/sector.c`.
  - Disposition/evidence: Optional OpenSSL full-payload validation outside Windows, retained Windows CNG and universal C fallback pass provider-missing/forced-portable and injected init/update/final failure checks. Final combined Windows/Linux game gates pass. Earlier six balanced Linux launch observations show12.4/13.8percent less wall and40.2/41.1percent less processCPU (A/A drift2.0/1.7percent), bounded shared-host evidence, not FPS. Selected Linux system libcrypto dependency is recorded; Windows review build needs no added crypto DLL. Catalog-only startup observations are neutral/inconclusive and are not added to these gains.

- [x] **E29: Resident-media representation** â€” P2; original N35.
  - Goal: Evaluate lower-copy/raw-versus-user-data residency, bounded mapped/shared storage and memory footprint.
  - Boundary: Preserve generic raw/audio/movie/sector reader contracts and allocation fallback. Memory savings are distinct from speedups.
  - Evidence: `runner/cd/disc_image.c`.
  - Disposition/evidence: Read-only mapped first-data-track residency96a2e2f shares bytes verified during mandatory full payload hashing; the256MiB cap and copied fallback remain. Windows/Linux full game proof and forced mapping-failure checks pass. Current mappedBytes139735888/privateResidentBytes0 removes the separate private copy; mapping length is not an RSS saving claim. Catalog-only matched Linux RSS samples are retained separately with their raw process status.

- [x] **E30: Reusable console-firmware services** â€” P2; original N36.
  - Goal: Assess closed standard Sega CD BIOS services that can be replaced once for supported firmware profiles across games.
  - Boundary: Require a real reusable firmware ABI and complete state/BRAM/UI/IRQ behavior. Game SP/file-engine routines and game-PC exceptions are deferred; do not generalize a title handler merely by moving it into the engine.
  - Evidence: `runner/cd/segacd_coordinator.c`, `runner/cd/standalone_program.c`.
  - Disposition/evidence: Qualified native BRMINIT protection-table operation f13bf7c replaces the quadratic sort with direct finite-field tables and complete guarded return/stack/state behavior, including PRG and owned Word RAM. Full57-service bounded remainder assessment retains required drive/control/native-save semantics. Actual normal BIOS menu formatting/reboot/native-save preservation and Windows/Linux gates pass. Sonic CD gameplay appears34frames (~0.57s) earlier; host CPU/startup neutral/inconclusive. ReadyPR56; [complete family evidence](ENGINE_FIRMWARE_SERVICES.md), [raw results](engine-firmware-services-results.json). Combined owner handoff remains.

### State and replay

- [x] **E31: Engine snapshot representation and identity** â€” P2; original N37.
  - Goal: Evaluate generic authoritative/derived state separation, dirty pages/deltas/checkpoints and exact incremental state identity.
  - Boundary: Cover all writes and restore/replay behavior. Title-owned snapshot payload reduction is separate; do not repeat the already-shipped checksum change.
  - Evidence: `runner/rb_state.c`, `runner/frame_snapshots.c`.
  - Disposition/evidence: Full bounded original opportunity rejected after implementing section-only identity, exact incremental prefix identity and actual writer-complete checkpoint/page-delta rewind with compact bases, lazy ownership and economic fallback. Correctness/branch/ownership gates pass; final production RKA cycles+1.610/+2.040percent and CPU+1.128/+2.256percent in opposite-order pairs disqualify the memory-saving package. Sonic1 mixed timing is neutral/inconclusive. Authoritative/derived audit found no independent large removable image; old full snapshot/digest code restored exactly71b19b0. No retained runtime PR. Complete evidence: [ENGINE_SNAPSHOT_REPRESENTATION.md](ENGINE_SNAPSHOT_REPRESENTATION.md), [results](engine-snapshot-representation-results.json), beads-3z5k.27. E32 remains open; future algorithms are not claimed impossible.

- [x] **E32: Serializer and snapshot storage** â€” P3; original N38.
  - Goal: Reuse bounded buffers, serialize into owned storage and reduce generic snapshot allocation/copy/thumbnail work.
  - Boundary: Keep rewind cadence, atomic save replacement and invalid-input rejection; old save-state compatibility is not required.
  - Evidence: `runner/local_states.c`, `runner/rb_state.c`.
  - Disposition/evidence: Qualified shared direct capture for BOTHrewind/netplay with existing transactional rbenginePR2/b29bdde, safe failed growth/shrink/save/publication and bounded4KiBcapacityslack; generic optional hostsaveCRC uses64KiBchunk insteadsecondfullbody. ActualRKA/S1normal/branch exactpixels/fullWAV/state/rollback,3/3MSVC+3/3Linux and independentownership/CRC gates pass. Timingneutral/inconclusive; S1peakcommit+3?3.5MBcost explicit, noFPS/memoryreductionclaim. RKA nativefilepairpasses; identicalS1coldload+probecrash inbaseline/candidate remains preexistingfailedroute/nondisqualifying. Runtime717565c, readyPR57; requestedTechnicallyComputersassignment blockedbyGitHubrepoeligibility, recordedforhandoff. Full bounded family disposition and sourcecost audit: [ENGINE_SNAPSHOT_STORAGE.md](ENGINE_SNAPSHOT_STORAGE.md), [results](engine-snapshot-storage-results.json), beads-3z5k.28. Owner combinedhandoff remains.

- [x] **E33: Discarded-replay engine work** â€” P2; original N39.
  - Goal: Remove remaining purely derived shared rendering/publication preparation from replay that produces no visible output.
  - Boundary: Continue all authoritative game/device/audio state. Title-specific capture changes require separate work and cannot be silently skipped.
  - Evidence: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/main.c`.
  - Disposition/evidence: Additional shared cartridge replay image work ruled out at this base: s_resim_hooks has no sink/producer/post-drain observation; machine_run_frame_ex only evaluates sprite status on discarded rows; genesis_sim_step continues chip evolution. Current Sonic1 rollback probes pass. Remaining pre-raster work sets simulation-affecting viewport/RAM parameters and cannot be removed. Title publication algorithms remain outside scope; new CD deferred work is tracked under E09.

### Host and built program

- [x] **E34: Host presentation/deadline pipeline** â€” P2; original N40.
  - Goal: Consolidate shared pacing, upload/present queue behavior and remaining compositor/CD coarse-wait costs.
  - Boundary: Reuse existing D3D11/deadline work and beads-hj4v; keep NTSC audio cadence and report delivery jitter separately from throughput.
  - Evidence: `runner/main.c`, `runner/cd/standalone.c`, `runner/cd/presentation.c`, `runner/host_wait.c`.
  - Disposition/evidence: Qualified exactrationalCDdeadlineownership afterupload/renderpreparation immediatelybeforePresent, fullperiodoverrun/pause reset and nonspinningnative/portablepark; existingcartridgeD3D11/deadline/spinpolicyandE16raw/UI/uploadownership preserved. Runtime81f7682/readyPR58. ActualpacedCDmatched11checkpoints/21artifacts/fullWAV/audio/counters exact;3/3MSVC+3/3Linuxclock/waitgates pass. Observedsubmitmedian31.1494 to16.79145ms onHEAVILYcontendedSERVER, withactualsoftware/dummybackend/blocking andall3000Bdeadlineshonored; noisolatedeffectsize/physicaldisplay/FPSclaim, CPUneutral/inconclusive. Independentfullscopeaccepted. See [ENGINE_HOST_PRESENTATION.md](ENGINE_HOST_PRESENTATION.md), [results](engine-host-presentation-results.json), beads-3z5k.29/relatedbeads-hj4v. Ownercombinedhandoff remains.

- [x] **E35: Asynchronous engine persistence and diagnostics** â€” P3; original N41.
  - Goal: Buffer immutable save/evidence/capture data and use bounded rings for enabled tracing instead of hot-path file I/O.
  - Boundary: Keep transactional native saves, diagnostics and clean shutdown. Default yield logging is already opt-in; reuse beads-3vb.11.
  - Evidence: `runner/runtime_evidence.c`, `runner/local_states.c`, `runner/glue.c`.
  - Disposition/evidence: Complete12-writer bounded qualification:5 immutable PCM/evidence/native-persistence owners retained;7 original text owners restored after per-stream/shared-worker/hybrid integrated capture regressions, not deferred. Runtimeccf939f, readyPR59, beads-3z5k.30. Final16/16MSVC+16/16GCC; all3actualcartridge default/all-capture state/raw/fullaudio/rollback/enabledfiles exact, including real periodic16KiBSRAM commits. Exact linkedCDidentity reuses actual controller BIOSformat/dirty8192B BRAM+WAV gate. Evidence producer-stall component observation18.697ms to0.086ms publication, durablecompletion8.559ms, not a gameFPS estimate. Default RKA/S1neutral-inconclusive; S3defaultINCONCLUSIVE with initial+10.38%CPU/+12.46%cycles preserved and nonrepeatingCPUoutlier. Captures show observedRKA/S3benefit and mixedS1, no isolatedeffectsize/offsetclaim. Native authority/scriptedlastframe defects fixed; transactional progress, cold boundaries, fallback and shutdown preserved. Independent full-scope technical acceptance; ownercombinedhandoff remains. [ENGINE_ASYNC_PERSISTENCE.md](ENGINE_ASYNC_PERSISTENCE.md), [results](engine-async-persistence-results.json).

- [ ] **E36: Whole-program native build optimization** â€” P2; original N42.
  - Goal: Evaluate common LTO/PGO, cross-TU inlining, hot/cold layout and qualified optional kernels on real built consumers.
  - Boundary: Keep arithmetic precise, public build portability and fixed qualified netplay capabilities; supported consumer build wiring is allowed, title logic is not.
  - Evidence: `cmake`, `external/m68k-recomp-core/profiles/genesis/code_generator.c`.
  - Disposition/evidence: Whole-program generated catalog canonicalization45b4805 is qualified on the current CD consumer without fast math, new ISA or a new runtime ABI. Ordinary linking drops duplicate metadata and unneeded operation objects: Windows142430208 to109482496bytes, unstripped Linux544293880 to489968336bytes. Loaded data.rel.ro falls102851160 to71537280bytes; relocation table61718808 to42927408bytes. Both platform game gates pass; instruction retirement falls3.732percent but runtime timing is inconclusive. Supported source pipeline enables compaction by default, retains original-registry fallback on unsupported formats and offers --no-canonicalize-catalogs. LTO/PGO remain deferred pending matched built-game benefit; no new unused compiler switch introduced.

## Coherent implementation packages

Use packages to reduce repeated work rather than treating every row as a separate patch. Effects overlap.

- Native execution/memory/device ownership: E01â€“E08; firmware E30 only where its reusable ABI is established.
- Single graphics producer and reusable raster data: E09â€“E16. GPU/worker alternatives E17â€“E18 require evidence that they repay their complexity.
- Ordered audio through device output: E19â€“E25.
- Verified media and bounded residency: E26â€“E29.
- State representation and discarded replay: E31â€“E33.
- Host delivery/I/O/build behavior: E34â€“E36.

## Already delivered and previous experiments

Do not count already-shipped lazy cartridge raster, tile spans, FM sample-block wrapper, host deadline waits, full-byte checksum change, discarded-plane replay work, CD descriptor lookup cache, or CD residency/CNG/ReadSectors as new gains. Backend choices vary by title; the historical Sonic1 SCALAR16:9 result does not establish the final package or another game's effect size.

Unchanged negative/neutral variants are not queued again: production bus-history removal, narrow plane/name/geometry/pattern caches, algebraic palette packing, Z80 low-RAM fast reads, conditional sprite clears, static tail indices and streaming digest of the same large payload. Revisit a mechanism only with materially new attribution or a broader design. Existing BATCHED selection needs package-specific qualification rather than being presented as a new implementation.

Historical evidence: [PERFORMANCE.md](PERFORMANCE.md), [NATIVE_OPTIMIZATION_BURNDOWN.md](NATIVE_OPTIMIZATION_BURNDOWN.md), [native-optimization-opportunities.json](native-optimization-opportunities.json), [Z80_STATIC_RECOMP.md](Z80_STATIC_RECOMP.md), [ENGINE_VDP_BATCHING.md](ENGINE_VDP_BATCHING.md). Raw audit inventory retained at `C:/Temp/Codex/sonic-cd-loading-20261027/remaining-candidates.json`; owning Beads metadata retains the machine-readable full list.

## Evaluation record

For each E-ID, record the chosen package and implementation/reference heads, built-game evidence, per-game positive/neutral/regression/inconclusive outcome, correctness, material costs and decision in `beads-3vb.31` and the campaign. Keep untested and deferred entries explicit. Do not close the implementation owner merely because this document exists.

## Historical evaluation/release batch (2026-10-28)

All36 families received evaluation dispositions; this does not close the implementation burndown: {'rejected': 1, 'implemented_or_partial': 14, 'deferred': 19, 'existing': 2}. Implemented/partial counts overlap; they are not independent speed wins. Deferred items remain future opportunities. The title backlog stays excluded.

Sonic1 stock/wide2400-frame, RKA stock/wide6000-frame and SonicCD Windows plus four Linux3000-frame checks preserve full state, pixels and WAV; cartridge replay checks and Sonic1 applied-audio trace also match. Raw per-game packages, source/binary identities, settings, costs and evidence are in [engine-optimization-results.json](engine-optimization-results.json).

The catalog fixture reduces Windows executable size23.13percent and unstripped Linux9.98percent. CD lazy raster omits38.5percent of original compositions on its measured route. These are footprint/repeated-work results; current runtime and catalog-only startup timing are inconclusive. The earlier media-provider launch improvement is recorded separately, not added to these results. Rejected native fault scopes are reverted.

Owner review: `G:/Temp/Codex/genesis-engine-burndown-20261027/owner-review/Review.cmd`. Three current private review builds have normal-window/audio launchers and persistent per-game Pass/Fail/deferred notes. Native saves/settings are isolated. Owner explicitly accepted the reviewed batch on 2026-10-28: Sonic CD passed; RKA passed after a direct old/new recheck, with the reported castle sighting called a fluke. Earlier cleared Sonic1 remains accepted. This clears merge/release handoff. No source fix was needed for the unconfirmed castle report (beads-h7ot); preserve the individual observations and A/B evidence. Core53fc7b4 and engineae1c742 were integrated and Windows consumer releases rebuilt/versioned and delivered with those pins. Release receipts are retained at G:/Temp/Codex/genesis-engine-release-20261028. Benchmark waiting was not a blocker. The remaining original implementation scopes are open under beads-3z5k; owner acceptance approved the reviewed batch, not exhaustion of future opportunities.

## Additional owner rendering requirement

- [ ] **E37: GPU-resident high-resolution rendering** - required owner guide2026-10-30; beads-3z5k.17. Required16x internal resolution at full speed with headroom on the owner PC; desired32x/64x, lower-end16x later. Compact immutable jobs, resident VRAM/tile/palette sources, changed-data-only uploads, shared native/enhanced preparation and bounded intermediates with a portable fallback. Native state/readbacks/hash/save-load/rollback/reset/resize and display-only netplay stay exact. Existing SDL window scaling is alreadyGPU; verify actual enhanced paths instead of assuming CPU-scale growth. E17 rejected native-resolution compositor implementations and did not evaluate or fulfill this guide. Reuse existing checks; any necessary small tooling addition is bounded to ten minutes of design under the owner clarification2026-10-31. Source: C:/Users/Matthew/Desktop/GPU-HIGH-RESOLUTION-RENDERING-GUIDE.md. This is OPEN and unqualified. The original36-row audit remains intact:2 original families open plus this additional requirement = **3 open across37 tracked families**.
