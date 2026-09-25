# Genesis 4-player netplay — handoff (2026-09-25)

Goal: up to 4 human players in the Sonic 2 party mod over netplay, at effective
parity with snesrecomp's recomp-net integration (rollback by default, N seats,
relay / LAN hub / ICE, spectators, soft return to the lobby, host SRAM sync,
mod/config gate, full recomp-ui lobby UX). recomp-net stays console-agnostic.

Status: **paused mid-Phase 3.** Nothing here is shippable yet; the existing
2-player delay-sync netplay (off by default, `-DGENESISRECOMP_NETPLAY=ON`) is
unchanged.

## Decisions already taken

- The full lobby client is lifted out of snesrecomp into recomp-net as a shared,
  console-agnostic `recomp_net_lobby` target. Genesis consumes it. snes, psx and
  nes migrate to it later, in their own changes.
- Lobby server: `ws://netplay.retcomm.net:8765` (the same server snesrecomp and
  psxrecomp use).
- Rollback must work at **any** tick: title, menus, loading and lag frames. The
  boundary/resume-PC approach psxrecomp uses doesn't cover those, so it is
  rejected. The approach here is an in-process snapshot of the suspended game
  coroutine (engine-owned stack, minicoro).
- WebSocket would-block: stay connected and keep a frame-atomic outbound buffer.
  Disconnect only on a hard error or when the backlog passes a cap. That buffer
  is not implemented yet (see the recomp-net PR). The same bug in snesrecomp is
  filed as RetroPortingToolKit/snesrecomp#104.

## Capability matrix

Compared: snesrecomp origin/main 284afca and recomp-net origin/main fa1e350,
against Genesis master c5d40a6.

| Capability | snesrecomp | Genesis today | Planned change |
|---|---|---|---|
| recomp-net pin | c2338c6 | c72196e: no `rollback.h`, 4 slots | fast-forward, then the N-peer work |
| Seats | lib 8; lobby 4P + 4 spectators; refuses seats beyond the player count | hard-coded 2 (`runner/netplay/genesis_netplay.c` slot_count / loops) | N-slot facade capped by `GameSpec.logical_players` |
| N>2 rollback hash agreement | 1-of-N (single peer digest; senders not attributed) | — | **done in recomp-net PR**: per-sender takes, N-way watermark |
| Pad blob | 4 B: 12-bit buttons + 2 host sync bytes, zero = neutral | 12-bit mask × 2 | 4 B per seat, 6-button active-high + sync bytes |
| Rollback | default on; snapshot ring; 7-partition digest | none | Phase 3 (this PR) + Phase 4 |
| Topology | relay star (3+ online), ICE for 2P | ICE / LAN 2P | relay + ICE + LAN hub (psxrecomp model) |
| Spectators | yes (relay wire slot ≥ 64) | none | yes |
| Disconnect | soft return to lobby, resume room, rematch | `running = 0`, process exits | soft-return loop |
| SRAM / saves | host-authoritative push; guest sandbox | each peer loads its own `.srm` | same as snes; autosave only at or below the confirmed tick |
| Mod / config gate | modset handshake, mod plan | Sonic 2 `commit_netplay` refuses save menu | modset + GameSpec config image |
| Lobby client | 4.6k-line full client | NES-derived 1.3k-line client, other server | shared recomp-net client (**done in recomp-net PR**) |
| recomp-ui callbacks | nearly all ~100 | 29; `np_create` lacks `max_slots`; guest bind `:0` bug | generic adapter in recomp-ui |
| recomp-ui Genesis seats | — | profile max_players 2 | **done in recomp-ui PR**: 4 |
| Determinism locks | wall-clock audio catch-up off; rewind / run-ahead refused | view-mode toggle not gated; window-size-dependent sim paths | session-pinned config; refuse scripts / TCP / quickstates online |
| Diagnostics / tests | JSONL diag, link sim, RB probe, loopback soak | none | `GENESIS_NET_DIAG`, `GENESIS_RB_PROBE`, 2- and 4-process soak |

