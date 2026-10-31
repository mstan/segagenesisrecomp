# E15/N19 independent final validation

**Verdict: ACCEPTED as complete evaluation and bounded rejection of the original E15 opportunity.** Neither render cache is accepted for production. The useful generated-C portability fix and its real-output regression fixture are ready in [PR47](https://github.com/mstan/segagenesisrecomp/pull/47), stacked on PR46. This is no merge, release, delivery or owner-playtest authorization.

Reviewed head: `199a313b363b6c6a61ac397b100c22f061e3f52b`. Production source: `a20ecc5fab83ac0cef53737a0ec1d8f72cd16894`. Frozen baseline: `426253e1b76056a45c48347ddcd75eb64e31c1d8`. Reviewer: `validate_e15_resumed`; validation only, with no implementation, builds, profiling or game launches delegated to this reviewer.

## Original scope and disposition

The full original opportunity covers generic current hardware render jobs, regions and results, preserving guest execution, sprite/status/callback timing, audio, direct/port/DMA/import/alias/private-provider writers and output lifetime. It is not restricted to a single cache or title scene.

R1 implemented complete indexed native results and owned captured-plane results. R2 coherently removed native overhead and independently cached only canonical current captured planes, leaving sprite/status/color metadata fresh and allocating lazily with safe failure fallback. Both were qualified and reached actual built consumers. Native and capture opportunities were evaluated independently; cartridge custom rendering was correctly identified as a native-row consumer rather than shared capture. Source and directed qualification address palette freshness, overflow/collision re-hit, callbacks, same-address mutations, current-build import/restore, aliases, ownership/lifecycle, portable paths and allocation failure.

The source and actual-game evidence justify rejecting these two architectures rather than deferring part of E15. Current-input derivation and exact comparison add substantial work to an already packed renderer. A frame-end identity or guest/status skipping cannot replace the required timed input history. This disposition does not claim that every possible future rendering algorithm is impossible or optimal. After two unsuccessful coherent architectures, further local tuning is unwarranted.

## Built behavior and per-game costs

All eight reported clean pairs match their raw same-host samples. Clean timing excludes diagnostic counters. Frozen source/binary identities, regeneration inputs, compiler/flags, configuration, reservations/telemetry and raw samples are retained; results from different packages or hosts are not combined.

- R1 native RKA was adverse in both AB and focused BA orders: CPU `4.328125 -> 4.531250` seconds and `4.375000 -> 4.671875` seconds, with cycles also adverse. Neutral or mixed Sonic 1/custom-renderer observations cannot offset that reproducible per-game cost. R1 CD's single PC AB sample also establishes no useful gain; there was no R1 SERVER runtime sample.
- Actual R1 CD capture reuse was demonstrated independently: 105,003 hits among 616,816 requests. R2 demonstrated 496,192 capture hits among 616,816 requests, with 4,002 misses and 116,622 backoffs, using 1,977,600 heap bytes. A high hit rate did not yield a useful built-game improvement.
- R2 CD PC BA was adverse in CPU and cycles (`5.875000 -> 6.062500` seconds). SERVER AB was mixed (`13.125000 -> 13.484375` seconds, slightly fewer cycles). These bounded shared-host observations justify no speedup claim; they do not establish a physical-presentation, startup or loading improvement.
- Retained clean outputs and diagnostic outputs match canonical machine/RAM/image checkpoints and full emulated WAV. Cartridge rollback and current-build save/load/progression checks pass. Both CD architectures match all eleven canonical checkpoints. R2 cartridge binaries were built but not runtime measured; no R2 cartridge performance claim is made.

Matched-source adaptations are fair and explicit. Both cartridge arms received identical task-only empty UI initializer changes and the same generator zero case. All four actual generated layouts have identical semantic bodies. CD uses the same qualified native archive and common runtime inputs with exactly one VDP source override; the 11,290 expected source/header hashes were independently compared with the actual archive manifest. SDK warnings were scoped to external SDK/CRT headers while first-party warnings remained enabled. Original failed attempts and corrected diagnostic assumptions remain available.

## Surviving production change and qualification

Every final `runner` source byte matches frozen `426253e`; rejected prototype sources remain archived separately. The only production code change emits `0` when `level_mode_count` is zero, matching the existing widescreen-mode initializer policy. Counts and runtime semantics are unchanged. The fixture invokes the real emitter for empty and populated configurations, compiles the resulting C11 and checks counts, values and zero-filled tails. Actual SERVER MSVC19.29 x64 and Win32 fixtures pass, with normal test registration and a cross-build host-execution guard.

R2's ten gates passed on MSVC x64, actual Win32 and GCC. The corrected nonzero-counter fixture subsequently passed all ten on SERVER x64 and Win32 with runtime source unchanged. The initial GCC run is not misrepresented as a rerun of that fixture correction. The initial R1 frozen-reference, directed and expanded qualification evidence remains intact.

The final independent recheck verified 392 distinct files totaling 1,982,691,713 bytes, including all 132 final SERVER retention entries, actual test PE architectures, frozen game PE/audio/image/RAM identities, all eight reported clean pairs, PR readiness and durable Beads milestones. Earlier dedicated cartridge and R1-CD audits supply the detailed initial-source/runtime checks. No unresolved objective finding remains. The R1-CD prose scope error and R2 counter-fixture error were corrected before this verdict.

## Records, cleanup and next instruction

PR47 is open and ready, correctly based on `codex/engine-stamp-jobs-20261029`. Owning `beads-3z5k.12` remains claimed by `codex`, with final milestones durably recorded on that issue, parent `beads-3z5k` and campaign `beads-2wy8.1`. Final status/count changes were intentionally held for this review.

Audited nonforced cleanup removed 14,095,398 bytes from four owned local test build roots; forty actual test PE/config/log files were retained and independently verified. Raw evidence is intact. Bounded SERVER portable tools, CD source/catalog/native archive and supported build trees remain explicitly retained for E16/campaign reuse; the recorded WSL temporary unit build still needs evidence-preserving cleanup when no longer needed. These are explicit cleanup obligations, not hidden abandoned scratch or a performance acceptance claim.

**Root instruction:** record E15/N19 as accepted `bounded_rejected`, update the original remaining-family count from 20 to 19, attach this verdict and the immutable audit receipt to PR47 and the owning/parent/campaign records, finish the remaining bounded scratch retention/cleanup disposition and central sync, then proceed to original **E16 with a NEW validation-only reviewer**. Authorized E16 preparation may be reused; its qualification, independent review and disposition remain the next serial gate. Do not reopen accepted earlier families or relaunch cleared owner playtests for this disposition.

Immutable machine receipt: `VALIDATOR_E15_FINAL_AUDIT.json`. Subsequent identity rechecks must write a separate receipt, preserving this reviewed-head audit.
