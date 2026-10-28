# E01 native execution regions: completed serial run

E01 is implemented and qualified for the reusable CD machine. Engine code `382678d28c02255bb1a79d271cb730c4d8902277`, shared core `34e5aee23348199adfc6eda9a92b9632138b9884`, validation consumer `94f57796db9de4990e23476b576b29239095fb22`. Owner/campaign: `beads-ufzc`, shared core `beads-7yle`, remaining parent `beads-3z5k`, campaign `beads-2wy8.1`.

This closes the original E01 deadline/observer region contract. It does not close code invalidation (E02), data-region ownership (E03), emitter register/flag work (E04), generated control-flow fusion (E05), or the broader wait/scheduling goal (E07). Those remain unchecked. No second task has started.

## Implemented contract

- Closed generated-source effect qualification emits optional immutable module sidecars. Only register/charge work with no calls, pointers, external storage or exception/device effects qualifies. Unknown text and arbitrary HLE callbacks remain exits. The original operation, entry and module ABI stays unchanged, allowing the qualified operation archive to be reused.
- Main/Sub regions retire eligible native instructions in the exact old CPU order, checking every live opcode/extension word. No stale code-identity cache is introduced. Trace, IRQ changes, absent owned instruction bytes, bus activity, ownership WAIT and unknown callbacks retain the original execution boundary.
- The qualified Z80 interval retains live RAM reads, timed writes, instruction IRQ/EI behavior and completed-state publication. Isolated RAM observation may settle CPUs without repeatedly publishing unchanged devices. Every due hardware event, MMIO/external access, terminal miss and interval return publishes the required exact clock state. Generic unqualified `step()` callers retain ordinary synchronization.
- Time sinks must be interval-composable and host observers must advertise every observable deadline. The known shared machine qualifies that contract; instrumentation expecting a callback on every old advance call must use ordinary stepping. All code is portable C with the existing universal operation path.

## Due diligence and rejected work

The first complete build preserved all outputs but increased CPU use by 23.3%/20.8% in its matched pairs. It was rejected. The next layout reduced repeated clock publication, but the full game found timing/WAV differences. A CPU could overshoot a third-master ceiling; after that ceiling advanced, a due event had to publish before another instruction, not after it. The new staged-ceiling test failed before that fix and passes afterward.

Final exit review also reproduced a terminal Z80 miss leaving completed device time unpublished, and restored the full `run_frame` reentry contract. These cold exit fixes are in the v4 artifact, which then repeated a 2.5/3.1% CPU regression. Final v5 folds the validated execution context together: it removes duplicate region-entry validation/third-master clock calculation, reuses fixed IRQ levels within an unobservable interval, and avoids repeating the first descriptor lookup. Older and final package/binary identities remain separate.

Validation passes: eight targeted MSVC gates, four effect/catalog tests, and six GCC gates. Coverage includes 20,000 CPU retirement comparisons, 1,308 scheduler/observer cases with active DMA/stamp/timer and DMA writes into executing code, 109 Z80 interval deadlines, callback priority/qualification replacement, trace/fault/WAIT/IRQ behavior, terminal misses and machine reentry. Final built-game checks cover all 3,000 frames, eleven inspect/picture checkpoints, complete memory/pixel dumps, full WAV and real execution totals. Zero fallback floors or audio/queue faults; exact reference equality.

## Performance and costs

**Runtime result: positive on the measured Windows Sonic CD workload.** Final v5 pairs observed 3.08% and 8.68% less process CPU time, and 8.57% and 7.17% fewer process cycles. A/A drift was 8.38% CPU and 1.72% cycles; both pairs improve, with larger/consistent cycle reductions. This is bounded shared-host evidence, not an FPS, universal host or other-game claim. Earlier v3 mixed timing remains neutral/inconclusive and is not pooled into these final results.

| Final v5 arm | Process CPU seconds, frames1800-3000 | Process cycles |
| --- | ---: | ---: |
| B | 8.859375 | 40727693960 |
| A | 9.140625 | 44543001210 |
| A | 9.90625 | 45310798850 |
| B | 9.046875 | 42059945620 |

The final EXE is 111,279,104 bytes versus 109,482,496 bytes (+1,796,608). Immutable effect bytes and slightly larger derived lookup slots are material costs; sparse lookup keeps its existing 8 MiB per CPU bound. Metadata generation took an observed 435.6s on this run; this is not a matched generator timing comparison. No audio/render quality was reduced.

37 million 68000 retirements used the region fast path on this route. That establishes participation, not new native coverage or a speedup; the original build was already native. The current package affects CD execution; cartridge runtime and common instruction-emitter sources are unchanged.

## Evidence and handoff

Machine-readable source/binary/package identities, raw samples, host/settings/costs and rejected experiments: [engine-native-region-results.json](engine-native-region-results.json). Full raw traces, dumps, WAV, logs and manifests: `G:/Temp/Codex/genesis-engine-native-regions-20261028`. MSVC/GCC test logs/XML and reproduction scripts: `C:/Temp/Codex/genesis-engine-native-regions-20261028`.

One current private review candidate is prepared outside delivery folders at `G:/Temp/Codex/genesis-engine-native-regions-20261028/owner-review/Review-E01.ps1`. It launches Sonic CD in a normal window with normal audio, isolated settings/native saves and local Pass/Fail notes. It is manually invoked; existing owner games remain untouched. The new batch is not merged or released. Check in with the owner after this task before starting another goal.
