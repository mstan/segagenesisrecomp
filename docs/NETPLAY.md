# Genesis netplay (rollback, 1-4 players)

Status 2026-09-25, branch `feat/netplay-4p-rollback`: **rollback netplay runs
end to end on Linux** -- Sonic 2 over recomp-net's shared episode driver,
2 to 4 seats plus spectators, LAN / LAN hub / the lobby server's UDP relay,
the recomp-ui lobby (create, join, launch, soft return, rematch, offline Play
after the room), host-authoritative SRAM, a session config seal, and a
netplay-off build that is fingerprint-identical to engine `c5d40a6`.

What that does NOT mean (recomp-ai-rules/NETPLAY.md section 8): every result
below is **two to five processes on one machine over loopback**, through the
link simulator. Nobody has played it with a controller. No two-machine or
internet run. ICE is compiled in but has not run (no STUN/TURN here). The
recomp-ui launcher's netplay screens were driven through the same callback
table headlessly, never looked at on a display. That pass did not build Windows/MSVC.

Windows packaging follow-up (2026-09-25): Sonic 2 builds with MSVC 19.41,
rollback/ICE enabled and all developer tracing disabled. The two cost timers
use SDL's portable performance counter, and the stripped command-server stub
exports the online callback required by the runner. A hidden 300-frame startup
check passes with zero dispatch misses. This is a build/startup check only;
multiplayer playtesting remains with the owner and another player.

Libraries: recomp-net `588059c` (RetroPortingToolKit
`feat/genesis-spectator-ready`, on `feat/nes-spectator` bdc58b6 = PR #17, on
`03ee1b1` = main `a9d20e2` + sparse-room `occupied_mask` + the frame-atomic
WebSocket buffer), rbengine `2a03e73`, recomp-ui `b9ef2f5` (RetroPortingToolKit
`feat/nes-lan-rematch` = PR #64, on `feat/genesis-netplay-4p` 9edc243: master
`b688ca7` + Genesis `max_players` 4 + the Direct IP START fix).

## How it fits together

```
main.c loop ──► genesis_netplay_poll_admit ──► rnet_rb_driver_poll_admit (INLINE replays run here,
    │                                               │         each through genesis_sim_step with
    │                                               │         no sink + scratch audio)
    ▼                                               ▼
genesis_sim_step(sealed GenesisSimInput)      RNetRbHost (runner/netplay/genesis_netplay_rb.c)
    │  one tick: pads -> pre-raster -> frame-      snap_*  : runner/rb_state.c blobs in rbengine's ring
    │  boundary rule -> 262 lines -> audio drain   digest  : rb_state partitioned digest
    │  -> V-blank bookkeeping                      pads    : 4 B/seat, GPAD_* 12-bit active high, neutral 0
    ▼
genesis_netplay_finish_frame
```

- **One tick** (`runner/sim_step.{c,h}`): `genesis_sim_step(in, audio, hooks)`
  is the same function for the offline loop, the live netplay tick, a
  rollback replay and the determinism probe. Adapters read players through
  `genesis_sim_pad(p)` / `genesis_sim_human_mask()` -- never SDL.
- **Rollback state** (`runner/rb_state.{c,h}`): one section table (exec =
  suspended game fiber + generated tail-frame head, cpu, sched, ram, machine,
  fm, psg, evq, input, game) drives both the snapshot and the 12-partition
  digest; each section's digest is the FNV-1a of exactly its snapshot bytes
  (exec: address-free view; raw stack compared in-process by the probe).
  `static_assert`s pin every raw image; `genesis_rb_selftest()` is the
  mutation test.
- **Determinism probe** (`runner/rb_probe.c`, `GENESIS_RB_PROBE=period:depth`,
  `tools/rb_probe.sh`): save, let the live run go N ticks, restore, replay
  the sealed inputs, compare every tick + restore symmetry.
  `GENESIS_RB_PROBE_STATICS` + `tools/rb_statics_resolve.py` +
  `tools/rb_statics_allow.txt` is the carrier finder (each allowed symbol
  carries its reason); `GENESIS_RB_PROBE_STACKSCAN` looks for heap
  references on the fiber stack; `GENESIS_RB_NEGCTL_SKIP=<section>` is the
  negative control.
- **Facade** (`runner/netplay/genesis_netplay.{c,h}`): seats 1-4 mapped to
  logical players by `slot_port[]` (recomp-ui HOST_FIRST), occupied mask,
  spectators, transports lan / hub / relay / ice, SRAM barrier, config seal,
  identity. **Replay shape: INLINE** -- a replayed tick costs p50 0.7 / p99
  1.0 ms and the driver's prediction cap bounds a replay, so it fits one host
  iteration; INCREMENTAL would hold presentation for as many frames.
