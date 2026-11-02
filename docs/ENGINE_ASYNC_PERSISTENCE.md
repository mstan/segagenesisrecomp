# E35: immutable persistence and capture ownership

Selected runtime: `ccf939f`, base `630a953` (qualified E34 runtime `81f7682`). Owner: `beads-3z5k.30`, related `beads-3vb.11`; parent `beads-3z5k`, campaign `beads-2wy8.1`. [Machine results](engine-async-persistence-results.json) retain exact builds, inputs, settings, raw observations and rejected experiments. Final combined title builds, owner playcheck, merge and release remain separate.

## Bounded result

The complete recurring-output audit found twelve families. Five retain immutable host ownership; seven keep their original synchronous text writers after implemented alternatives failed the combined gameplay workload. These are individual dispositions within the original E35 family, not twelve additional campaign items. This rejects the evaluated preformatted-text package, not every conceivable future text algorithm.

| Recurring family | Selected disposition |
| --- | --- |
| Mixed PCM WAV | Two fixed owned buffers, bounded backpressure, worker writes; drain before RIFF seek/header finalization and close. |
| Pre-DRC FM/PSG PCM stems | Copy original stereo/mono bytes before scratch reuse; drain at normal and scripted exit. |
| Four runtime-evidence TOMLs | Deep-copy entries/notes and freeze paths/frame metadata; coalesce requests and acknowledge only successful matching generations. |
| Cartridge SRAM | Preserve geometry, dirty hash and thirty settled frames; publish immutable bytes, acknowledge actual durable bytes on the producer, and settle before guest/session/reset boundaries. |
| CD BRAM | Preserve sixty-frame change detection and exact 8192 bytes; immutable publication, completion acknowledgment, atomic replacement and final latest-data drain. |
| Yield text log | Original writer retained; part of the rejected integrated text-ownership package. |
| FM callback text trace | Original writer retained; same bounded package rejection. |
| Memory-write text trace | Original writer retained; same bounded package rejection. |
| Main full frame log | Original `fprintf` and per-frame `fflush` retained, including its full WRAM hash and cap. |
| Game-fiber frame log | Original capture point, formatting, cap and per-frame `fflush` retained. |
| Pushed chip-write text dump | Original push-time text writer retained. |
| Applied chip-write text dump | Original post-sort/spread/clamp merged order retained in both serial and split paths. |

The implementation reuses `GenesisWorker` and its inline portable fallback. PCM storage is bounded to two buffers per stream; a full pair waits rather than dropping accepted bytes. Borrowed FILEs remain owned by the producer until drain completes. Static capture streams drain before CRT closes files, including when SDL has already shut down. Actual write/flush failures remain observable.

Native persistence separates accepted, in-flight and committed images. Failed writes never advance the committed marker. Shared UTF-8 atomic replacement preserves the prior valid file on failure. SRAM callbacks execute on the producer with the actual committed bytes, before another guest frame or session transition; allocation/worker failure retains a synchronous path. Native save contents/formats remain intact.

Two objective SRAM defects were fixed: guest cleanup could shut down netplay before checking host-only save authority, and scripted EXIT could miss changes made during its last frame. Authority and pending work now settle before role destruction/reset; EXIT refreshes dirty state and completes native saves and WAV output. The benchmark-only read-only contract remains separate from normal runtime measurement.

Runtime evidence previously wrote new addresses synchronously and could lose dirty retry state on commit/replacement failure. Workers now use independent immutable generations; old completions cannot erase newer entries. Commit failures preserve previous files and pending changes without spinning every frame. Final flush includes latest data and drains its worker.

## Evaluated and rejected text ownership

The first complete twelve-owner implementation (`7e41228`) matched all game outputs but slowed combined Sonic 1 capture twice: CPU +3.55% and +5.88%. Individually, the bulk and frame-log groups appeared beneficial, so the existing worker was shared across streams as a second implemented architecture (`6d89343`, with the ownership/test clarification `018c3b4`). That still regressed CPU +13.97%, cycles +7.68% and whole-process CPU.

Restoring only synchronous frame logs did not solve the interaction: the reverse pair regressed CPU +15.7% and cycles +13.35%. Disabling only the pushed/applied dumps also left CPU +5.88% and cycles +8.04%. Those results selected rejection of the evaluated text-buffering package instead of another scheduler/formatter project. They do not establish seven independently isolated regressions. All original text paths were restored before qualifying the retained package.

One repeat was discarded because its baseline gameplay diverged while the candidate matched earlier runs. Existing input scripts intentionally merge live devices. Subsequent private profiles set all live input devices to zero while preserving scripted masks; no production input behavior or owner settings were changed. That mismatched pair is not performance evidence or an engine regression.

## Actual games and measurement limits

