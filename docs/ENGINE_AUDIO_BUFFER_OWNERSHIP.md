# E24: Audio buffer ownership

Original E24 (`beads-3z5k.22`) is qualified as a bounded delivered contract in [PR52](https://github.com/mstan/segagenesisrecomp/pull/52). Overall game timing is **neutral/inconclusive**; no general game speedup is claimed. Independent qualification accepted the source and per-game evidence. E25 starts only after the final source/records/cleanup durability review is recorded in the owning issue.

CD FM/PSG current/pending frames now exchange two private slots after successful flush. The same render-before-flush and one-frame source lookahead remain. Host audio prepares one immutable normalized F32 block after the original extra-source, pre-volume WAV tap and integer gain; it publishes at most two contiguous ring spans under the existing SDL lock when exact-count/no-overflow checks prove equivalence. The unchanged scalar push handles overflow and large counters. Shared callback controller/history/read cursor state still requires exclusion. Taking bridge telemetry under that same lock fixes an existing data race; event logging stays outside it.

Cost: **128 KiB extra fixed host scratch and four private CD pointers**. No new thread, callback allocation, SIMD/assembly requirement or simulation-profile change. This closes the evaluated handoff contract, not every possible future lock-free architecture.

Measured production source: `42130bc407077d11b3f17ae63cbf7152d2e9386b`; final tests: `0f032082634c4dc3b146e484491e4e54f9a79b22` (runner byte-identical). Baseline cartridges use qualified fa696e2; CD uses accepted E22 PE9d0c4625 and matching19.41 toolchain. Cartridges use matching19.44. See [machine-readable results](engine-audio-buffer-ownership-results.json) for complete identities, raw paths, configurations, samples and retained binaries.

Five meaningful focused contracts pass on both x64 and Win32: complete original-S16 bridge state/ring/pull parity including gains/wrap/overflow/2^53/refusal/NaN/conceal/recovery/reset; actual production gained bridge/pre-volume WAV and lifecycle; extra-source ordering; CD mixer; six changing FM/PSG slot rotations, frame-late metadata and fatal failure paths.

| Game/profile and order | CPU cost change | Cycles cost change | Decision |
|---|---:|---:|---|
| rka shadow-off dummy100 AB | +18.90% | +2.75% | neutral/inconclusive |
| rka shadow-on dummy100 AB | +4.17% | +1.87% | neutral/inconclusive |
| sonic1 shadow-off dummy100 AB | +10.96% | -2.88% | neutral/inconclusive |
| sonic1 shadow-on dummy100 AB | -26.44% | -2.65% | neutral/inconclusive |
| rka shadow-off dummy100 reverse BA | -8.97% | -0.65% | neutral/inconclusive |
| rka shadow-off wasapi0 AB | +16.92% | +9.41% | neutral/inconclusive |
| rka shadow-off wasapi0 reverse BA | -22.22% | -3.57% | neutral/inconclusive |
| sonic_cd qualified native CD dummy100 AB | -1.27% | +0.28% | neutral/inconclusive |

All16 actual game runs in8 matched pairs preserve recorded pixels/full WAV and canonical observations; cartridge state/audio hashes and current-build rollback match. CD retains11 checkpoints/21 artifacts/full WAV against frozen expectations. CD save/load/rollback is not claimed. Paced delivery was used, not uncapped FPS. Each pair has fixed host/features/affinity with raw headroom and costs; different pairs are not pooled into a fabricated win.

The actual WASAPI pair and targeted reverse use silent task-local master0,48000Hz/1024frames/stereo/S16. The initial adverse physical CPU/cycle result did not reproduce; the reverse is favorable. Existing baseline hitch records show unrelated simulation/present/pacer stalls and concealment. Candidate callbacks are active with no underrun or overflow in both physical runs. Missing baseline peak/held measurements remain unavailable. Candidate-only snapshot/publication exclusion timing does not establish a matched physical lock-duration or jitter improvement.

Informational same-input paused-dummy component samples (five alternating-order rounds,2000 blocks each) reduce x64 original push body6.659?9.323us to preparation+publication0.824?1.310us; Win32 original21.603?26.929us to0.959?1.193us. These are local component costs and are not added to whole-game gains.

No new profiling matrix or stress gate is needed. Current title/unit PEs, configuration, source and raw per-game records are retained outside delivery folders. Audited non-forced cleanup removed591591084bytes of disposable build/duplicate-bin/ROM/staging data on this PC and SERVER;10 final unit PEs, current title PEs/configs and all raw logs/PNGs/WAVs/settings remain. Shared SERVER S22 source/build cache is retained for the next serial audio item and is due cleanup after the campaign. Combined owner visual/play approval and merge/release remain pending.
