# Engine optimization completion audit (2026-10-28; serial update2026-10-29)

**No: the entire original implementation burndown was not completed.** The previous pass completed evaluations/dispositions and released a useful qualified subset. It did not exhaust the reusable engine opportunities. The all-checked presentation made that distinction too easy to miss.

Audit: `beads-1o1d`. Remaining implementation owner: **`beads-3z5k`**, under the Genesis engine meta-project and `beads-2wy8.1` optimization campaign. Existing E34/E35 owners remain `beads-hj4v` / `beads-3vb.11`. Title-specific opportunities remain in [TITLE_OPTIMIZATION_BACKLOG.md](TITLE_OPTIMIZATION_BACKLOG.md).

Current serial update: selected bounded contracts E01/E03-E08/E10 are delivered.
E11 wholefusion, E13 completegraphicsderivation and E14 completeexactstampjobs
were implemented, qualified and reached by actualgames, then decisively rejected.
Useful BATCHED/provider, immutableDAC and Win32-build/oracle packages remain.
E14 production452f52a/readyPR46 reviewed8029d30 passes fresh independentfull-scope
review, with no runtimegainclaim. **20 families remain open**. Reviewer explicitly
instructed freshserialE15 afterstatus/cleanup/sync. Combinedownerhandoffpending.
Historical narrative below describes the releasedbase; the36-rowtable and
[machine-readable audit](engine-optimization-completion-audit.json) reflectcurrentdispositions.

## What was actually closed

The written slash goal required evaluating all 36 families, implementing useful changes and recording evidence-backed dispositions. Its checklist explicitly allowed deferred and rejected evaluations to be checked. Under that wording, the evaluation/release task was completed. That is weaker than implementing or decisively eliminating the original opportunities. A dependency that requires new engine architecture is still work; it is not proof that an opportunity has been exhausted.

The released record contains **14 implemented-or-partial families, 19 deferred families, two existing capabilities and one rejected experiment**. The 14 overlap: E02/E36 share catalog compaction, E09/E12/E16 share deferred raster ownership, and E21/E25 share settled PSG silence. They are not 14 independent optimizations or proven speed wins.

Against the original scope, this audit distinguishes four bounded delivered contracts, ten partial families, nineteen deferrals, one rejected mechanism with its broader family still open, and two existing bounded findings. **30 families retain open original scope.** The other six close only their selected contracts; this is not a claim that no further variants exist. E19 is partial because the cartridge ordering/scratch change did not consolidate all timestamp/deferred-stream representation; it does not imply that cartridge and CD must share one scheduler.

## Audit method and evidence limits

Audited released engine `ae1c742adef48b6ef6ed5b4c1dd4b64514086edd`, engine base `f422ad52a04b9d253c0e9cbcdb3476aabd563ae8`, runtime core `53fc7b417d5c194a5ebdf8d623c1c4501226eb60` and generator core `7233852a6e45d8779b9dae8b29afd6b94d5231dc`. Compared tracked changes, original goals, per-family dispositions, retained profiles and final game/package evidence. This is a source/evidence audit, with no new implementations, builds, benchmarks or game launches.

Key direct checks:

- Core runtime still sets a fault scope, compares every opcode/extension word and invokes one operation per `execute_operation` (`runtime/m68k_step.c:165`). The Genesis code generator is unchanged between the two core identities. CD `execute_cpu` still dispatches one step through the catalog (`runner/cd/segacd_coordinator.c:217`). Code spans made instruction fetch cheaper; they did not provide whole native regions or general data-memory ownership.
- Generic raster still prepares separate plane/sprite arrays and composes them later (`runner/video/genesis_vdp.c:929`, `:954`). Packed kernels and deferred original rows are useful delivered changes, but do not establish full production fusion or host upload ownership.
- `sn_silent_span` requires all channels muted and filters settled; active PSG retains per-sample synthesis (`runner/audio/sn76489.c:184`, `:233`). Actual FM kernels, PCM/CDDA/mix pipeline, host audio, generic state storage and stamp kernel files are unchanged in the engine batch.
- CD presentation still makes a UI copy and uploads it (`runner/cd/presentation.c:522`); CD pacing still uses coarse `SDL_Delay` (`runner/cd/standalone.c:291`). Sequential stdio read-ahead is not asynchronous prefetch.

