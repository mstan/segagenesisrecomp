# Sonic native service optimization burndown

Owner: `beads-3vb.28`, related to optimization campaign `beads-2wy8.1`.
Final candidate: isolated `codex/sonic-native-integration-20261027`, combining
master `252535d` and released CD runtime `c8bcab9`; frozen cartridge reference
engine `252535d`, CD reference `c8bcab9` with matched input-isolation instrumentation.
Machine-readable opportunity state: `native-optimization-opportunities.json`.
Private evidence/build root: `C:/Temp/Codex/sonic-native-20261027`.

## Acceptance and sequence

Evaluate Sonic 1, Sonic 2, Sonic 3, Sonic & Knuckles, Sonic 3 & Knuckles,
then Sonic CD serially. Retain per-game positive **or neutral** outcomes;
fix or reject reproducible individual-game regressions. Matching failures in
the reference and candidate are shared pre-existing defects, recorded separately.
Ten percent less ordinary-gameplay process CPU time is a search target, not
a minimum gain required in every game. Never credit disabled features or
shortened guest work as a gain. Distinguish packages and platforms.

Every opportunity below receives a disposition: implemented/qualified, neutral,
rejected, ruled out by evidence, or explicitly unverified. Existing measured
rejections are evaluations and are reused unless new attribution changes the
case. After two unsuccessful experiments, reassess rather than continue tuning.

## Opportunity list

| ID | Opportunity | Evidence and current disposition |
|---|---|---|
| V1 | Avoid duplicate hardware image when a custom Sonic row is complete | Sonic 1 services package saves40.47% CPU in tested16:9; stock neutral. All five cartridge titles pass stock/16:9 state/audio/image/work and current-build lifecycle checks. Final-package timings unmeasured; owner explicitly removed them as a release blocker. |
| V2 | Decode custom terrain/background in tile-row spans | Sonic 1 independent lazy-to-tile stage saves22.39% CPU in tested16:9.20,000 random pattern cases and all five title comparisons pass. CD owns a separately tested span renderer. |
| V3 | Prepare sprite row jobs / scanline membership once per scene publication | Sonic1 custom sprite drawing:16/2022 mapped executable samples (~0.79%). Retain list traversal; large crowd/mod scenes unprofiled. |
| V4 | Reuse palette and row geometry work at valid change boundaries | Row geometry hoisted in all cartridge adapters. Existing64/128-color tables already follow live per-row CRAM; no persistent palette cache added. CD per-pixel color conversion hoisted to live row tables separately. Combined correctness passes; independent geometry timing unmeasured. |
| V5 | Broader shared VDP spans, sprite decoding and priority composition | Evaluated existing exact BATCHED implementation and two inconclusive widescreen investigations. Keep SCALAR after this package removes much of its duplicate work. No new promotion or repeated tuning without fresh attribution. |
| V6 | Remove redundant ARGB conversion, copies and intermediate buffers | Original-row work disappears with V1 when unused. Attribute remaining presentation costs before another representation change; retain linear-filter edge correctness. |
| V7 | Avoid building sprite color layers during status-only evaluation | Sonic1 full sprite evaluator57/2022 mapped executable samples; historical normalSonic2 sprites3.29%. Unused color writes are a subset. Prior S3K conditional-clear experiment regressed both pairs4.1 to 4.9%; no speculative variant. |
| V8 | Restore native Blue Spheres composition | Shared baseline presentation defect reported during owner check (`beads-tdq.3.26`). Removed inaccurate host projection; original-aspect native special stages, normal levels remain widescreen. Both renderer-profile native-pixel oracles and owner visual recheck pass. Shared native-pixel tests and Sonic3 runtime recheck pass; no independent performance claim. |
| A1 | Generate faithful YM2612 samples in blocks between register events | Implemented behind a fixed build choice, preserving scalar. Exact component and Sonic 1/2 correctness pass. Independent FM comparison mixed (+3.94%/-0.40% CPU saving), spread 4.50pp: inconclusive. Whole-package Sonic 1 evidence remains valid; no independent FM win claimed. |
| A2 | PSG event spans / unchanged-output batching | Normal Sonic2 PSG1.76%, historicalS3K1.279%; counter/LFSR/LPF exactness retained. Larger measured rendering and rollback opportunities selected. |
| A3 | Audio event ordering, pair matching and repeated sorting | Existing mixer sorts stable (stamp,push-index) order and defers long PCM bursts. Prior routing share only 0.149%; evaluated as low priority absent new attribution. Do not erase address/data pairing or long-burst behavior. |
| Z1 | Larger native Z80 execution blocks / sound-driver service boundary | Matched cartridge builds retain the stepped Z80 interpreter; family GENESIS_Z80_RECOMP=OFF, no AOT source; S1/S2 do not select AOT. Optional guarded flat-step AOT exists with historical exact audio/RAM checks but no current built-performance qualification. Normal Sonic2 execution/bus3.19%, historicalS3K2.714% bounds its whole-game ceiling; no claim that these cartridge runs use native sound. Keep reset/BUSREQ/timestamps and current coverage. CD separately already uses strict native Z80. |
| C1 | Generated 68000 dispatch and execution granularity | Binary-search dispatch already retained. Static tail indices previously mixed2.895%/-1.243%, rejected. Remaining dispatch2.51% normal; no further specialization. |
| C2 | Scheduling/fiber batching between observable events | Normal Sonic2 scanline/fiber2.14% (named scheduler1.96%), dense1.28%. Preserve IRQ/BUSREQ/DMA boundaries. |
| M1 | Direct guest RAM/ROM access or native object representation | Historical wrappers/bus around 2% combined; few statically direct-map absolute sites. Prior bus-ring removal and Z80 low-RAM fast reads failed to improve. Ruled out for broad surgery without fresh evidence; preserve byte layout and slot identity. |
| D1 | DMA/port transfer batching | Prior VDP write/control attribution only 0.286%. Evaluated as low ceiling for cartridge workloads; CD must be assessed independently. |
| R1 | Snapshot/digest representation and discarded rollback rendering | Discarded image optimization already integrated; streaming digest experiment neutral/rejected. Dense replay gains are not ordinary-gameplay gains. Inspect title-state payload only if new measured costs justify it. |
| R2 | Portable full-byte rollback checksum | Dense replay attribution42.31% versus0.65% ordinary snapshot/digest work. Pinned BSD2 XXH64 candidate retains all bytes/partitions and exact build identity gate. Component full1.59MB x400: FNV0.521s, XXH640.026s; golden/alignment/full-byte mutation pass. Built-game correctness passes; final runtime/dense timings unmeasured, no numerical game claim. |
| H1 | SDL presentation, texture upload and framework bookkeeping | Active rectangle upload and logical-size caching already retained. SDL13.86% and host audio3.17% in normal profile include required delivery. Timer work isolated asH2; no physical GPU performance claim. |
| H2 | Replace the final host pacing spin with a deadline timer | New paced Sonic 1 profile records 1,163 QPC samples versus 2,022 in-executable samples. Existing code sleeps coarsely then busy-spins. Windows high-resolution waitable-timer candidate preserves deadline math and a portable fallback; evaluate paced CPU/frame and delivery jitter separately from uncapped graphics headroom. |
| CD1 | Sega CD VDP/custom rendering ownership | Implemented game-owned spans, lifted geometry and live row palettes.4,000 randomized full-output/fallback differential cases pass. Whole-frame fallback retained; cartridge lazy raster rejected for this contract. Built stock/wide game correctness passes; final timing unmeasured. |
| CD2 | Sega CD CPU coordination, Word RAM and device scheduling | Implemented guarded descriptor-prefix discovery cache: live opcode/extension guards and module order retained, progressive search, overflow fallback, registry reset;22KiB fixed cost. Cached/reference tests pass Windows/Linux. Built stock/wide game correctness passes; final timing unmeasured. |
| CD3 | Sega CD media reads, rotation/scaling, PCM and CDDA | Source screened: SGCD identity reads already batched; sector reads follow drive cadence. Native PCM/CDDA and ares-derived stamp path retain asynchronous completion/timestamps. No loading shortcut adopted; optional remaining-cost profile deferred per owner release instruction. |

