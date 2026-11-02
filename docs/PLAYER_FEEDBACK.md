# Replayable player feedback

The shared runner includes **Mods > Diagnostics > Replayable feedback** in
production builds. It defaults to **on**, including upgrades whose settings
file predates this option. No debug build, TCP listener, or Developer option
is required. Games without a custom renderer receive the same Diagnostics mod.
An explicit saved opt-out remains respected; `GENESIS_REPLAY_RECORD=0` also
disables recording for one launch.

After a problem, quit normally and zip the game's files together from `logs`:

- `<game>-feedback.replay`
- `<game>-feedback.replay.sav`
- `<game>-feedback.replay.config.ini`
- `<game>-feedback.replay.mods.txt`

Include what happened, the character/mods, stage/act, and roughly when it
happened. Attach every sidecar. Keep source ROMs out of the report. Reports
include the session's starting native save data, declared campaign-save context,
and quickstates loaded during play. Nothing is uploaded automatically.

The current session and two previous sessions are retained. Previous names
append `.previous-1` or `.previous-2` to `.replay`, before the sidecar suffix.
Each session is bounded to 64 MiB of execution records and 32 MiB of state
attachments; the fixed writer queue is 16 MiB. If storage cannot keep up or a
limit is reached, recording stops with an explicit warning and gameplay
continues. This preserves the boot prefix; arbitrary old inputs cannot be
discarded while keeping a replayable native call stack. Restart the game to
start another report after a limit warning. Abrupt termination can lose the
last 100 ms of buffered records. Interrupted reports are explicitly incomplete.

Reproduce with the exact recorded executable and an owned ROM:

```powershell
python tools/replay_diagnostic.py capture.replay --check
python tools/replay_diagnostic.py capture.replay --exe path/to/Sonic3KRecomp.exe --rom path/to/sonic3k.bin --headless --work-dir path/to/new-reproduction
```

Omit `--headless` to watch in a normal window with audio. Playback restores
effective provider features/options and the drawable dimensions/video policy
of each execution, so Adaptive width does not depend on the reproducer's
monitor. Referenced mod source assets must remain available to that build.
The helper uses a new isolated settings/save directory; native battery-save
writes are disabled during playback. Keep the resulting `replay.log`.

Playback rejects a different executable/ROM, damaged attachments, malformed
records, and missing checkpoints. It stops at the first CPU/scheduler, work RAM,
machine/audio/input, or game/mod state mismatch and returns a nonzero status.
The last recorded input of an interrupted frame still executes, allowing the
original hang/crash to recur. A missing result/end marker is never success.

The bounded SDL worker is adapted from snesrecomp's
`runner/src/diagnostics/replay_writer`. Native stacks stay process-local:
snapshot save/load events refer to checkpoints recreated in execution order
during playback. The common tick observes rollback executions rather than
deduplicating frame numbers. Netplay qualification is separate from offline
qualification: unrecorded host mutations can produce explicit divergence.
TCP/script writes to arbitrary RAM are not recorded as editing commands;
use input-only routes for replay QA.

Regression tests: `diagnostic_replay_test`,
`tests/runtime/test_diagnostic_replay.py`, and `video_mod_test`. Reports and
machine checkpoints are forbidden release assets.
