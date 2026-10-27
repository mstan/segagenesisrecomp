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
| V1 | Avoid duplicate hardware image when a custom Sonic row is complete | Sonic 1 services package saves40.47% CPU in tested16:9; stock neutral. All five cartridge titles pass stock/16:9 state/audio/image/work and current-build lifecycle checks. FinalR2 Sonic1 recheck and other title timings pending. |
| V2 | Decode custom terrain/background in tile-row spans | Sonic 1 independent lazy-to-tile stage saves22.39% CPU in tested16:9.20,000 random pattern cases and all five title comparisons pass. CD owns a separately tested span renderer. |
| V3 | Prepare sprite row jobs / scanline membership once per scene publication | Sonic1 custom sprite drawing:16/2022 mapped executable samples (~0.79%). Retain list traversal; large crowd/mod scenes unprofiled. |
| V4 | Reuse palette and row geometry work at valid change boundaries | Row geometry hoisted in all cartridge adapters. Existing64/128-color tables already follow live per-row CRAM; no persistent palette cache added. CD per-pixel color conversion hoisted to live row tables separately. Combined correctness passes; independent geometry timing pending. |
| V5 | Broader shared VDP spans, sprite decoding and priority composition | Evaluated existing exact BATCHED implementation and two inconclusive widescreen investigations. Keep SCALAR after this package removes much of its duplicate work. No new promotion or repeated tuning without fresh attribution. |
| V6 | Remove redundant ARGB conversion, copies and intermediate buffers | Original-row work disappears with V1 when unused. Attribute remaining presentation costs before another representation change; retain linear-filter edge correctness. |
| V7 | Avoid building sprite color layers during status-only evaluation | Sonic1 full sprite evaluator57/2022 mapped executable samples; historical normalSonic2 sprites3.29%. Unused color writes are a subset. Prior S3K conditional-clear experiment regressed both pairs4.1 to 4.9%; no speculative variant. |
| V8 | Restore native Blue Spheres composition | Shared baseline presentation defect reported during owner check (`beads-tdq.3.26`). Removed inaccurate host projection; original-aspect native special stages, normal levels remain widescreen. Both renderer-profile native-pixel oracles and owner visual recheck pass. Final family runtime recheck pending; no independent performance claim. |
| A1 | Generate faithful YM2612 samples in blocks between register events | Implemented behind a fixed build choice, preserving scalar. Exact component and Sonic 1/2 correctness pass. Independent FM comparison mixed (+3.94%/-0.40% CPU saving), spread 4.50pp: inconclusive. Whole-package Sonic 1 evidence remains valid; no independent FM win claimed. |
| A2 | PSG event spans / unchanged-output batching | Normal Sonic2 PSG1.76%, historicalS3K1.279%; counter/LFSR/LPF exactness retained. Larger measured rendering and rollback opportunities selected. |
| A3 | Audio event ordering, pair matching and repeated sorting | Existing mixer sorts stable (stamp,push-index) order and defers long PCM bursts. Prior routing share only 0.149%; evaluated as low priority absent new attribution. Do not erase address/data pairing or long-burst behavior. |
| Z1 | Larger native Z80 execution blocks / sound-driver service boundary | Matched cartridge builds retain the stepped Z80 interpreter; family GENESIS_Z80_RECOMP=OFF, no AOT source; S1/S2 do not select AOT. Optional guarded flat-step AOT exists with historical exact audio/RAM checks but no current built-performance qualification. Normal Sonic2 execution/bus3.19%, historicalS3K2.714% bounds its whole-game ceiling; no claim that these cartridge runs use native sound. Keep reset/BUSREQ/timestamps and current coverage. CD separately already uses strict native Z80. |
| C1 | Generated 68000 dispatch and execution granularity | Binary-search dispatch already retained. Static tail indices previously mixed2.895%/-1.243%, rejected. Remaining dispatch2.51% normal; no further specialization. |
| C2 | Scheduling/fiber batching between observable events | Normal Sonic2 scanline/fiber2.14% (named scheduler1.96%), dense1.28%. Preserve IRQ/BUSREQ/DMA boundaries. |
| M1 | Direct guest RAM/ROM access or native object representation | Historical wrappers/bus around 2% combined; few statically direct-map absolute sites. Prior bus-ring removal and Z80 low-RAM fast reads failed to improve. Ruled out for broad surgery without fresh evidence; preserve byte layout and slot identity. |
| D1 | DMA/port transfer batching | Prior VDP write/control attribution only 0.286%. Evaluated as low ceiling for cartridge workloads; CD must be assessed independently. |
| R1 | Snapshot/digest representation and discarded rollback rendering | Discarded image optimization already integrated; streaming digest experiment neutral/rejected. Dense replay gains are not ordinary-gameplay gains. Inspect title-state payload only if new measured costs justify it. |
| R2 | Portable full-byte rollback checksum | Dense replay attribution42.31% versus0.65% ordinary snapshot/digest work. Pinned BSD2 XXH64 candidate retains all bytes/partitions and exact build identity gate. Component full1.59MB x400: FNV0.521s, XXH640.026s; golden/alignment/full-byte mutation pass. Built runtime and dense replay qualification pending; no game claim. |
| H1 | SDL presentation, texture upload and framework bookkeeping | Active rectangle upload and logical-size caching already retained. SDL13.86% and host audio3.17% in normal profile include required delivery. Timer work isolated asH2; no physical GPU performance claim. |
| H2 | Replace the final host pacing spin with a deadline timer | New paced Sonic 1 profile records 1,163 QPC samples versus 2,022 in-executable samples. Existing code sleeps coarsely then busy-spins. Windows high-resolution waitable-timer candidate preserves deadline math and a portable fallback; evaluate paced CPU/frame and delivery jitter separately from uncapped graphics headroom. |
| CD1 | Sega CD VDP/custom rendering ownership | Implemented game-owned spans, lifted geometry and live row palettes.4,000 randomized full-output/fallback differential cases pass. Whole-frame fallback retained; cartridge lazy raster rejected for this contract. Built game gate and timing pending. |
| CD2 | Sega CD CPU coordination, Word RAM and device scheduling | Implemented guarded descriptor-prefix discovery cache: live opcode/extension guards and module order retained, progressive search, overflow fallback, registry reset;22KiB fixed cost. Cached/reference tests pass Windows/Linux. Built game gate and timing pending. |
| CD3 | Sega CD media reads, rotation/scaling, PCM and CDDA | Source screened: SGCD identity reads already batched; sector reads follow drive cadence. Native PCM/CDDA and ares-derived stamp path retain asynchronous completion/timestamps. No loading shortcut adopted; profile remaining cost afterCD2 gate. |