## Phase status

**Phase 0 (sync and tracking): done.**

**Phase 1 (recomp-net): done except the would-block buffer; see the recomp-net
PR.**
- N-peer rollback agreement, with no wire change.
- Shared lobby client, 127 public functions. It covers all 86 lobby functions
  snesrecomp calls.
- 26/26 ctest on MSVC. gcc and clang (WSL) pass for the rollback part.

**Phase 2 (recomp-ui): partial; see the recomp-ui PR.**
- Genesis profile `max_players` goes from 2 to 4, pinned by
  `genesis_seats_test` (mutation-checked).
- Remaining: `src/netplay/recomp_lobby_adapter.{h,c}`, a generic implementation
  of `RecompLauncherCNetplayCallbacks` on top of `recomp_net/lobby_client.h`
  (port `snes_host_lobby.c`).

**Phase 3 (Genesis rollback core, this PR): steps 1–2 green, step 3 red, steps
4–9 not started.** Every step must keep all 7 targets fingerprint-identical:
S1, S2, S3-alone, S&K, S3K, RKA and Puyo.

| # | Step | State |
|---|---|---|
| 1 | Generator emits `recomp_tail_frame_get/set/walk` (m68k-recomp-core `profiles/genesis/code_generator.c`). `g_recomp_tail_frame` points into the game fiber stack and was the only generated mutable static. | **Green** |
| 2 | `runner/fiber_compat.{h,c}` on vendored minicoro (Unlicense/MIT-0, `runner/external/minicoro`). Engine-owned 32 MB stack with a guard page; the Windows TIB is kept consistent for `__chkstk`. New `fiber_reset`, `fiber_snapshot_bound/save/load`, `fiber_stack_range`. Startup check that shadow stacks are off, plus `/CETCOMPAT:NO`. `glue_restart_game_fiber` resets in place. The yield site is recorded at all 9 game→main switches. | **Green (corrected 2026-09-25).** The original claim here -- "passes on MSVC, mingw gcc, clang, and WSL gcc/clang/ASan" -- was **refuted** on Linux gcc 16 / clang 22: the test failed in Release and RelWithDebInfo. Two causes: (1) test UB, `acc * 6364136223846793005LL` is signed overflow and gcc -O3 used it to collapse the reference recursion ("max depth 2"), fixed with unsigned arithmetic; (2) a real hazard, the 8 KB frames of the overflow child stepped over the single 4 KB guard page and faulted *outside* it (exit 43), i.e. into memory below the guard -- where the coroutine header lives. Fixed: `FIBER_GUARD_BYTES` = 64 KB guard, and gcc/clang runner targets build with `-fstack-clash-protection`, asserted by a new `--overflow-huge` case (a 128 KB frame must fault in the guard; negative control without the flag fails as expected). Now green on gcc and clang at -O0/-O2/-O2 -g/-O3 and in the gate's Release (gcc) and RelWithDebInfo (clang) ctest runs; aarch64 still compile-only. |
| 3 | Side-effect-free host memory access (`runner/include/genesis_host_mem.h`: `glue_peek/poke*`, `gbus_peek*`, `gvdp_peek_*`). All host-side reads and writes are converted: main.c, glue.c, host_state.h, cmd_server.c, S1/S3 video. The Sonic-1 RAM literals in main.c now come from `g_game_layout`. | **Green (2026-09-25)** after `glue_sched_frame_begin()` (see below) |
| 4 | `runner/sim_step.{c,h}`: `genesis_sim_step(const GenesisSimInput*, const GenesisSimOutput*)` extracted from the inline frame in main.c; `genesis_sim_pad(p)` / `genesis_sim_human_mask()`; status-only VDP render mode. | Not started |
| 5 | `runner/rb_state.c`: one section table driving both snapshot and digest (exec, cpu, sched, ram, machine, fm, psg, compact evq, GameSpec hook), plus a byte-budgeted snapshot ring. | Not started |
| 6 | Partitioned digest reusing `cosim_state_hash`; add the missing YM-timer / VDP-stall / scheduler fields; `static_assert(sizeof)` drift guards; mutation test. | Not started |
| 7 | During resim, audio drains into scratch; the device stream is never rewound. | Not started |
| 8 | `GENESIS_RB_PROBE`: live vs resim and load vs uninterrupted, at every tick; `_STATICS` and `_STACKSCAN` carrier finders. | Not started |
| 9 | Game repos migrate to `cmake/GenesisRecompRunner.cmake`. It already fills in missing runner sources for unmigrated consumers, so no game-repo edit is needed to build. | Partial |