The retained CD profile is **an earlier candidate on one gameplay route**, not a universal post-release profile: `G:/Temp/Codex/genesis-engine-burndown-20261027/cd-spans-profile-symbolized.json` (3492 samples, engine `86d2d0c`, frames 1800-3000). It contains `execute_cpu` 6.96%, `execute_operation` 6.90%, `segacd_z80_bus_time` 5.18% and `fetch_plane_span` 2.12%, as well as DSO/title work. These self-attributions select possible investigation areas; they are not additive recoverable gains. Its cold `resolve_row` result does not rule out cartridge raster, boot/firmware, stamp-heavy transfers, physical GPU or real audio-device costs. The previous E03 claim that every data-read helper was below 1% is inaccurate: `segacd_read16` is 1.088% in this sample.

## Complete 36-family accounting

“Delivered” means a qualified bounded contract. “Partial”, “Deferred” and “Rejected mechanism” all retain open original scope. Historical evaluation statuses remain in [engine-optimization-results.json](engine-optimization-results.json); the separate [machine-readable audit](engine-optimization-completion-audit.json) records original goals, classifications, remaining work, source anchors and closure approaches.

| ID | Family | Implementation audit | Delivered or existing portion | Remaining original scope |
| --- | --- | --- | --- | --- |
| E01 | Native CD execution regions | Delivered (bounded) | Qualified bounded native CPU/deadline regions; engine-native-region-results.json. | Selected original bounded contract delivered; merge/owner handoff pending. Future variants not claimed exhausted. |
| E02 | Code identity and invalidation | Rejected mechanism; family open | Page-version runtime rejected, family remains open; engine-code-identity-results.json. | Other code-identity architectures remain open; rejection of page versions is not broader closure. |
| E03 | Owned memory access | Delivered (bounded) | Qualified operation-scoped owned data regions and word bus; engine-owned-memory-results.json. | Selected original bounded contract delivered; merge/owner handoff pending. Future variants not claimed exhausted. |
| E04 | Generated native arithmetic and state | Delivered (bounded) | Implemented common-emitter local D/A/SR spans, backward CCR liveness and exact grouped cycles, plus single-memory-RMW EA/read-write consolidation under complete managed dual ownership. Original bus/control/HLE/WAIT/fault/trace/unsupported exits remain exact. Engine132b84d/core4479e7e; final MSVC/GCC and all three built-game state/pixel/full-WAV routes and cartridge rollback pass. All final game timing neutral/inconclusive, no claimed speedup; r2/r3 regressions rejected, targeted reverse RKA off run does not reproduce the original24percent slowdown. Body proof131573 symbols/36089 redirects; current bytes and descriptor effects/priority unchanged. Cartridge observable bus limits documented. Independent final PR/record gate accepted; validator instructed E05 next. Owner visual handoff pending. See ENGINE_NATIVE_STATE.md, engine-native-state-results.json, beads-3z5k.3/beads-rxq.5. | Selected original bounded contract delivered; merge/owner handoff pending. Future variants not claimed exhausted. |
| E05 | Native call/control-flow consolidation | Delivered (bounded) | Inline complete native call frames and proven tail continuations; bounded complete-body/alias callgraph co-location; canonical live-guarded CD arithmetic/pure-control regions with exact variable retirement and fixed fallback. MSVC/GCC and all3 built-game gates pass; per-game timing neutral/inconclusive. EnginePR39/corePR10; independent final gate accepted. | Selected native-call/inlining/control-region contract delivered. Whole CD pure-leaf folding rejected after live upper-bound census0Main/19Sub fits among849238calls. Broader code-identity and compiler-wide choices remain independently owned E02/E36; no all-future-optimizations claim. Owner combined visual merge/release handoff pending. |
| E06 | Reusable native Z80 backend | Delivered (bounded) | Qualified shared uploaded-code/variant build integration, cartridge native scanline slice publication and reproduced boundary/producer fixes; engine424a990 / producerdc775df; PR40/producerPR3. | Selected original bounded engine contract delivered and independently accepted. Combined owner merge/release handoff pending. No claim all future ISA/driver/region alternatives exhausted. |
| E07 | Event-driven waits and scheduling | Delivered (bounded) | Actual event deadlines and qualified unchanged-clock pre-instruction service; production2079c51/PR41. Real service calls18.3569% fewer, exact clock/CPU work; final4-game-arm/checkpoint/pixel/WAV and6/6 MSVC+GCC pass. | Selected original bounded contract delivered; combined owner visual/merge/release handoff pending. No claim every future engine/game variant exhausted. |
| E08 | Transfer spans and layout mapping | Delivered (bounded) | Exact inactive DMA clock spans and all-five-band direct cell permutation; full selected-family inventory qualified. ENGINE_TRANSFER_SPANS.md / engine-transfer-span-results.json. | Selected original bounded contract accepted; combined owner merge/release handoff pending. Future consumer/ownership variants not claimed impossible. |
| E09 | Single image-production ownership | Delivered (bounded) | Reusable CD lazy original-image capture/composition with exact fallback; cartridge already had lazy rows. | The selected ownership/fallback contract is delivered. Other raster/cache/upload goals remain separate. |
| E10 | Renderer interface independent of aspect ratio | Delivered (bounded) | Used independent row-ownership/frame plan, logical viewport preservation, exact all-limit native fallback and mechanical consumer adoption; ENGINE_RENDERER_OWNERSHIP.md. | Selected original bounded contract independently accepted; combined owner visual/merge/release handoff pending. No claim title fidelity rollout or all future optimizations exhausted. |
| E11 | Fused generic VDP raster | Whole bounded opportunity rejected | Complete native direct-stream fusion implemented/qualified/reached, rejected versus best existing BATCHED. Existing BATCHED default adoption and provider compatibility repaired/qualified in50b0854/PR44. | Selected original bounded whole-fusion opportunity decisively rejected, not deferred/partial; no claim every future algorithm is exhausted. Combined owner merge/release handoff remains for useful adoption. |
| E12 | Shared sprite evaluation jobs | Delivered (bounded) | CD sprite evaluation captured once for both CPU-visible status and deferred image production. | The selected shared-evaluation contract is delivered; additional caching belongs to E13/E15. |
| E13 | Versioned generic graphics derivation | Whole bounded opportunity rejected | Full art/palette/geometry complete-input architecture1d0aab9 implemented/qualified/actuallyused, rejected for no demonstratedgain. Useful finite immutable DAC domain999f392/PR45 retained and independentlyaccepted; mutable caches removed. | Selected whole bounded opportunity evaluated/rejected, not partial/deferral or universal futuredesign exhaustion. Combined owner merge/release handoff remains for palette-only package. |
| E14 | Native Sega CD stamp-device jobs | Rejected whole bounded opportunity | Both complete exactjob/span architectures qualified/reached400jobSS1windows, then rejected on observedcost; originalkernel retained. Useful Win32fix+3080joboracle; PR46. | Selected bounded original opportunity decisively rejected; no universalfutureexhaustion. Combinedownerhandoffpending. |
| E15 | Reuse unchanged hardware render results | Deferred; open | No new generic unchanged-job/image reuse. | Complete intermediate hardware-input identity and reusable render output ownership. |
| E16 | Final pixels and upload ownership | Partial; open | Lazy CD conversion when original pixels are requested. | Conversion/copy/filter/UI staging/upload ownership across the host boundary. |
| E17 | Optional generic GPU raster backend | Deferred; open | Immutable CPU raster packets are available; no generic GPU backend was added. | A qualified generic GPU composition implementation with portable CPU fallback. |
| E18 | Independent engine work scheduling | Deferred; open | No new worker scheduling implementation. | Safe independently owned jobs and overlap without extra emulated latency. |
| E19 | One ordered audio timeline | Partial; open | Cartridge stable natural-run event ordering, reverse FM pairing and shared sort/defer scratch. | Broader timestamp/deferred-stream representation consolidation beyond this cartridge ordering replacement. |
| E20 | Faithful FM kernels | Deferred; open | Existing FM block wrapper retained; no new operator/channel kernel. | Actual faithful YM2612 arithmetic/layout/kernel optimization. |
| E21 | PSG event spans | Partial; open | Exact closed-form settled-muted PSG spans. | Active tone/noise generation spans and filter-tail work. |
| E22 | Sega CD PCM/CDDA spans | Deferred; open | Existing native timed PCM/CDDA advancement retained. | Block source publication instead of per-sample callbacks, including queue/status behavior. |
| E23 | Synthesis-to-device pipeline | Deferred; open | No new synthesis-to-device fusion. | Equivalent chip staging/mix/interpolation/filter/device-rate production. |
| E24 | Audio buffer ownership | Deferred; open | Existing bounded mixed frame and device-locked handoff retained. | Qualified reduction of callback locks/copies/shared state. |
| E25 | Exact constant/silent spans | Partial; open | Exact settled PSG silence, shared with E21. | Constant/silent PCM and CDDA runs with queue/timestamp/fader evolution. |
| E26 | Bounded generic media prefetch | Partial; open | Redundant seeks removed so bounded 1 MiB stdio read-ahead works. | Actual asynchronous request-driven prefetch off the simulation path. |
| E27 | Verified media handoff contract | Deferred; open | No generic verified media handoff. | Safe reuse of complete launch verification across a generic immutable media handoff. |
| E28 | Portable full-media validation | Delivered (bounded) | Optional OpenSSL full-payload SHA outside Windows, existing CNG and full portable restart/fallback. | The selected accelerated full-verification contract is delivered; immutable-sector reuse was not implemented or globally exhausted. |
| E29 | Resident-media representation | Delivered (bounded) | Read-only mapped first-data-track residency with bounded copied fallback. | The selected lower-private-copy representation is delivered; this does not exhaust all raw/user-sector representation alternatives. |
| E30 | Reusable console-firmware services | Deferred; open | Native BIOS instruction catalog retained; no new reusable whole-service HLE. | A named standard firmware service ABI with complete reusable effects. |
| E31 | Engine snapshot representation and identity | Deferred; open | Existing authoritative section table and full-byte checksum retained. | Generic writer ownership, dirty pages/deltas/checkpoints and exact incremental identity. |
| E32 | Serializer and snapshot storage | Deferred; open | Existing snapshot ownership/atomic-save implementation retained. | Generic transactional borrow/capacity/commit storage or pooling where useful. |
| E33 | Discarded-replay engine work | Existing (bounded) | Discarded cartridge replay already omits derived image work while retaining authoritative status/audio. | No remaining shared cartridge image work demonstrated at this base; title publication remains separate and CD work is E09. |
| E34 | Host presentation/deadline pipeline | Deferred; open | Previously delivered cartridge D3D11/high-resolution pacing retained. | Further shared presentation/upload/UI ownership and CD pacing consolidation. |
| E35 | Asynchronous engine persistence and diagnostics | Deferred; open | Existing dirty-tracked/minute-rate evidence and opt-in tracing retained. | Async immutable persistence or bounded trace rings for an enabled costly workload. |
| E36 | Whole-program native build optimization | Partial; open | Generated catalog compaction under ordinary linking, shared with E02. | Actual matched LTO/PGO/cross-TU/hot-cold layout evaluation and qualification. |