## Release disposition, 2026-10-27

All six built games passed matched stock and16:9 full-runtime correctness checks.
All five cartridges passed current-build save/load, invalid-state rejection,
rollback probes and mutable-section selftests. Sonic CD retained exact native
execution counts, memory and nonzero audio with zero floor/audio faults. The
shared Blue Spheres defect was fixed with original native composition and
accepted by the owner. The owner cleared all six games visually.

The owner explicitly instructed: "just finish the entire release pipeline.
stop waiting for benchmarks. not a blocker". Final-package timing is therefore
unmeasured, and no additional reservation or benchmark retry blocks release.
Earlier Sonic1 services-only16:9 measurements saved40.47% process CPU on the
recorded host; stock was neutral/inconclusive. Those historical binaries and
samples remain distinct from this integrated release. No final-package or
other-title numerical gain is inferred from that earlier result.

Sonic1 preserves upstream cb898070's qualified Windows BATCHED default. Both
new final comparison arms use BATCHED; earlier SCALAR experiments and results
are not relabeled. All remaining unmeasured opportunities are explicitly
deferred, without additional runtime changes. In particular, CD3's source
screen found no justified media/stamp/audio change; its optional new profile
is deferred. The23-opportunity machine-readable list records these outcomes.

Pinned source/build identities, per-game data, raw correctness/timing evidence
and owner observations are retained in the campaign and private evidence root.
Final source revisions are recorded by release build manifests. No blog post
or cross-build netplay compatibility claim is authorized by these checks.

## Handoff and integration

Keep raw samples, build/configuration/source identities and per-game JSON rows
linked from the owning Bead and campaign. Coordinate stress timing across console
sessions; when busy, check host availability at 30-minute intervals. Use fixed
CPU affinity, A/A controls and balanced matched pairs with full runtime features.
Keep failed controls; use existing 3-percentage-point noise gate.

Prepare final combined candidate builds in normal windows with normal audio and
preserved owner saves. The owner's confirmation that those games look/play good
authorizes merging that accepted batch to main/master, updating title engine
pins, and cutting verified replacement title builds. No second merge approval
is required. Keep one current delivered archive per title. Do not publish a blog
post; retain reproducible data for the campaign's eventual visuals.

## Owner acceptance and Blue Spheres follow-up, 2026-10-27

Owner cleared Sonic 1, then the remaining normal games. The selector preserved a Sonic 3 Blue Spheres failure and chat identified the same family special-stage problem. The forced native-entry S3K reference and candidate match at initial/turn checkpoints in stock and16:9 (pixels, RAM andVRAM), making this a shared pre-existing visual defect. The owner then approved the native-composition fix: "blue spheres ok now". Exact accepted S3K fix binary is `ac3b700abff4368b8f5671e3b384126ee44f0dced75124caa8a6f57ab4e12555`. Shared renderer tests cover both cartridge profiles. Normal-game visual checks are complete; do not repeat them. Native special stages intentionally retain original aspect inside the chosen window canvas.

Owner subsequently removed benchmark waiting as a release blocker. Merge, title repins and verified replacement packages proceed using the completed correctness and visual acceptance; no further benchmark attempt is queued.