- **Lobby** (`runner/netplay/genesis_host_lobby.c`): a thin adapter over
  recomp-ui's `recomp_netplay_host` (replaces the NES-derived `runner/lobby`
  client and `genesis_launcher_netplay.c`), plus the headless room selftest.
- **Sessions** (`main.c`): after a match (peer gone, refusal, coordinated
  stop) the process soft-returns: the launcher reopens on the waiting room
  (`run_recomp_launcher(..., soft_return=1)`) or the headless room readies
  again; the next session -- rematch or offline Play -- starts from
  `genesis_session_reset()`: the pre-first-tick rb_state snapshot restored and
  the battery save re-read from disk, i.e. a cold power-on in the process.

### Knobs

| Env | What |
|---|---|
| `GENESIS_NETPLAY=1`, `GENESIS_NET_SLOT`, `GENESIS_NET_SLOTS`, `GENESIS_NET_BIND`, `GENESIS_NET_PEER`, `GENESIS_NET_TRANSPORT=lan\|hub\|relay\|ice` | a session without the lobby (harnesses) |
| `GENESIS_NET_DELAY`, `GENESIS_NET_PREDICTION`, `GENESIS_NET_MODE=rollback\|delay` | D, P, mode (rollback is the default) |
| `GENESIS_NET_OCCUPIED`, `GENESIS_NET_SLOT_PORTS=a,b,c,d`, `GENESIS_NET_SPECTATOR[_WIRE_SLOT]` | sparse rooms, seat->player, gallery |
| `GENESIS_NET_PAD_TYPES=a,b` | port pad types (session config) |
| `GENESIS_RB_*` | the driver's knobs (`FORCE_MISPREDICT`, `FORCE_BOOT_FORK`, `SNAP_DEPTH`, `TIP_RUNWAY`, ...) |
| `GENESIS_NET_TEST_PAD=period:hold:mask` | scripted local pad (organic mispredicts) |
| `GENESIS_NET_TIMELINE_EVERY=n` | `TIMELINE t= d=` for confirmed ticks |
| `GENESIS_LOBBY_SELFTEST=host\|guest` (+ `_PLAYERS`, `_SPECTATORS`, `_ROUNDS`, `_FRAMES`, `_THEN_OFFLINE`, `_LAN`) | the headless room |
| `GENESIS_SCREENSHOT_AT_EXIT=png`, `GENESIS_SCREENSHOT_AT_TICK=T` | engine framebuffer PNGs |

Harnesses: `tools/rb_loopback.sh` (2-4 processes, exact ledger by pair and
epoch, coordinated SIGUSR1 drain, dispatch-miss check), `tools/rb_sweep.sh`
(14 cells + pre-flight), `tools/rb_lobby.sh` (N players, spectators, rounds,
forced refusals, offline-after-room vs a fresh process),
`tools/rb_probe.sh`, `tools/netplay_regression.py` (the netplay-off gate;
Linux and Windows).

## Capability matrix

SNES column as in the n64lle import artifact. "measured" = a run exercised
the path and counted it above zero. Sonic 2 unless a row says otherwise.