## Why several closures were too weak

The weakest closure pattern made the necessary replacement architecture a prerequisite for considering the optimization: effect metadata (E01/E04/E05/E07), complete writer ownership (E02/E03/E13/E31), timed block sinks (E22/E25), and transactional snapshot capacity (E32). These are legitimate correctness requirements, but creating them is part of the authorized engine work. They should lead to coherent packages, not imply that existing subsystem boundaries are permanent.

E27 used the title-backlog exclusion too broadly. A new game-specific launcher/converter/cache design remains title work. A reusable verified media contract plus mechanical consumer adoption is explicitly permitted engine work. It still needs immutable ownership/full-identity proof, including time-of-check/time-of-use behavior and a portable verification fallback.

Other deferrals are reasonable prioritization decisions: no stamp-heavy workload for E14, no default persistence stall for E35, no physical device evidence for E24/E34. They remain deferrals rather than delivered implementations or universally ruled-out candidates. Rejected narrow cache/tail/fault-scope mechanisms must not be repeated unchanged, but do not eliminate materially broader designs.

## Coherent remaining packages

This records remaining work; the audit does not start another implementation campaign. No speedup amount is promised. Use the smallest current cost observation needed to choose useful work, integrate a coherent replacement promptly, and preserve per-game neutral/positive acceptance. A busy benchmark host does not block all progress.

