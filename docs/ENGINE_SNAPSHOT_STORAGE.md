# E32: serializer and snapshot storage

Qualified bounded package: engine `717565c4e6bfed23f4ad63821373ec96191d9fdf`, based on E31 records05d7476 and unchanged qualified E30 runtime71b19b0. Owner `beads-3z5k.28`, parent `beads-3z5k`, campaign `beads-2wy8.1`. [Machine results](engine-snapshot-storage-results.json) retain exact source/binary/ROM/input identities, raw samples, settings and host telemetry. Combined owner build/play/merge/release handoff remains open.

## Delivered behavior and costs

Both local rewind and the netplay snapshot adapter serialize directly into rbengine's unpublished capture capacity. The existing shared [rbengine PR2](https://github.com/RetroPortingToolKit/rbengine/pull/2) is reused at `b29bdde642bc087f21303b827fadba40496b502d`; this package does not change or duplicate that API. The prior visible history is untouched until serialization succeeds. Failed growth/shrink/save preserves history, and the local tick/thumbnail metadata commit only after publication. The old canonical full snapshot bytes, identity and file formats remain unchanged.

Stable-size capture reuses depth+1 allocations after warmup rather than calling the allocator for every snapshot. The callback allows up to4096B of spare capacity for small size fluctuations; larger excess shrinks on the next successful capture. A failed resize keeps the old unpublished allocation and does not publish a replacement. Clear/shutdown free the owned ring and spare. This is host storage only, with no new simulation profile or wire format.

**This is not a proven whole-game speedup or memory reduction.** Actual timing is neutral/inconclusive. Sonic1 peak committed memory rises about3?3.5MB; RKA rises about0.2?1.2MB in the measured pairs. These costs are retained, rather than representing fewer allocator calls as lower RSS. The component improvement is bounded allocation reuse, verified with the actual callback and ring. No unmeasured stutter/latency claim is made.

The optional GRHOST2 save path no longer allocates a second complete body solely to calculate its checksum. It reads the same temporary-file body through a fixed64KiB stack buffer and computes exactly the original CRC polynomial/initialization/final complement. The old opaque title payload and load-preflight body are still required. Existing header update, flush/close, atomic replacement and error cleanup remain. The32MiB maximum test demonstrates bounded checksum storage; GCC reports a65,632B helper stack frame, not an RSS measurement. Representative RKA/Sonic1 specs use GROWNS2, so no per-game host-save latency or memory benefit is claimed from this optional path.

## Complete bounded family decisions

| Candidate | Decision |
| --- | --- |
| Direct/reusable local rewind capture | Implemented and used in real games; published history and cadence/metadata stay exact. |
| Direct/reusable netplay serializer | Uses the same new engine callback and existing ring API; normal full-image load vtable and driver ordering remain. Actual callback/ring fixtures and the common built-game serializer/restore checks qualify storage. No new network-transport matrix or cross-build compatibility claim. |
| Generic checksum copy | Removed the second full save-body image; fixed-buffer CRC matches the independent old algorithm, including chunk boundaries, short/error reads and32MiB input. |
| Generic thumbnail work | Preserve fresh128x90 sampling every frame, including when rewind is disabled. Native-slot thumbnails consume the latest frame independently of rewind cadence. The caller does not transfer persistent image ownership; retaining arbitrary pointers across ticks/restores is unsafe, while owning a full source copy costs more than the bounded thumbnail. No stale cadence, borrowed image or title-specific eligibility is adopted. |
| Digest/probe serializer scratch | Already grows/reuses its buffers; not new allocation work. |
| Cold reset and load rollback/preflight | Reset captures once; cold load backups and preflight payloads are required for safe rejection/transactional recovery. Their semantics remain. Title-owned payload shrinking is excluded. |

The bounded original opportunity is resolved with independent dispositions; this does not claim that every future storage/thumbnail algorithm is exhausted. Async persistence/diagnostics remains E35.

## Actual results

WindowsMSVC19.44 actual native games run matched inputs/softwareSDL presentation/full audio emulation with dummy output, two fixed measured physical cores per pair, uncapped runtime, shadow off, isolated profiles and hidden normal windows. Each pair uses one host/configuration. Baseline is frozen71b19b0 with the same diagnostic rewind hook; candidate source hashes match the committed implementation. Raw files are under `C:/Temp/Codex/genesis-engine-snapshot-storage-20261101`.

| Game/run | CPU change | Cycles change | Peak commit A?B |
| --- | ---: | ---: | ---: |
| RKA6000, BA | +3.488% | +2.737% | 75,956,224?76,926,976B |
| RKA6000, AB | -0.380% | -0.236% | 75,718,656?76,922,880B |
| RKA6000, rewind branch | -4.332% | -2.594% | 75,857,920?76,054,528B |
| Sonic1 2400, BA | +2.151% | -0.006% | 76,046,336?79,446,016B |
| Sonic1 2400, AB | -1.031% | +1.176% | 76,066,816?79,548,416B |
| Sonic1 2400, rewind branch | +2.062% | +1.482% | 75,882,496?78,970,880B |

The RKA direction reverses across orders; Sonic1 small mixed results are neutral/inconclusive. No aggregate offset or speedup claim is made. Guest/audio identities, periodic pixels, raw PNG, full WAV, rollback diagnostics and actual oldest/middle/newest rewind resumption match A/B. The branch route covers three RKA restores and one Sonic1 restore.

MSVC and Ubuntu24.04/GCC13.3 each pass3/3 targeted CTests: local save browser/rewind, shared capture ownership and streaming CRC. The independent actual-callback/real-ring model passes16 scenarios/384,439 checks, including failed grow/shrink/save, branch/refill, all-buffer shrink, no allocator calls after stable warmup and zero live allocations at shutdown. CRC passes13 scenarios/623 checks against the old independent oracle. Those are bounded component checks, not claims of actual network-transport or optional full-host-save runtime coverage.

## Native file caveat retained

RKA current-build save/load completes the6000-frame route with matching A/B guest/raw/fullWAV/rollback. Its157,106B files use GROWNS2; file bytes themselves may contain build-specific images and are not required to match across builds.

The Sonic1 cold-load-plus-rollback-probe route is **failed, pre-existing in both builds**. Both save at1321, load at1441, restart at3AE2, display identical frame hashes through1500 and exit0xC0000005 afterward. `native-qualification-save-r1.json` and `native-qualification-save-followup.json` retain the failed flags, exact logs, executables and isolated saves. It is not presented as a passed native-load route or fixed by this package. The owner explicitly allows identical baseline/candidate failures without disqualifying an optimization. This separate defect remains tracked in the E32 qualification notes; normal/rewind checks pass.

The first build found the new callback missing from the separate legacy-consumer reconciled source list; both supported source-list paths were fixed, then both final native games built successfully. The original failed log is not a candidate runtime result. No owner-visible games were relaunched, delivery folders changed or user saves/settings touched.

Ready engine [PR57](https://github.com/mstan/segagenesisrecomp/pull/57). Requested assigneeTechnicallyComputers resolves as a validGitHubaccount, but GitHub reports it is not assignable in this repository; CLI assignment fails. The requested assignment is recorded on the PR and in tracking, without changing repository access. This external handoff gap is distinct from the qualified engine contract.