| Capability | SNES | Genesis | Source | Evidence |
|---|---|---|---|---|
| **Libraries** |  |  |  |  |
| admission scheduler + gates | bound | **measured** | recomp-net | bound through the driver; every sweep cell runs it |
| input history / hash-confirm / RB_POST | present | **measured** | recomp-net | neutral row 0 per seat (active high); FORCE_MISPREDICT flips START\|B (0x0090, bits every scene reads): `replays_changed` = replays in every cell (e.g. 92/92 per peer at 200 ms) |
| link simulator | present | **measured** | recomp-net | sweep cells 60/200/300 ms, 2%/5% loss |
| snapshot ring | present | **measured** | rbengine | rb_state blobs, 159-179 KB; ring path save p50 0.039 / p99 0.12-0.15 ms, load 0.015 / 0.036-0.048 ms (NETPLAY_FIELDS) |
| lobby client + launcher callbacks | lifted | **bound + measured** | recomp-net + recomp-ui | `genesis_host_lobby.c` over `recomp_netplay_host`; identity `game_version` = `0.7.0+<fnv32 of exe>`, `content_fingerprint` = ROM SHA-256 |
| **Episode driver (recomp-net; Genesis binds it)** |  |  |  |  |
| episode FSM | present | **measured** | recomp-net | sweep 14/14 PASS, 0 forks, residual 0 |
| tip-hold / tip-extend | ported | **measured** | recomp-net | mean hold 3.3-9.0 ticks per cell; 62 tip-extends in STRESS |
| stage watchdogs + cooldowns | ported | **measured** | recomp-net | 1 watchdog (5% loss cell; disconnect cell), 1 (4 seats 2% loss); ledger exact |
| baseline_fork_cap | ported | **measured** | recomp-net | `GENESIS_RB_FORCE_FORK=4` (synthetic baseline fork, local = peer digest), D=8, 30 ms: cap set 10x; with `LOCKSTEP_TICKS=0` + ring 120 the cap CLAMPED later loads (mismatch 75 -> load 59, 150 -> 134; replayed 32 ticks) and was lifted on commit; ledger 76/76, confirmed timelines identical on both peers |
| lockstep_no_invent | ported | **measured** | recomp-net | same run, default 60-tick lockstep: entered 10x, released 9x, 9 injected mispredicts cancelled ("lockstep forbids invent"); ledger 40/40, timelines identical |
| boot-digest gate | built | **measured** | recomp-net | FORCE_BOOT_FORK via the online room: both peers refuse `boot_digest_mismatch` at sim 1, soft-return, room shows `last_error` |
| rematch cold reset | start only | **measured** | engine + driver | online 4 players + 1 spectator, sessions 2 -> 3, one boot digest across 10 match starts, 0 forks (recomp-net 588059c, recomp-ui b9ef2f5); LAN rematch twice: sessions 280921172 -> 280921173 and 1028161760 -> 1028161761, fresh per START, both PASS; offline Play after the room == a fresh process (`RUN_DONE state=96d43a0dff200343` on all 4 peers, earlier pin) |
| FRAME_COMMIT chain | advisory | advisory | recomp-net | 0 chain stalls in 13 of 14 cells; 4 in STRESS |
| N seats | 2 only | **measured** | recomp-net + engine | 4 HUMAN players (roster Sonic / Tails / Knuckles / Sonic, every seat human, seat 0 and seat 3 scripted controllers, seat 1 injector): 0 ms 221 ep ledger 663/663; 200 ms 122 ep 366/366; 2% loss 190 ep 570/570 (3 watchdogs); 0 forks, all drained, tick-aligned screenshots of the confirmed state byte-identical on all 4 peers at t=1100/1150/1200 in every cell. 3 seats (Sonic / Tails / Knuckles, all human): 0 ms 151 ep 302/302, 200 ms 100 ep 200/200, 2% loss 149 ep 298/298, screenshots identical at t=1100/1200. Probe with the 4-player duplicate roster: 691 passes x 12, 0 divergences |
| replay ownership | n/a | **inline** (justified) | engine | replayed tick p50 0.7-0.85 / p99 0.96-1.65 ms |
| **Engine** |  |  |  |  |
| rollback snapshot fast path | n/a | **measured** | rb_state | raw save p50 0.011-0.016 / p99 0.029-0.048 ms, load 0.009-0.013 / 0.027-0.046 ms (probe, 5 games) |
| partitioned per-tick digest | present | **measured** | rb_state | 12 partitions (wire: ram / sound / vdp); p50 0.15 / p99 0.17 ms; digesting every tick leaves the guest identical (sweep pre-flight) |
| resync after restore | ported | **measured** | rb_state + game hook | probe: S1, S2, S3, S3K, S&K and the S2 party, 691 passes x 12 ticks each, 0 divergences, 0 asymmetric; `_STATICS` 0 carrier candidates on all five |
| determinism probe | present | **measured** | rb_probe | carriers found and fixed: `g_split_sp_popped` (generated), VDP status flags from rendering, the unlimited-sprites switch, 6 S2 function-local guards; negative controls detected for every section |
| per-tick host loop, netplay-off identical | n/a | **measured** | sim_step | gate: S1, S2, S3, S3K, S&K identical to `c5d40a6` in attract / savestate / gameplay (S2 with netplay compiled in); RKA and Puyo not available here |
| config seal | n/a | **measured** | engine + S2 | image = pads, widescreen margin, custom-video mode, S2 roster + owner features, sim-affecting dev knobs; host's adopted (online guest without widescreen adopted the host's 16:9, 0 forks); a different image (16:9 vs 21:9): MOD SETS DIFFER, 0 episodes; scripts that write RAM / load state, TCP execution control, quickstates, turbo and the view-mode toggle are refused online; an adaptive (window-size) custom-video width is pinned to the host's width as `W:224` |
| host-authoritative SRAM + guest sandbox | ported | **built** | engine | host ships SRAM before tick 0 (`RNET_STATE_OP_SRAM`); guests never write; host writes once per match. Not exercised: Sonic 2 has no battery SRAM |
| **Product** |  |  |  |  |
| recomp-ui lobby | present | **measured (headless)** | engine + recomp-ui | online create/join/launch 2 and 4 players; LAN 2 players; soft return with `last_error`; rematch; launcher screens not seen on a display |
| spectators | yes | **measured** | recomp-net relay | 4 players + 1 spectator online, 2 rounds (sessions 2 -> 3), twice: the spectator predicts nothing and opens/follows no episode (bdc58b6 observer path: episodes=0 invents=0 in both matches), keeps level (sim 1207-1211 vs 1200 drained), and its confirmed TIMELINE equals every player's (identical files); one boot digest across all 10 match starts. Found on the way and fixed in recomp-net `588059c`: a spectator re-armed `set_ready` on every `lobby_update`, the server (which keeps ready in the player table only) answered each with another update -- an unbounded storm behind which the rematch's `op:launch` was never read (round 2: no peer launched, 6 of 7 runs); `lobby_client_test` case added (fails on bdc58b6) |
| two-process harness + sweep | in tree | **measured** | tools | `rb_sweep.sh` 14/14 PASS on isolated ports with recomp-net 588059c + recomp-ui b9ef2f5 (pre-flight probe 99 passes + digest transparency; residual 0 in every cell); `rb_lobby.sh` PASS in every mode above |
| screenshots | -- | **measured, tick-aligned** | engine PNG + rb_loopback | `GENESIS_SCREENSHOT_AT_TICK` writes each peer's frame at sealed tick T with the digest of the state shown; the harness grades PNG identity only where every peer's live digest equals the CONFIRMED TIMELINE digest (a frame that showed a later-corrected prediction is reported, not graded). End-of-run captures are now `*.exit.png` and labelled NOT tick-aligned. **Correction:** an earlier version of this row compared the `runs/nseat/s4_200ms_shot` tick-1200 set but also let the end-of-run PNGs of `runs/nseat/s4_200ms` stand as if comparable; they are not (peers stop at different ticks while draining), and in those frames P1 Sonic IS visible (jumping, top-left of Knuckles). "Not visible" was wrong for them; in idle runs P1 and P4 Sonic are hidden BEHIND Knuckles because extra actors spawn at P1's position and the party overlay draws on top. Seat 4 control: P4 (second Sonic) visibly steps out right of Knuckles when only seat 3 presses RIGHT (`runs/seat4/move` vs `control`, t=1100, 4 peers each identical) |

