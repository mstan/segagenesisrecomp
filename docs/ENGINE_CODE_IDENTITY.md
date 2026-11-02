# E02 code ownership: two further implemented alternatives

The original code-identity work now has two additional built-game evaluations beyond the rejected physical-page package below. Both preserve exact behavior. Neither demonstrates a runtime gain on the qualified Sonic CD route. The short-interval alternative is rejected. The immutable-region implementation delivers its qualified ownership contract with neutral/inconclusive runtime observations and is ready for independent adoption review under the owner's acceptance of neutral games. The small consistent whole-run cost increase remains an explicit review finding, not a proven slowdown beyond ambient variation. No performance gain, adoption, merge, release or universal impossibility is claimed.

Baseline: engine `be957a4a542ab3895711102e2633b27bd709fc0c` / runtime core `f7dd616530e4c296855f2c22f9420bbe88586dc0`. The exact qualified 10,957-object archive (`1ebae703e46fe0c50e10b77c7687e022515ab2fd11320bfe94383732f29d9249`) is reused unchanged. Neither alternative changes the generated operation, descriptor or `M68kStepContext` ABI. Only CD uses the changed explicit-step entry points; cartridge generator/execution code is unchanged, and no new cartridge timing is claimed.

## Broader execution ownership, not another page tracker

The first alternative, engine `d0b3e176eb4fd156ef0886980149ad3499d21abc` / core `21169f0de49b379770a2b07b6ab65ef2b9dc46cd`, borrows a successful code proof only inside an exclusive register/flow interval. It clears every proof before any device/host observer and every return. Thus even an unannounced raw host write between intervals gets full guards on next use, without watched pages or per-writer callbacks. Actual gameplay reaches only **53** reuses across 25,478,174 native-region runs: strict CPU ordering ends these intervals before repeated execution. Four canonical arms pass, but both timing orders cost more. This mechanism is rejected; further tuning of its bookkeeping is not warranted by its demonstrated reuse window.

The second alternative, engine `235cd3e700e93e51ee7055c2f92c6eaaaadb6596` / core `df39ba669e3af540cf08f3eb3f5827737e5233aa`, introduces an explicit immutable executable-region lease spanning a whole audited production frame. It covers ordinary fault-scoped native operations as well as register/flow spans, retaining the first complete current-word guard and exact variant precedence. The production qualification owns only Main BIOS `[0x74,0x20000)`, whose writes are ignored by the hardware API; the dynamic `0x72` hint vector and every crossing instruction/span remain guarded.

Generic machines/coordinators default OFF. Qualification binds exact IO/host/CPU bus/code/data callbacks, user objects, media cancellation, floor, audio/video observers and module registries. A changed binding disables reuse until explicitly qualified again; configuration APIs revoke active leases. Every frame return, early error and between-frame host import/replacement expires the proof. Serial wrap clears front/sparse derivation. Derived caches, host proof pointers and lease serials are not guest snapshot state. Current guest-state restoration between frames gets a fresh first-use guard; arbitrary serialization of coordinator host pointers is not a supported snapshot format.

Mutable PRG (including protected/banked aliases), Word RAM ownership/modes/1M views, host RAM, Z80 writes, CDC DMA, stamp output and opaque/unqualified owners retain full ordinary live guards. No writer to those regions is assumed immutable. The audited production media worker, audio queues and read-only presentation/debug callbacks cannot write the leased BIOS; unknown callback sets are not automatically qualified. IRQ, trace, fault, WAIT, CPU retirement and device/audio deadlines remain the original contracts.

## Correctness and actual runtime decision

The final immutable implementation passes **10 MSVC and 10 GCC 13.3 gates** using existing fixtures. Cases include exact sparse/front variant precedence, eviction, rebind/user-object revocation, epoch wrap, raw BIOS replacement between frames, early native-MISS expiry, IRQ/trace/reentry/WAIT/fault behavior, active DMA/stamp/Sub self-modification and third-master ordering. The interval-only predecessor separately passed 9+9 gates.