**Step 3 red -- RESOLVED 2026-09-25 (root cause measured).** The carrier is
the same-address **spin streak** (`s_spin_addr`/`s_spin_count` in glue.c's
`spin_check`), not the Z80 poll streak and not the 256-poll fallback. The
baseline's per-frame host reads `m68k_read32(0xFFFE0C)` (before and after
`machine_run_frame`) went through the emulated bus and so restarted the spin
streak every frame; the peeks do not. Measured, Linux gcc 16, 18000-frame
attract: spin yields S3K 22 -> 33, S&K 22 -> 39 (S2 0 -> 0); Z80 poll yields
identical (604 / 419 / 22) and `g_z80poll_fallback_hits` = 0 in base and
candidate alike. Resetting only the Z80 poll streak at the frame boundary
leaves S3K/S&K red; resetting the spin streak (before the frame, after it, or
both) restores all fingerprints. The reset is now an explicit, documented
scheduler rule (`glue_sched_frame_begin()`, called right before
`machine_run_frame`), and the streak variables are part of the rollback
scheduler section. Gate after the fix: S1, S2, S3, S3K, S&K identical in all
three scenarios; RKA and Puyo are not available in this workspace (no repo, no
ROM) and were **not** gated. The text below is the investigation as it stood.

**Step 3 red, the open investigation (historical).**
- S1, S2, S3 and RKA stay identical. Puyo, S3K and S&K differ in all three
  scenarios.
- S3K and S&K have identical framebuffers but different state and audio, so the
  change is in the sound path. Puyo's framebuffer diverges at frame 380 in
  attract and frame 285 in gameplay.
- Hypothesis: a host read in the baseline went through `m68k_read*`, which
  advances `spin_check` state, the Z80-poll streak, or the cycle budget. The
  baseline schedule therefore depends on host observation, and the peeks
  removed that coupling.
- Suspects, in order:
  1. The floor capsule's expected-return read (`m68k_read32` on the game fiber).
  2. The per-frame `audio_obs` / `g_snd_vint` reads.
  3. The PLC and widescreen paths.