## Findings (2026-09-25)

**A. Step 3 fingerprint regression -- the spin streak, not the Z80 poll.**
The baseline's per-frame host reads `m68k_read32(0xFFFE0C)` restarted
`spin_check`'s same-address streak every frame; side-effect-free peeks let it
carry. Spin yields over the 18000-frame attract run: S3K 22 -> 33, S&K 22 ->
39, S2 0 -> 0; Z80 poll yields identical (604 / 419 / 22). Resetting the Z80
poll streak alone leaves S3K/S&K red; resetting the spin streak restores
them. The reset is now an explicit scheduler rule, `glue_sched_frame_begin()`
(before `machine_run_frame`), and the four streak variables are in the
rollback sched section. The audit's Z80-streak hypothesis is refuted.

**The 256-poll fallback (filed separately).** `m68k_read8`'s Z80 poll path
returns a synthesized `0x00` after 256 unanswered polls. Measured: 0 hits in
S2, S3K and S&K (18000-frame attract, baseline and candidate). It is still a
fake-the-answer path (PRINCIPLES.md, No Stubs) that would fire silently --
counted only in `g_z80poll_fallback_hits` under GEN_DEV_TRACE. Open: make it
loud (log + miss file) or delete it; not changed here because nothing
measured reaches it.

**B. Fiber test.** Signed-overflow UB in the test; a real hazard (8 KB frames
skipped the 4 KB guard page). 64 KB guard + `-fstack-clash-protection` +
an `--overflow-huge` case with a negative control. Green on gcc 16 / clang 22.

**C.** Sonic 2 master did not build on gcc (`0xCE0E+be16` lexes as one
pp-number). Split; audit found no other instance.

**D.** `tools/netplay_regression.py` runs on Linux (Ninja, symlinks, exe
paths) and reports unavailable repos instead of failing or dropping them.