1. **Native execution and memory ownership:** E01-E05/E07, with E08 where transfers benefit. Generate canonical effect/exit metadata, retain unknown callbacks as exits, and run deadline-bounded regions with exact state materialization. Own data/code writers and remaps before versioned guard elision. This addresses repeated cross-boundary work still visible in the CD profile.
2. **Generic raster production and output ownership:** E10/E11/E13-E18. Start from delivered immutable rows and packed kernels, then examine a representative post-change cartridge pipeline. Replace repeated derivation/passes where useful; do not infer its cost from CD-only resolve_row. GPU/workers are alternatives, not mandatory infrastructure.
3. **Faithful audio stream production:** remaining E19-E25. Treat FM kernels, timed PCM/CDDA sinks, staging/mix/resample and device handoff as one possible representation change, preserving clipping order, integer arithmetic, filter/DRC state and capture taps. Retain event ordering and silent PSG.
4. **Verified launch/media ownership:** E27, then remaining E26 if actual read waits justify it. Reuse mandatory complete validation safely within one launch; retain E28/E29 full-validation/mapped-residency fallbacks. Keep title preload tables and file-engine HLE separate.
5. **Generic snapshot/storage ownership:** E31/E32. Complete writer/restore identity and transactional storage before dirty pages/deltas or serialize-into-owned-capacity. Exclude title payload redesign; do not add old save-state migration work.
6. **Workload-dependent alternatives:** E14 stamp, E30 firmware service HLE, E34 host delivery (reuse `beads-hj4v`), E35 enabled persistence/tracing (reuse `beads-3vb.11`), E36 actual compiler optimization. Each stays open with a concrete selection/closure approach below.