## Current title results

The gameplay and timing evidence in the historical table below belongs to the earlier, separately
identified packages. A new isolated integration candidate combines master
`252535d` with released CD runtime `c8bcab9` and both optimization packages.
It uses forward CPU-core dependencies `7233852` / `4dfc5fc`, with fresh cartridge
generation. All six game binaries build. Its synthetic suites pass 69 (Sonic 1),
81 (Sonic 2), 99 (Sonic 3 family), and 38 (CD) tests. Earlier wins are not
relabelled as measurements of these binaries.

Final combined-build gate, updated 2026-10-27:

| Game | Integrated correctness | Integrated performance |
|---|---|---|
| Sonic 1 | Stock/16:9 exact image/state/audio/work, rollback/selftest and lifecycle pass | Stock identical-binary A/A drift 1.240% exceeds fixed 1% gate; samples unqualified, no candidate timing yet |
| Sonic 2 | Stock/16:9 exact image/state/audio/work, rollback/selftest and lifecycle pass | Pending idle-core reservation |
| Sonic 3 | Stock/16:9 exact image/state/audio/work, rollback/selftest and lifecycle pass | Pending idle-core reservation |
| Sonic & Knuckles | Stock/16:9 exact image/state/audio/work, rollback/selftest and lifecycle pass | Pending idle-core reservation |
| Sonic 3 & Knuckles | Stock/16:9 exact image/state/audio/work, rollback/selftest and lifecycle pass | Pending idle-core reservation |
| Sonic CD | Stock/16:9 full-runtime exact images, memory, audio and native work counts; zero fallback/audio faults | Pending idle-core reservation |

Game-fidelity-only jobs still hold the shared reservation and run below normal
priority, muted. They may run during ordinary build load and make no timing
claim. Performance jobs additionally require an idle physical core, record native
PDH headroom, and retain the same fixed controls and noise thresholds. Exact EXE,
SDL, ROM, input and settings hashes bind reusable game gates to the timing arms.

| Game | Package | Correctness | Performance | Owner check |
|---|---|---|---|---|
| Sonic 1 | V1+V2+V4+A1+H2; final R2 rebuild pending game recheck | Services stock/16:9 image/state/audio/work and lifecycle match;31 CTests,18 rollback probes pass | Qualified services build:16:9 saves40.47% process CPU; stock neutral. LatestR2 build not yet timed. | Pending final build |
| Sonic 2 | V1+V2+V4+A1+H2+R2 | Stock/16:9 image/state/audio/work and lifecycle match;43 CTests plus2 digest tests;18 rollback probes zero divergence; section selftest passes | Awaiting coordinated timing slot | Pending |
| Sonic 3 | V1+V2+V4+A1+H2+R2 | Active AIZ3,600frames stock/16:9 exact;28 rollback probes/view; section selftest and lifecycle pass;63 CTests pass after setup repairs | Awaiting coordinated timing slot | Pending |
| Sonic & Knuckles | V1+V2+V4+A1+H2+R2 | Active Mushroom Hill3,600frames stock/16:9 exact;28 rollback probes/view; section selftest and lifecycle pass | Awaiting coordinated timing slot | Pending |
| Sonic 3 & Knuckles | V1+V2+V4+A1+H2+R2 | Active AIZ3,600frames stock/16:9 exact;28 rollback probes/view; section selftest and lifecycle pass | Awaiting coordinated timing slot | Pending |
| Sonic CD | CD1+CD2+A1; distinct CD engine lineage | Both built;38 CTest outcomes pass after obsolete scratch-policy/path-casing tooling repair. Full game gate pending. | Unmeasured | Pending |

Sonic CD reference is title797a109/enginec8bcab9 plus matched QA-only physical-input isolation; the candidate uses the same native catalogs and inputs. Its feature-line runtime differs substantially from cartridge master. No cartridge performance result is credited to CD, and no unrelated CD backend merge is implied by these measurements.

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

Both hosts had foreign held reservations at16:17UTC; next coordinated automated game/timing attempt is at or after16:47:43UTC. Remaining technical gates precede the already authorized merge, title repins and replacement releases.