**Carriers the probe found** (each MISSING from the snapshot, none WRONG):
`g_split_sp_popped` -- a mutable generated global, refuting "the only
generated mutable static" (step 1 row below); replays skipped rendering but
sprite evaluation sets the VDP status flags; `gvdp_set_unlimited_sprites`
(game-set, decides the overflow flag); the S2 adapter's function-local
reentrancy guards (hoisted). Also fixed: `g_snd_frame`, the YM timer-B clock,
was in no savestate while the timer deadlines in the bus are absolute in it.

## Open defects and not exercised

- Nobody has played it; no two-machine or internet run; ICE not run.
- The launcher's netplay screens (soft return, rematch) not seen on a display.
- Without the Amy donor ROM there are three distinct characters, so players 3
  and 4 may now repeat a character (the party roster rule changed: P1/P2 stay
  distinct; P3/P4 are independent actors). A human in seat 4 plays e.g. a
  second Sonic; both Sonics look the same.
- Custom video online: carried as session config (above). Enabling it made
  the probe fork in the game partition (74 of 460 passes) because the
  scanline renderer's scene selection was in the rollback section; the
  presentation half is now excluded (0 divergences in 460 passes with 16:9).
  **Corrected 2026-09-25:** the GPU max-texture clamp in `custom_video_prepare`
  used to reach the simulation -- the Sonic 2 scanline renderer copied the
  presented (clamped) width into `s_requested_width`, the width that drives
  object activation -- so a peer with a smaller texture limit started from a
  different state (forced 360-px limit on one peer: BOOT DIGEST MISMATCH at
  tick 0). Now the simulation width comes only from the sealed mode
  (`width()` runs even without a renderer), the clamp and allocation failures
  change presentation only (online a failed allocation presents natively
  instead of switching the mode off), and the same forced case plays: 51
  episodes, ledger 51/51, 0 forks, confirmed digests equal on both peers at
  t=1100/1150 while the PNGs differ by design; the unforced peer's frames are
  byte-identical to a run without the knob. Knob:
  `GENESIS_FORCE_MAX_TEXTURE_W` (validation only).
- The campaign save menu stays local-only and refuses netplay.
- Harness port collision (found 2026-09-25): the NES/SNES/N64 harnesses on
  this machine use UDP 9700..9703 and session id 1, which Genesis shared; a
  concurrent NES soak cross-delivered datagrams and wedged Genesis runs
  (pcap_freeze, "peer gone"). rb_loopback now uses 9810..9813 and a random
  session id per run (the session drops foreign session ids), rb_lobby
  18965/18977/17890. Earlier Genesis results were re-run on the isolated
  ports where they are cited as current.
- Driver: "rewind N frames, replay 1" is NOT a bug. The replay covers the
  sealed span load..target (inclusive); the driver then sets sim =
  target + 1 and the discarded ticks after it are re-run as Live ticks
  (rnet_rb_driver.c: rb_log_raw "RESIM episode", and sim = target + 1 at the
  end of the replay). Measured: per peer, live ticks - sim ticks = sum over
  episodes of (rewind - replay) exactly (300 ms: 2194 - 1841 = 353 = 353;
  200 ms: 2465 - 2094 = 371 = 371). The "confirmed 180 vs 1828" was my own
  output truncation (`cut -c1-150` cut "confirmed=1800"); refuted, no defect.
- Host SRAM push is not exercised (Sonic 2 has no battery SRAM).
- Android runner list updated, not built. Windows/MSVC not built.
- RKA and Puyo: no repository and no ROM in this workspace -- not gated.
- Sonic 1 and Sonic3AndKnucklesRecomp built against this engine through
  `GENESIS_RECOMP_ROOT` for the gate; their own submodule pins are unchanged
  and they do not enable netplay.

---

## History: the mstan handoff (2026-09-25), as written, corrected at its claim sites


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
| 1 | Generator emits `recomp_tail_frame_get/set/walk` (m68k-recomp-core `profiles/genesis/code_generator.c`). `g_recomp_tail_frame` points into the game fiber stack and was the only generated mutable static. | **Green**; the "only generated mutable static" claim is **refuted** (2026-09-25): the genesis profile also emits `int g_split_sp_popped` (`<prefix>_part00.c`), and the `_STATICS` probe found it as the carrier of Sonic 3's replay forks (5 of 149 passes, cpu/sched/ram at step 1). It is now in the rollback scheduler section. An audit of the generated trees of S1, S2 and S3 finds no other file-scope or function-local mutable static. |
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