## Per-family evidence and closure approach

### E01: Native CD execution regions

Core execute_operation still guards and invokes one instruction; coordinator dispatches it per step.

**Next closure:** Generate effect/exit metadata and implement a bounded region removing repeated dispatch/state work; keep unknown callbacks as exits.

Sources: `runner/cd/segacd_coordinator.c:217`, `core/runtime/m68k_step.c:165`. Historical evaluation: `evaluated_rejected`.

### E02: Code identity and invalidation

catalog_index explicitly disclaims code-identity caching; core compares every live instruction word.

**Next closure:** Own all native/direct/import writers and aliases, then qualify versioned regions and invalidation.

Sources: `runner/cd/catalog_index.h:1`, `runner/cd/catalog_lookup.h:1`, `core/runtime/m68k_step.c:179`. Historical evaluation: `implemented_catalog_guard_elision_deferred`.

### E03: Owned memory access

code_span returns instruction bytes; it does not replace general data reads/writes. segacd_read16 is 1.088% in the cited profile, so the old below-1%-per-helper wording is inaccurate.

**Next closure:** Use the E01/E02 ownership contract for safe data regions with MMIO, remap, protection and WAIT exits.

Sources: `runner/cd/segacd.c:266`, `runner/cd/segacd_coordinator.c:145`, `core/runtime/m68k_step.c:181`. Historical evaluation: `implemented_game_qualified`.

### E04: Generated native arithmetic and state

The Genesis code generator is unchanged between generator core 7233852 and runtime core 53fc7b4. Effect metadata is engine work, not an external blocker.

**Next closure:** Add canonical generated-operation effects and exact observable-exit materialization as part of the native region package.

Sources: `core/profiles/genesis/code_generator.c`, `core/runtime/m68k_step.h:70`. Historical evaluation: `evaluated_deferred`.

### E05: Native call/control-flow consolidation

No new generator work; existing calls and a rejected tail-index experiment do not exhaust region fusion.

**Next closure:** Use E04 edge/effect metadata for proven direct regions; retain dynamic-target, guest-stack and IRQ exits.

Sources: `core/profiles/genesis/code_generator.c`, `runner/glue.c`. Historical evaluation: `evaluated_deferred`.

### E06: Reusable native Z80 backend

Historical existing-only finding was overturned by fresh diligence. Shared build wiring, full-state boundary/variant fixes and bounded cartridge native slice publication are delivered and independently accepted (engine424a990/producerdc775df, PR40/producerPR3). Supported consumer/runtime/portable/current-build state contracts pass; timing neutral/inconclusive. See [E06 qualification](ENGINE_Z80_BACKEND.md).

**Next closure:** Owner combined merge/release handoff remains; reviewer explicitly instructed serial E07 next. Driver-specific rollout and a full future Z80 ISA audit are not claimed.

Sources: `runner/z80_recomp.c`, `runner/cd/z80_native.c`, `docs/Z80_STATIC_RECOMP.md`. Historical evaluation: `evaluated_already_native`.

### E07: Event-driven waits and scheduling

Deadline memoization does not change the instruction/synchronization boundary.

**Next closure:** Combine with E01/E04, preserving MMIO, DMA, interrupt and Z80 observer times.

Sources: `runner/cd/machine.c`, `runner/cd/segacd_coordinator.c:217`, `runner/glue.c`. Historical evaluation: `implemented_game_qualified_further_coalescing_deferred`.

### E08: Transfer spans and layout mapping

DMA source is unchanged; the 1.58% CD attribution includes inactive calls and does not measure an eligible transfer-heavy workload.

**Next closure:** Use an actual transfer-heavy consumer; implement spans only across proven unobservable intervals, retaining partial progress and completion clocks.

Sources: `runner/cd/dma.c`, `runner/cd/segacd.c`, `runner/video/genesis_vdp.c`. Historical evaluation: `evaluated_deferred`.

### E09: Single image-production ownership

720000 rows captured, 442800 composed: 38.5% fewer original compositions, with 898600 retained bytes; full state/pixels/WAV checks. This is not an FPS claim.

**Next closure:** Retain exact hardware-status deadlines and per-row palette/fallback contracts.

Sources: `runner/cd/machine.c:30`, `runner/cd/machine.c:62`, `runner/video/genesis_vdp.c:891`. Historical evaluation: `implemented_game_qualified`.

### E10: Renderer interface independent of aspect ratio