- Method: revert one conversion at a time, rebuild S3-family and Puyo, compare.
  Accept new fingerprints only once it is proven which baseline behaviour
  depended on the host read (PRINCIPLES #23).
- Disproved:
  - `--hash-on-mode`'s per-frame read; the baseline is identical with and
    without it.
  - The `glue_yield_for_vblank` yield-log reads; the baseline is identical with
    the log file open or blocked.

**Phase 4 (Genesis netplay facade parity): not started.**
- Bump `external/recomp-net`.
- N-slot `genesis_netplay` mirroring `snes_netplay`.
- `genesis_netplay_rb.c` (port of `snes_netplay_rb.c`).
- `genesis_host_app/session` (barrier admit, starvation latch, soft return,
  rematch).
- Replace `runner/lobby` and `genesis_launcher_netplay.c` with the shared client
  and the recomp-ui adapter.
- Session config agreement.
- Online refusal of scripts, TCP, quickstates and similar.

**Phase 5 (Sonic 2 game repo): not started.**
- P3/P4 input from `genesis_sim_pad` instead of live SDL.
- Human/CPU companions from the session `occupied_mask`.
- A GameSpec `rb_state_*` hook covering the `inside_*` flags and hoisted
  reentrancy statics, excluding the engine-owned event queue.
- A config image carrying roster, characters, donor hashes and custom-video
  width.
- Widen `s2_options_netplay_allowed`.
- Enable netplay in release builds.

## Regression harness

`tools/netplay_regression.py` builds all 7 targets against an engine checkout,
runs headless workloads, and compares fingerprints:
- `GENESISRECOMP_BENCHMARK` state and audio FNV
- FBHASH on every frame, and MODEHASH
- pre-resample mixer sha256
- `dispatch_misses`

```
python tools/netplay_regression.py all --engine <baseline engine> --out <dir> --json base.json
python tools/netplay_regression.py all --engine <candidate engine> --out <dir2> --json cand.json
python tools/netplay_regression.py compare base.json cand.json
```

- **Environment:** the game repos sit side by side under `GENESIS_WORKSPACE`
  (default `F:/Projects/segagenesisrecomp`). `GENESIS_CMAKE` and
  `GENESIS_CMAKE_GENERATOR` override the VS2022 BuildTools default. Runs are
  hermetic: `GENESIS_*` and `RNET_*` are stripped, and every run gets a fresh
  run directory.
- **Baseline:** deterministic run to run for all 7 targets across the three
  scenarios (attract 18000 frames, save/load, scripted gameplay). A negative
  control (`GENESIS_INTERLEAVE_IRQ=1`) is detected.
- **Puyo:** its CMake has no engine-root override and omits `cosim_state.c`. The
  harness configures it from a scratch copy with an `engine-local` link and adds
  that file from the build configuration, identically for baseline and
  candidate.
- **ROMs:** they are not in any repo. Place them where each game's CMake expects
  them. Sonic 2 expects `game/sonic2.bin`.

## Risks and unknowns

- **Resim cost:** one tick is 262 scanlines plus about 887 ymfm samples. The
  milliseconds per resim tick are unmeasured, and the prediction window has to
  fit in the frame budget.
- **Fiber snapshot on arm64** (Android, macOS) has not been run on hardware.
- **Heap references on the fiber stack:** the audit found none that can go stale
  during a session. `_STACKSCAN` (step 8) is meant to enforce this mechanically.
- **Unsnapshotted state:** Sonic 2's reentrancy statics and `inside_*` flags are
  not serialized today. Quickstates only save at game-approved boundaries, which
  hides this.
- **Latent quickstate bug:** the YM timer clock is `g_snd_frame`
  (`genesis_bus.c`), a trace counter that no save state contains.
- **Window-size-dependent simulation:** Sonic 2 custom video and engine adaptive
  widescreen both change the simulation with the window size. Both must be
  pinned per session; this already affects today's 2P delay-sync netplay.
- **Cross-compiler undefined behaviour** in the generated C (MSVC vs gcc/clang
  peers) has not been checked.
- **snesrecomp rollback is 2-peer by design** (`set_rb_peer_slot` pins one seat).
  The recomp-net N-peer APIs need host-side migration there; the steps are in
  recomp-net `docs/rollback.md`.
- **Separate findings, tracked outside this work:**
  - The PLC gate masks `plc_pending_addr & 0xFFFF`, so on Sonic 1 it reads ROM
    and RunPLC runs every vblank. Kept as-is here to preserve hashes.
  - The yield log does per-frame file I/O inside the simulation.

## Game-repo edits still needed (not made)

- Sonic 2 `game/sonic2_video.c` and `game/sonic2_save_menu.c`: host writes use
  `m68k_write*` and should move to `glue_poke*`.
- The Sonic 2 Android `android/app/jni/src/CMakeLists.txt` lists runner sources
  by hand. It needs `sim_step.c`, and `libucontext` is no longer needed by
  `fiber_compat`.
