# E22: owned Sega CD PCM/CDDA source spans

The selected engine contract is qualified for a PR, with a neutral/inconclusive
performance classification and lower CPU observations. It is not a proven
general speedup. Final record/durability review and the combined owner playtest
and merge handoff remain pending. Owning issue: beads-3z5k.20; campaign:
beads-2wy8.1. Exact measured source: a8f45ad36bf1c38d4890dc0d0c27ed727595b73b.

PCM voice jobs, CDDA FIFO/fader work and timestamped mixer queues share a
synchronous ownership contract. The producer writes directly into a reserved
queue span and retires state and queue accounting together. No device or queue
observer can run during a grant. Replacing either legacy callback binding
revokes eligibility before its old user context can be dereferenced. Opaque or
reentrant callbacks, unsupported state and refused grants retain the original
sample path, successful queue prefix and native first-failure behavior.

The contract handles one due sample as well as longer blocks. The first real
game run exposed why this matters: the original 768/568-clock gates produced
zero owned jobs despite passing long-block fixtures. CPU/Z80 bus boundaries
often publish shorter intervals; regionAdvances counts only a subset of device
service. Phase-aware dispatch and explicit one-clock crossing tests fixed the
integration. Scheduler intervals and guest device publication were not enlarged
or deferred to manufacture batches.

Complete RF5C164, CDDA, native public statistics and mixer state layouts remain
unchanged. SegaCDIO appends host capabilities; the native owner adds private,
optional diagnostic counters. Existing generic APIs and portable paths remain.
GENESIS_CD_AUDIO_SPANS enables log-only path counters, without changing window
titles or public inspector output.

## Correctness and actual game coverage

The independent oracle is the frozen original RF5C164/CDDA implementation from
d8685cdc08a986a54cbec06ce56c9073292f19ed. Cases cover complete state, samples,
timestamps, arbitrary wave/register writes and loops, FIFO wrapping, fader fields,
seek/pause, underflow, queue boundaries and terminal failure, binding replacement,
opaque reentry and reused partial advances. Explicit one-clock crossings require
an actual one-frame grant. Native integration checks compare attached ownership
with legacy publication and verify first-FULL failure and later suppression.

The two changed source/native checks pass on x64 and Win32 at the measured head.
Four unchanged focused device/bus/mix checks reuse the passing earlier suite.
The Win32 build found two signedness warnings in new capacity checks; explicit
size_t arithmetic fixed them before the measured build. Retained binaries,
configurations and their relocation map are in the machine-readable record.

Eight qualified Sonic CD runs form four matched pairs: AB, BA, then ABBA on one
fixed core for that complete balanced run. Every arm matches all 11 frozen
inspection checkpoints, 21 captured artifacts and the full WAV, with no audio
faults. Each candidate really uses 1,629,702 PCM grants/frames and 2,207,839 CDDA
grants producing 2,207,841 frames. An earlier inactive-path integration pair also
matches output but contributes no matched performance result. CD rollback or
save-state qualification is not claimed.

Tests use the existing hidden/muted protocol, real audio emulation, disposable
backup/settings and matched affinity/features. CD uses the existing qualified
consumer catalogs, native archive and UI, MSVC 19.41 x64 and SDK 19041. All
new SegaCDIO consumers compile against consistent headers. Cartridge compiled
inputs are unchanged; no new cartridge measurement or gain is claimed.

## Performance decision

Negative cost changes mean lower cost. All rows are retained; the adverse
reverse-order result is not hidden by pooling the runs.

| Pair | CPU cost change | Cycle cost change | Interpretation |
| --- | --- | --- | --- |
| Initial AB | -11.63% | -5.67% | Favorable observation |
| Reverse BA | +11.46% | +11.53% | Adverse observation; substantial order/host variation |
| Balanced ABBA, first pair | -7.92% | +1.40% | CPU lower; cycle difference within observed variation |
| Balanced ABBA, second pair | -4.14% | -5.31% | Favorable observation |

In the balanced run, baseline CPU drift is -6.85% and cycle drift -0.75%; the two
candidate cycle samples differ by about 7.3%. This supports a neutral candidate
with lower CPU observations, rather than a robust cycle or universal CPU win.
The earlier adverse result did not reproduce in balanced order. Paced delivery
remains about 20 seconds for the 1,200-frame window, so this is not an uncapped
FPS claim. Captures/checkpoints remain part of that workload. Startup and memory
observations show no repeatable material regression; their raw samples remain
available without a new startup or footprint claim.

Independent reviewer validate_e22 directed one final existing-protocol balanced
run, then accepted this narrow neutral classification and stopped further timing
or tuning. Per-game neutral is allowed. E23 synthesis-to-device fusion and E25
constant PCM/CDDA spans remain separate open work; this result does not close
them or claim every future source-span design is exhausted.

Raw evidence: G:/Temp/Codex/genesis-engine-cd-audio-spans-20261031. The JSON
record retains exact sources/binaries, commands, per-game samples and hashes.
383,531,384 bytes of disposable local unit builds and duplicate SERVER binaries/
source ZIP were removed after retaining test PEs/configuration, game binaries
and raw evidence. One local source ZIP and the consistent SERVER source/build
cache remain for immediate E23 reuse; clean that bounded scratch after campaign
integration. No owner windows, release replacement or merge occurred.