Delivered and independently accepted: used optional ownership predicate and host
frame plan separate requested simulation width, viewport policy and local output.
Mechanical Sonic1 adoption retains existing eligibility; legacy RKA is unchanged.
All insufficient local texture limits choose exact native fallback without
passing a reduced width to any legacy adapter. Actual H32/H40 native ownership,
forced160 and supported360 fallback, exact state/audio/fullWAV/pixels/rollback and
current-build state evidence qualify the contract. Timing neutral/inconclusive.

**Next closure:** Combined owner visual/merge/release handoff. Fresh validator
explicitly instructed serial E11 next. TechnicallyComputers assignment remains
unavailable through repository eligibility404 and is transparently recorded.

Sources: `runner/main.c`, `runner/game_video.h`,
`docs/ENGINE_RENDERER_OWNERSHIP.md`, `docs/engine-renderer-ownership-results.json`.
Production96be781; PR43 revieweda75cd9f; companionPR7 pin6ecb59e.

### E11: Fused generic VDP raster

Complete bounded opportunity independently evaluated and rejected versus best
existing BATCHED, not deferred or partially delivered. Useful existing BATCHED
default adoption/provider fixes50b0854/PR44 accepted with exact built outputs,
portable tests and per-game runtime qualification. See [complete inventory and
failed experiment evidence](ENGINE_RASTER_FUSION.md). Reviewer instructed E13 next;
combined owner handoff remains.

### E12: Shared sprite evaluation jobs

Deferred composition consumes captured lanes without repeating sprite evaluation; limits/status/palette gates pass. This overlaps E09.

**Next closure:** Retain current sprite status and exact fallback tests; do not double-count E09/E12 as independent measured wins.

Sources: `runner/video/genesis_vdp.c:929`, `runner/video/genesis_vdp.c:954`, `runner/cd/machine.c`. Historical evaluation: `implemented_game_qualified`.

### E13: Versioned generic graphics derivation

Complete art/palette/geometry immutable-input architecture1d implemented,
qualified and actually used, then bounded rejected for no demonstrated benefit.
Pure finite DAC domain999f392/PR45 retained with exact full-platform/color/built-game
qualification, all timing neutral/inconclusive. Independent final gate accepted
reviewed5576577. See [full original inventory and evidence](ENGINE_GRAPHICS_DERIVATION.md).
This is no partial/deferral or universal future-cache exhaustion claim. Reviewer
explicitly instructed E14 next; combined owner handoff remains.

### E14: Native Sega CD stamp-device jobs

stamp.c is unchanged and had no samples in the recorded CD gameplay route. No stamp-heavy qualification was performed.

**Next closure:** Select a built stamp-heavy workload before choosing a block kernel; preserve partial writes, priority and IRQ timing.

Sources: `runner/cd/stamp.c`. Historical evaluation: `evaluated_deferred`.

### E15: Reuse unchanged hardware render results

Frame-end equality cannot establish identical raster history. A cold CD resolve_row does not rule out static-render-heavy consumers.

**Next closure:** Build on immutable packets and exact raster identity where actual repeated work justifies reuse.

Sources: `runner/video/genesis_vdp.c`, `runner/video/genesis_machine.c`. Historical evaluation: `evaluated_deferred`.

### E16: Final pixels and upload ownership

This is the E09 deferred-raster package. Host presentation still copies the image for UI and uploads it. Low resolve_row attribution is not physical upload evidence.

**Next closure:** Design shared raw-image/UI/output ownership preserving capture and filter edges; use actual presentation costs to choose fusion.

Sources: `runner/cd/machine.c`, `runner/cd/presentation.c:522`, `runner/main.c`. Historical evaluation: `implemented_lazy_pixels_upload_deferred`.

### E17: Optional generic GPU raster backend

No physical GPU A/B qualifies this alternative. One CD software/dummy route cannot globally reject it.

**Next closure:** Pursue only if actual composition/upload cost supports packet-to-GPU implementation; preserve CPU-visible effects and exact raw output.

Sources: `runner/video/genesis_vdp.c:891`, `runner/cd/presentation.c`. Historical evaluation: `evaluated_deferred`.

### E18: Independent engine work scheduling

The record is a design deferral, not a worker experiment. Current CD capture/resolve cost is not evidence about every cartridge/audio job.

**Next closure:** Choose a demonstrated heavy immutable job; include dispatch/copy/barrier costs and single-thread fallback in qualification.

