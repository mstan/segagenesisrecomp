# Differential Co-Simulation & Divergence Tooling (Genesis)

A full-state, first-divergence decision procedure for the Genesis recomp +
runner, plus a holistic divergence *profiler*. Mirrors psxrecomp and
`recomp-template/DIFFERENTIAL-COSIMULATION.md`. Compiled **only** into the
dev-only `_cosim` / `_oracle_cosim` targets (`#ifdef GENESIS_COSIM`); the
shipping native build links none of it and is byte-for-byte unaffected.

The point of this doc is as much **what it does NOT do yet** (bottom) as what it
does. Read that section before trusting a number.

---

## Why two pairings (and what each can/can't see)

- **Pairing #1 — recompiler correctness.** `recomp` vs the in-project
  interpreter (`interp`), *both on the same clean-room runner*. This is the
  gold standard (psxrecomp's construction). It proves the recompiled 68K
  reproduces the interpreter. **It is BLIND to the runner** — VDP, Z80 sched,
  mixer, timing are shared, so a runner bug is identical on both sides.
- **Pairing #2 — runner correctness.** `recomp` (own backend) vs the
  `clownmdemu` oracle. This is the *only* instrument that sees the runner. It is
  inherently fuzzier (two independent implementations): raw equality is doomed
  for volatile state, so it hashes the guest-**visible** surface only.

## The clocks (alignment axes)

- **`frame`** — keyed on `g_machine.master_cycle` (VDP raster, advanced by the
  scanline loop, not CPU execution). Identical on both backends at each frame
  boundary → the **only** valid cross-backend (pairing #2) ruler.
- **`cycle`** (alias `insn`) — keyed on `g_cosim_cycle`, a monotonic
  per-instruction **cycle-cost** axis that recomp AND interp bump identically
  (via `GEN_COSIM_TICK`, cost table below). The psxrecomp guest-cycle counter.
  Valid for **pairing #1 only** (see gaps: clownmdemu isn't on this axis).

## Components

| file | role |
|---|---|
| `runner/cosim.{c,h}` | TCP lockstep server + checkpoint/park; `frame`/`cycle` clocks; `GENESIS_COSIM_START_FRAME` prologue skip; commands: `step/chain/sub/cpu/window/inject/memchunks/timingfields/vdpfields/...` |
| `runner/cosim_state.c` | full-state per-subsystem FNV hash (pairing #1): cpu68k, timing, ram, z80, z80ram, handshake, vdp, fm, psg, evq |
| `runner/cosim_visible.c` | guest-**visible**-surface hash (pairing #2, normalized both backends via `frame_snapshots.c`); `cosim_visible_region_chunks` (the localizer) |
| `runner/game_cycles.{c,h}` + `generated/<game>_cycles.c` | clown-measured per-address 68K cycle-cost table; interp charges the same costs under `GENESIS_COSIM` so both share the cycle axis |
| `runner/glue.c` | `FORCE_INTERP` (interp drives the whole program, yields at the WaitForVBla PC); **interleave-IRQ scaffold** (`GENESIS_INTERLEAVE_IRQ`, default OFF — see gaps) |
| `tools/genesis_cosim.py` | coordinator: gates, `--clock frame\|cycle`, `--profile` (holistic, no-halt), `--subs`, injection |
| `tools/divergence_report.py` | one-command scorecard (below) |
| audio instruments (env-gated, no-op default) | `GENESIS_AUDIO_WRITEDUMP` (push stream), `GENESIS_AUDIO_APPLIEDDUMP` (post-mixer applied stream), `GENESIS_AUDIO_PREDRC` (raw mixer render) + `tools/render_divergence_map.py`, `predrc_to_wav.py`, `_wt-accuracy/tools/synth_replay` |

## Run it

```bash
# from segagenesisrecomp/ (the engine worktree). Build first:
#   cmake --build build-cosim --config Release --target SonicTheHedgehogRecomp_cosim
#   cmake --build build-cosim --config Release --target SonicTheHedgehogRecomp_oracle_cosim

# ONE-COMMAND SCORECARD (gates -> pairing#1 cycle-clock -> pairing#2 frame-clock + chunk-level):
python tools/divergence_report.py --frames 600 --cycles 2000000

# validation gates only (trust nothing until these pass):
python tools/genesis_cosim.py --a recomp --b recomp --clock cycle --profile --max 300   # Gate 1 determinism
python tools/genesis_cosim.py --a interp --b interp --clock cycle --profile --max 300   # Gate 2
python tools/genesis_cosim.py --a recomp --b recomp --clock cycle --inject-at 100 --inject reg:0:1  # Gate 3

# holistic profiles:
python tools/genesis_cosim.py --a recomp --b interp  --clock cycle --profile --max 4000        # recompiler faithfulness
python tools/genesis_cosim.py --a recomp --b oracle  --clock frame --profile --max 600          # runner vs oracle
```

Ports default to **4720/4721** in `divergence_report.py` (psxrecomp cosim also
defaults to 4600 — collision guard). `genesis_cosim.py` still defaults 4600/4601;
pass `--porta/--portb` if a psxrecomp instance is around.

## What it currently establishes (Sonic 1 attract, 2026-07-01)

- **Recompiler: bit-exact** — pairing #1 on the cycle clock, all 10 subsystems,
  0 divergence over 2M cycles. The recompiled 68K is a faithful implementation.
- **Runner: ~90–97% faithful (chunk-level)** over the 10 s attract demo. Display
  (VRAM) stays faithful; divergence is sparse, timing-sensitive sound/gameplay
  scratch (z80ram ~97 %, wram ~91 %, vram ~97 %), non-cascading in that window.
- The apparent "diverges at frame 54 / vdp 78 %" was a **whole-region-hash
  artifact** (one byte flags the entire region). Use the chunk-level metric.

---

## What it does NOT do yet (READ THIS)

1. **No cross-backend CYCLE alignment vs clownmdemu.** The own backend
   fast-forwards the `WaitForVBla` idle spin, so it shares no instruction/cycle
   axis with a literal interpreter. Pairing #2 therefore only has the **frame**
   clock. We cannot (yet) say "VBLANK fired at a different guest cycle" against
   the oracle the way psxrecomp did against its interp.

2. **Whole-region sub-hashes over-report divergence.** `cosim_state.c` /
   `cosim_visible.c` hash entire regions (8–64 KB); a single differing byte
   flags the whole frame as diverged (→ misleading "ram 9 % / vdp 43 %"
   match-rates in `divergence_report` section [3]). The **honest** metric is the
   **chunk-level** section [4] (`memchunks`). Section [3]'s percentages should be
   read as "did ANY byte differ", not "how divergent". Not yet reconciled into
   one number.

3. **No byte-level value-vs-phase discrimination.** `memchunks` localizes *which*
   chunk forks, not *why*. We cannot yet tell a **value** divergence (wrong SMPS
   track data/pointers = real bug) from a **phase/counter** divergence (same
   data, different tick-timer = benign timing). Needs a byte-dump command + the
   SMPS Z80 track-struct layout.

4. **`z80ram` is mis-classified as must-match.** SMPS sound-driver scratch is
   timing-sensitive and diverges from a cycle-accurate reference even when the
   runner is "correct enough". It belongs with the volatile surface; only
   VDP/display is a clean must-match invariant. The report's MUST-MATCH set still
   lists z80ram.

5. **No cross-backend AUDIO comparison of the applied stream.** clownmdemu
   renders its *own* FM (not through our mixer), so we can't diff its
   applied-through-our-mixer output. `APPLIEDDUMP` captures ours only.

6. **Pairing #2 cascades after the first divergence.** Once game state forks
   (RNG/timers), everything downstream forks — cross-backend comparison is
   meaningless past the first divergence (the layered-parity wall). So we cannot
   cross-validate the attract **demo gameplay** (starts ~frame 555) without first
   re-syncing state. `START_FRAME` skips a prologue but does not re-sync.

7. **Sonic-1-specific bring-up.** `--waitvbl-pc` defaults to `29a8` (S1);
   `divergence_report` region layout (`$FFFD00` sound region, chunk sizes) and
   port defaults are S1-flavored. Not yet parameterized per game.

8. **The interleave-IRQ scaffold is NOT a fix.** `GENESIS_INTERLEAVE_IRQ`
   (glue.c) runs handlers on the game fiber (budget-interleaved) instead of
   atomically. Measured deterministic + genuinely active (Z80 steps during the
   handler), but a **no-op on the frame-54 divergence** (that divergence is
   pre-scream). Kept default-OFF as correct architecture for when the scream
   *becomes* the frontier; do not treat it as a solution.

9. **Gate 3 (injection) only exercises a CPU register flip.** It proves the tool
   detects divergence and names `cpu68k`, but doesn't yet inject into VDP/Z80/RAM
   to prove those field-diffs surface.

## Gotchas

- Cosim builds **park** waiting for the coordinator; run standalone → they hang.
- Kill stray `_cosim`/`_oracle_cosim` processes between runs (port reuse) — the
  coordinator does this, ad-hoc scripts must too.
- The `_oracle_cosim` build is slower (clownmdemu + interp sandbox); the plain
  `_oracle` build renders no audio through our mixer (uses clownmdemu's FM).