All eight actual Sonic CD 3,000-frame arms match the frozen canonical eleven checkpoints, complete memory/pixels/WAV, execution/drive traces and existing feature counters, with zero execution floors, audio faults or audio queue ordering errors. Tests use SDL software output with dummy video/audio, a fixed logical CPU within each pair, the same native archive/media/settings and shared SERVER Windows capacity reservations. They are ambient-host CPU observations, not uncapped FPS, physical-display or native Linux/Deck game claims.

| Package / order | Arm | CPU seconds, frames1800-3000 | Process cycles |
| --- | --- | ---: | ---: |
| Exclusive interval A/B | A | 13.578125 | 46,276,556,101 |
| Exclusive interval A/B | B | 14.062500 | 47,704,393,082 |
| Exclusive interval B/A | B | 14.578125 | 48,641,866,993 |
| Exclusive interval B/A | A | 14.421875 | 47,520,848,731 |
| Immutable frame A/B | A | 13.937500 | 46,971,427,177 |
| Immutable frame A/B | B | 13.843750 | 46,893,253,980 |
| Immutable frame B/A | B | 13.875000 | 47,004,772,516 |
| Immutable frame B/A | A | 13.359375 | 46,202,084,779 |

The immutable alternative reaches **11,467,628** reuses in both arms. Gameplay differences are small and mixed: CPU reductions +0.67/-3.86 percent; cycle reductions +0.17/-1.74 percent. Baseline A/A drift is 4.15 percent CPU and 1.64 percent cycles. Whole-run candidate CPU is nevertheless 1.60/2.48 percent higher and whole-run cycles 0.68/0.90 percent higher in the two orders. Boot-at-frame300 CPU is mixed (-1.75/+0.99 percent candidate cost). These are neutral/inconclusive ambient measurements, not a demonstrated gain; proof reuse is not presented as speedup. Independent review retains the owner's rule that neutral games are acceptable and shared baseline defects are not disqualifiers.

Final immutable PE SHA256 `a1898a6470b7773deb7506e368edac54b6624e7d128c98f68e8557bdbbfa980e`, 115,855,872 bytes, versus baseline `80d9a08ddd92d13a0ced8150abe207c6da10851e67eb5efb9692f6603bf99ad2`, 115,852,288 bytes (+3,584). Private memory differs by +434,176/+409,600 bytes in the two pairs. Build/source/binary identities, full raw samples, startup/whole costs, memory and capacity telemetry are retained in the `followup_2026_11_01` object in [the existing results file](engine-code-identity-results.json).

Local evidence and reproduction jobs: `C:/Temp/Codex/genesis-engine-code-ownership-20261101`. Complete raw game artifacts, frozen PEs and independent source/build cache: `G:/Temp/Codex/genesis-engine-code-ownership-20261101-server`. Small worktrees/unit caches and both build inputs remain for independent disposition; no delivery folders, owner saves/settings, unrelated jobs or frozen E35 binaries were changed. The accepted E35 runtime remains the integration base until independent review explicitly adopts the new candidate. This bounded evaluation does not assert that every possible code-ownership algorithm has been disproved.

## Earlier physical-page identity package (rejected)

E02's physical-page code identity package passes correctness, but the final built Sonic CD candidate costs more CPU time in both matched pairs. It is rejected. No optimization PR, merge or release is authorized by these results. The broader E02 opportunity remains open; a rejected mechanism is not exhaustion of every possible design.

Owner: `beads-3z5k.1`, shared core `beads-rxq.3`, remaining work `beads-3z5k`, campaign `beads-2wy8.1`. Rejected engine code `c9db7a1847597f63d1cc294cea55d91940ec0091`, runtime core `55208206dceb126d2e0ae84422382b07292d38be`, consumer `94f57796db9de4990e23476b576b29239095fb22`. Baseline is qualified E01 engine `aec3c9c`/core `34e5aee`, not the older released build. Instruction generator core remains `7233852`.

## Implemented and evaluated contract

