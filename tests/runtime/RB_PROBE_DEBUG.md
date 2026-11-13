# Rollback probe debugger boundaries

Use an owned compatible ROM and an isolated writable profile. Keep audio
muted. The normal runner's `debug.ini` enables loopback TCP; `--port` chooses
its port. `tools/tcp_cmd.py` supplies the existing JSON client.

For the Sonic 3 & Knuckles cold-route control, arm `GENESIS_RB_PROBE=300:4`,
run `sonic3k_rb_probe_cold.input` with `--max-frames 1200`, and retain the
native runtime log. An uninterrupted run must report three exact windows,
zero divergence/asymmetry and no discarded window.

Repeat the same run. After `ping` reports a frame in 289..295, send 24
consecutive `{"cmd":"run_frames","count":1}` requests on one TCP connection.
These requests span the recording window rather than relying on one request
arriving within four frames. Each completed debugger tick must participate
in the probe. The final log must still report zero divergence/asymmetry and
no discarded window. Without debugger-tick accounting this control can
compare live guest advancement that the replay never recorded.

For external-load handling, complete `rb_probe_prepare_current.input` with
`--max-frames 240` and verify that the save finished successfully. Start a
new process of the same build, arm `GENESIS_RB_PROBE=30:4`, then run
`rb_probe_load_current.input` with `--max-frames 240`. Require an actual
successful load. The frame-30 partial window must be explicitly discarded,
not counted as a passed replay; all six subsequent complete windows must
match. Replacing the path with a nonexistent file must reject the load,
retain all seven complete windows and report zero discarded windows.

Snapshots are created by the build being tested. These checks do not claim
older-state compatibility, native cartridge-save progress or game-mechanics
coverage. They do not change digest fields or replay acceptance thresholds.