Final MSVC x64 games use frozen native consumers, matching ROMs/scripts, software SDL rendering, dummy audio output with full audio emulation, raw screenshots and rollback probes. Each pair fixes the same two measured physical cores on this shared PC. Default runs and all-enabled capture runs are distinct. Every retained candidate pair matches complete guest/audio state, frame hashes, raw PNG, rollback and all enabled capture bytes. No title windows were activated for unattended checks.

These shared-host samples qualify correctness and record costs; **they are not isolated FPS improvement estimates**. Small mixed results are neutral/inconclusive, not proven wins. No game's regression is offset by another game's gain.

| Game / mode | Observed CPU change | Observed cycle change | Interpretation |
| --- | ---: | ---: | --- |
| RKA default, BA / AB | +0.34% / +3.44% | +1.27% / +1.05% | Small shared-host signal; no default gain claimed. |
| RKA all captures | -10.85% | -8.75% | Beneficial observation, not an isolated effect size. |
| Sonic 1 default | 0.00% | -0.26% | Neutral. |
| Sonic 1 all captures, AB / BA | -8.33% / +4.29% | -5.29% / +0.63% | Mixed, neutral/inconclusive. |
| Sonic 3 & Knuckles default, BA / AB | +10.38% / -0.34% | +12.46% / +2.51% | First adverse observation did not repeat in CPU; retain both, no improvement claim. |
| Sonic 3 & Knuckles all captures | -6.80% | -12.60% | Beneficial observation, not an isolated effect size. |
| Sonic CD real BRAM-format route | -11.11% | -1.25% | Contended SERVER observation; neutral/inconclusive. |

The exact linked CD source/helper/header identities match the previously built final `7e41228` CD package. Its actual controller-only BIOS route formats an FF backup image: both builds already contain the same changed native BRAM file at frame1322, before quitting. All eleven checkpoints, twenty-one artifacts, full WAV and guest/native/audio counters match. The retained package's cartridge-only text restoration does not change that CD build; exact source comparison supports reuse instead of repeating gameplay.

Sonic 3 & Knuckles uses a real native save slot. Both arms perform two actual 16384-byte flushes before the final frame and produce identical native SRAM bytes; this is not an exit-only save check. The title's separate post-commit campaign journal is recorded under T14 in [the title backlog](TITLE_OPTIMIZATION_BACKLOG.md), outside the engine goal.

Independent evidence-path observations selected the ownership change: an original synchronous 1024-entry write blocked its producer for18.697ms on G:, while current publication took0.086ms and durable completion8.559ms. These are component observations with raw samples, not a claim that durability became free or a matched game FPS gain. Ordinary clean ticks perform no file writes. Original four-file synchronous counter publication was39.464ms in the earlier observation.

Capture working-set/commit costs remain explicit: the final observed RKA capture peak commit was163971072B versus162242560B, about1.65MiB higher; default memory was effectively unchanged. The machine record retains every game's whole-process CPU, sampled memory and full costs, including startup/teardown. Initial partial-package capture improvements are retained as historical evidence and are not presented as measurements of this final package.

## Other output boundaries and qualification

The audit also reviewed settings/ROM selection, explicit states/thumbnails, diagnostic/image exports, shutdown coverage, crash evidence, startup-only media snapshots and directly compiled dependencies. These have explicit event/return/completion contracts rather than recurring gameplay output. Preserve their synchronous visibility and error results. Actual representative157106-byte RKA native state saves completed in1.35-2.60ms including the local control round trip; that does not measure hypothetical large optional title payloads. Existing E32 handles shared snapshot ownership. Fatal crash evidence remains synchronous best effort when the runtime/worker may be unhealthy. No additional recurring dependency file sink was found.

Final existing targeted CMake/CTest gates pass16/16 under MSVC and16/16 under Ubuntu/GCC13.3, including real workers, forced inline fallback, immutable caller mutation, bounded backpressure/order, write failures, accepted-only versus latest flush, commit/replace failure recovery, producer callbacks, atomic save replacement, current audio integration and exit ordering. Independent reviewers separately exercised stream/evidence ownership and SRAM callbacks/authority. A synthetic zero-byte test gate originally raced its preceding data job; that fixture was fixed and given a30-second timeout. Its old hang is not an engine deadlock result.

The helper, persistence and evidence paths are portable C with the existing conditional worker implementation. Linux fixture qualification does not claim Linux native-game performance. Source/binary identities, raw paths, host telemetry and input settings are retained for the combined campaign handoff.

Ready [PR59](https://github.com/mstan/segagenesisrecomp/pull/59), stacked on PR58. Cleanup removed189 verified agent-owned per-run executable/DLL/ROM copies (956112896B). Native saves, settings, raw evidence and frozen stage binaries remain; isolated build caches are retained for the ongoing combined handoff.