The package replaces repeated live-word comparisons with exact guarded identities over watched physical 512-byte pages. It covers positive and negative variants, opcode and extension words, mappings/ownership, Main/Sub/host RAM, external Z80 writes, DMA and stamp writers. Cold or changed identities still compare every current word. Unknown callbacks suspend reuse and fence their direct writes; imports/restores between machine-run leases get a fresh root. Serial wrap clears derived state.

Only independently qualified generated register/context-bus operations participate. The shared runtime adds optional identity/observer hooks and a tracked entry point while retaining the operation/entry ABI and live guarded fallback. Sample IO qualification captures the exact function and user object and retains the original per-call callback snapshot semantics. No sample evolution, output, device deadline or audio quality is omitted.

The ownership contract is explicit: generic coordinator callers must qualify host bindings or provide fences. The production machine/standalone bindings are audited; arbitrary unknown coordinator callbacks are not automatically made safe. Noncontiguous 1M Word RAM deliberately exits the cache lease and retains E01 live guards. This is a portable fallback, not a claim of caching every hardware mode.

## Due diligence

All 19 targeted MSVC and 7 GCC gates pass. Coverage includes cached mismatches/variant priority, cross-page extension writes, unrelated/cold data pages, physical aliases, remapping and ownership WAIT, 1M exit/resume, external writers, direct opaque writes, nested CPU execution, faults, imported/restored bytes, serial wrap, IO rebinding and exact output-only callback/user qualification. Existing native-region tests compare identity off/on with active DMA/stamp/timer and DMA self-modification. An initially missed external-master write was reproduced with a failing regression and fixed before the final build.

The final balanced B,A,A,B game run passes all four 3,000-frame comparisons: eleven checkpoints, full memory/pixel dumps, complete WAV, execution totals and CDD timing match the frozen reference. All have zero fallback floors, audio faults and queue ordering errors. GCC contracts establish portability coverage; no native Linux/Deck game performance qualification is claimed.

## Runtime result

| Final v6 arm | Process CPU seconds, frames1800-3000 | Process cycles |
| --- | ---: | ---: |
| B | 6.765625 | 31814942020 |
| A | 6.28125 | 31013492850 |
| A | 7.609375 | 35633427410 |
| B | 8.09375 | 36377611310 |

Candidate CPU time is 7.71% and 6.37% higher in the paired comparisons; cycles are 2.58% and 2.09% higher. Baseline A/A drift is 21.14% CPU and 14.90% cycles on this shared host. These observations do not prove a universal slowdown, but provide no basis to adopt this package or claim a gain. Earlier v1/v4 mixed samples and v2/v5 regressions remain separate; they are not pooled into a favorable headline.

The final EXE is 111,285,760 bytes versus 111,279,104 (+6,656); the derived tracker is 82,072 bytes. Final counters report 135,272,571 cache hits, 22,359,384 full checks, 1,618,428 watched writes and 1,859,611 fences. Fewer comparisons/write epochs are participation counters, not a speedup. Metadata generation took an observed 940.8 seconds, not a matched build-time result.

The build and complete matched run used shared capacity reservations from `benchmark_hosts.py`, retaining before/after CPU/GPU/RAM/storage telemetry. The build was limited to one compiler worker (CPU1/RAM4GiB); the test process was restricted to one logical CPU (CPU1/RAM2GiB, GPU0 with SDL dummy output). Other owner work was left running. Both Windows hosts and Deck were probed; this built package required Windows, and no Deck build/backend was qualified.

## Disposition and evidence

Stop this implementation approach after six variants. The latest owner workflow permits documenting unsuccessful outcomes and moving to the next family after independent validation. Do not check E02 off as fully exhausted. No E03 work starts before that review gate. Continue subsequent implementation from qualified E01 so the rejected runtime package is not carried into new candidates.

[Machine-readable results](engine-code-identity-results.json) retain source/binary identities, every variant's raw samples, capacity receipts, costs and qualification limits. Raw game traces, captures, WAV, build receipts and frozen binaries: `G:/Temp/Codex/genesis-engine-code-identity-20261028`. Reproduction scripts, test XML, host probes and checkpoints: `C:/Temp/Codex/genesis-engine-code-identity-20261028`. Delivery folders and owner saves/settings remain untouched.
