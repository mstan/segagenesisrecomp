# Sonic 2 party: shared-engine integration gate

Validated on Windows/MSVC on 2026-09-19 before integrating the character spike.
Owner authorized engine integration only with opt-in/correctness scope and
compatibility with the other games. Megamix/six-player work was cancelled.

## Scope and defaults

- Character controllers, roster, mods, imported rendering and native hooks live
  under `sonicthehedgehog2/`; no other game's gameplay files changed in the spike.
- The new settings/mods/netplay/save-state callbacks are optional GameSpec fields.
  Every other game leaves them zero. The appended GameVideo overlay is NULL in
  other games, including the positional Sonic 1 and Sonic 3 initializers.
- Logical input storage grows to four; the emulated Genesis bus still has two
  ports. Existing P1/P2 bindings and defaults are unchanged.
- Controller enumeration is explicitly capability-gated. `gamepad_init()` and
  `gamepad_init_players(0)` retain two pads; only Sonic 2 advertises four.
  The SDL virtual-pad test verifies both defaults and the four-pad opt-in.
- Extra INI player sections are additive. Per-player scripted input is test
  tooling; timed presses now have independent deadlines rather than a shared
  four-timer pool. Normal gameplay does not activate scripts.
- No recompiler, CPU, VDP, audio, or native bus changes were needed for the spike.

## Evidence

Private artifacts are under the engine worktree's ignored
`build/party-integration/`; no ROMs, donor graphics or generated C are committed.

| Target | Build | 3,600-frame regression |
| --- | --- | --- |
| Sonic 1 | Unmodified consumer, engine override | Three PNGs and two full-RAM captures match master |
| Sonic 2 | Updated consumer, engine override | Same five captures match master |
| Sonic 3 | Unmodified consumer, engine override | Same five captures match master |
| Sonic & Knuckles | Unmodified consumer, engine override | Same five captures match master |
| Sonic 3 & Knuckles | Unmodified consumer, engine override | Same five captures match master |
| Rocket Knight Adventures | Unmodified consumer, engine override | Same five captures match freshly built master |
| Puyo Puyo | Isolated build-file correction, below | Same five captures match freshly built master |

All seven runtime comparisons have empty `dispatch_misses.toml` extra arrays.
Sonic baselines are the primary checkout's `build/annotation-smoke/` outputs;
Rocket Knight and Puyo baselines were rebuilt using pre-feature master runtime
sources. Candidate and baseline runs were isolated and serial.

Additional checks: all 18 CTests pass, including launcher/engine binding
persistence and virtual controller isolation; all 23 serial Sonic 2 live cases
pass with strict stack checks and zero dispatch misses. The diagnostic frame-60
boot snapshot matches the existing fresh-primary reference. No baseline changed.

### Existing Puyo build-file issue

The current Puyo consumer omits `runner/cosim_state.c` from its native target,
although pre-feature master `runner/main.c` already calls `cosim_state_hash`.
An unmodified build therefore fails to link that symbol. Validation used an
isolated copy of its CMake file adding the missing source equally to baseline
and candidate. The baseline uses the primary runner sources; generated Puyo
code, game spec and recompiler are unchanged between engine revisions. No Puyo
repository source or dependency pin was modified as part of this integration.

## Limits

These checks establish compatibility for the tested Windows build and stock
boot/attract paths, not zero risk. They do not constitute complete campaigns,
physical-controller matrix testing, network sessions, audio-reference comparison,
or Linux/macOS builds. Other consumers remain on their own pinned engine/UI
versions until explicitly updated. The four-player gameplay acceptance caveats
and separate human gate for campaign save/zone implementation remain in effect.
