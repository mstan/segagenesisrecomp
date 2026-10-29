# E02 code identity: unsuccessful package, not adopted

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