Sources: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/audio.c`. Historical evaluation: `evaluated_deferred`.

### E19: One ordered audio timeline

event_order.h/mixer.c changed, but event_queue and CD native scheduler did not. CD receives no cartridge timeline gain. Storage rises from 5 to 6 MiB. A common scheduler is not automatically required.

**Next closure:** Treat ordering as delivered; pursue broader stream representation only where it removes repeated work while retaining ordering, push-index traces and DAC bursts.

Sources: `runner/audio/event_order.h`, `runner/audio/mixer.c:97`, `runner/audio/event_queue.c`, `runner/cd/audio_native.c`. Historical evaluation: `implemented_game_qualified`.

### E20: Faithful FM kernels

ym2612_ymfm.cpp and ymfm_fm.ipp are unchanged in the batch. Vendor API and exact arithmetic are implementation constraints, not a rejection of kernel replacement.

**Next closure:** Use current operator/channel attribution and a concrete equivalent layout/kernel; qualify register writes, feedback/LFO/envelopes/DAC and state.

Sources: `runner/audio/ym2612_ymfm.cpp`, `runner/external/ymfm/src/ymfm_fm.ipp`. Historical evaluation: `evaluated_deferred`.

### E21: PSG event spans

sn_silent_span requires all four volumes muted and filters exactly zero; otherwise psg_advance calls sn_render_sample for each sample. This overlaps E25.

**Next closure:** Keep the silent path; consider active runs only with a concrete exact phase/filter/kernel design and meaningful affected cost.

Sources: `runner/audio/sn76489.c:184`, `runner/audio/sn76489.c:233`. Historical evaluation: `implemented_game_qualified`.

### E22: Sega CD PCM/CDDA spans

PCM/CDDA/native mix source is unchanged. A span-sink contract is reusable engine implementation work, not an immutable subsystem boundary.

**Next closure:** Design a timed span sink and mechanical mixer adoption preserving wave writes, loops, fader, queue/underflow and timestamps.

Sources: `runner/cd/rf5c164.c`, `runner/cd/cdda.c`, `runner/cd/audio_mix.c`. Historical evaluation: `evaluated_deferred`.

### E23: Synthesis-to-device pipeline

Host and CD audio pipeline files are unchanged. Clipping, integer divisions, capture taps and phase constrain equivalent fusion; they do not establish that it is impossible.

**Next closure:** Specify the exact stream computation/ownership, then remove redundant passes while preserving generated-WAV/device contracts.

Sources: `runner/audio.c`, `runner/audio/mixer.c`, `runner/cd/audio_native.c`, `runner/cd/audio_mix.c`. Historical evaluation: `evaluated_deferred`.

### E24: Audio buffer ownership

audio.c is unchanged; dummy-device throughput does not measure actual callback contention or jitter.

**Next closure:** Use existing timing for a physical-device symptom; redesign all shared callback ownership rather than removing a lock alone.

Sources: `runner/audio.c`. Historical evaluation: `evaluated_deferred`.

### E25: Exact constant/silent spans

Other sources still publish samples individually. Silence does not imply the absence of authoritative state changes.

**Next closure:** Combine with E22 timed span sinks if source/mixer cost justifies compression.

Sources: `runner/audio/sn76489.c:184`, `runner/cd/cdda.c`, `runner/cd/rf5c164.c`. Historical evaluation: `implemented_psg_other_sources_deferred`.

### E26: Bounded generic media prefetch

There is no async prefetch worker in the new disc_image implementation. Raw-read/seek counts establish reduced seeks, not zero I/O stalls in every route.

**Next closure:** Retain read-ahead; add bounded cancellable request-driven prefetch only if actual media wait cost warrants it, with synchronous fallback.

Sources: `runner/cd/disc_image.c:446`, `runner/cd/cdd.c`, `runner/cd/cdc.c`. Historical evaluation: `implemented_read_ahead_async_deferred`.

### E27: Verified media handoff contract

The prior T12 exclusion is too broad if applied to mechanical adoption of an engine contract, which the scope explicitly allows. New title converter/cache design stays separate.

**Next closure:** Design a full-identity immutable handle/buffer or equivalent proof without a TOCTOU gap and with portable full-verification fallback; mechanically adopt where applicable.

Sources: `runner/cd/disc_image.c`, `docs/ENGINE_OPTIMIZATION_BURNDOWN.md`, `docs/TITLE_OPTIMIZATION_BACKLOG.md`. Historical evaluation: `deferred`.

### E28: Portable full-media validation

Provider/failure/fallback checks and parity pass. Earlier balanced Linux launches show bounded 12.4/13.8% wall and 40.2/41.1% process-CPU reductions; not an FPS or Windows claim.

**Next closure:** Keep full corruption detection and fallback; revisit immutable reuse separately only with complete ownership proof.

Sources: `runner/cd/disc_image.c:223`, `runner/cd/sector.c`. Historical evaluation: `implemented_game_qualified`.

### E29: Resident-media representation

Current mappedBytes 139735888 and privateResidentBytes 0 remove the separate private copy. Mapping length is not a measured RSS saving.

**Next closure:** Retain 256 MiB bound, raw/audio/movie readers and allocation/mapping-failure fallback.

Sources: `runner/cd/disc_image.c:40`, `runner/cd/disc_image.h`. Historical evaluation: `implemented_game_qualified`.

### E30: Reusable console-firmware services

Catalog execution is not service HLE. A gameplay-only profile does not cover boot, BRAM/UI or firmware control waits; title file/SP routines remain excluded.

**Next closure:** Find a costly standard firmware service and specify its complete ABI/effects before replacing it; do not move a title routine into engine code.

Sources: `runner/cd/standalone_program.c`, `runner/cd/segacd_coordinator.c`. Historical evaluation: `evaluated_deferred`.

### E31: Engine snapshot representation and identity

rb_state.c is unchanged. Dominant title fiber payload is outside scope, but does not rule out generic representation work. Old save-state compatibility is not required.

**Next closure:** Establish complete writer/restore ownership and current per-section cost before choosing a generic page/delta representation.

Sources: `runner/rb_state.c`, `runner/frame_snapshots.c`. Historical evaluation: `evaluated_deferred`.

### E32: Serializer and snapshot storage

rbe_snap_ring_store owns the blob on success, so malloc is not an immediate-copy leak. A new transactional storage contract is feasible shared engine work.

**Next closure:** If allocation/copy cost warrants it, serialize into reserved owned capacity without destroying the previous entry on failure.

Sources: `runner/local_states.c:142`, `runner/rb_state.c`. Historical evaluation: `evaluated_deferred`.

### E33: Discarded-replay engine work

Existing resim hooks/machine frame path establish this bounded terminal finding; current replay checks pass. No new gain is counted.

**Next closure:** Retain current-build replay/state correctness; reopen for newly demonstrated shared derived work.

Sources: `runner/sim_step.c`, `runner/video/genesis_machine.c`, `runner/main.c`. Historical evaluation: `evaluated_already_implemented`.

### E34: Host presentation/deadline pipeline

presentation.c is unchanged and CD still uses coarse SDL_Delay. Blind sleep/spin reuse is not a qualified solution; no new physical delivery comparison was made.

**Next closure:** Reuse beads-hj4v. Select an actual delivery/jitter or copy cost and implement a suitable shared pipeline/nonspinning fallback.

Sources: `runner/cd/presentation.c:522`, `runner/cd/standalone.c:291`, `runner/host_wait.c`, `runner/main.c`. Historical evaluation: `evaluated_deferred`.

### E35: Asynchronous engine persistence and diagnostics

No default hot file stall is demonstrated. This is a reasonable low-priority deferral, not an implemented asynchronous pipeline.

**Next closure:** Reuse beads-3vb.11; pursue a measured enabled trace/save stall and retain transactional saves, shutdown/error handling.

Sources: `runner/runtime_evidence.c`, `runner/local_states.c`, `runner/glue.c`. Historical evaluation: `evaluated_deferred`.

### E36: Whole-program native build optimization

Compaction is not an LTO/PGO experiment. No qualified new compiler alternative was delivered; noisy catalog CPU timing does not decide compiler optimization.

**Next closure:** Use a supported compiler package on built consumers; retain exact arithmetic/portability and record runtime/startup/memory. Benchmark host waiting is not a global blocker.

Sources: `tools/native_catalog_compact.py`, `tools/segacd/native_catalog_build.py`, `cmake`, `core/profiles/genesis/code_generator.c`. Historical evaluation: `implemented_catalog_lto_deferred`.

## Preserve the accepted result

The accepted Windows releases remain valid; this audit does not undo owner visual approval or identify a new runtime regression. Frozen release evidence remains at `G:/Temp/Codex/genesis-engine-release-20261028` with source/build/package identities, downloaded-asset validation and one-current-build delivery records. The historical evaluation/release owner stays closed; remaining engine scope is separately open under `beads-3z5k`.

Keep performance claims bounded: CD executable size fell 23.13%; lazy raster omitted 38.5% of original compositions on the measured route. Separate Linux SHA-provider launch observations have their own package/host limits. Catalog runtime timing was mixed, final broad per-game throughput is not universally qualified, and the RKA owner-capture pair is provisional rather than a controlled benchmark. Do not add component reductions or overlapping packages into a claimed whole-game speedup.
