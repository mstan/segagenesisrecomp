# Rocket Knight Adventures support handoff

> **Historical snapshot (July 2026).** This handoff preserves an unfinished
> investigation state. Use the repository [README](../../README.md), current
> `rka/game.toml`, and open Beads issues for authoritative work status.

## Objective and acceptance gate

This branch is bringing **Rocket Knight Adventures (USA)** up as the fourth
supported game in `segagenesisrecomp`.

RKA is not complete until all of the following are true:

1. The entire attract sequence runs at normal rate without crashes, frame
   drops, crackly audio, or dropped audio.
2. After attract, the player can enter a real game and fuzz controls (primarily
   hold Right, with occasional jumps/attacks) without graphical corruption or
   gameplay glitches.
3. Only after RKA passes those gates, run Sonic 1, Sonic 2, Sonic 3, Sonic 3K,
   and S&K runtime regressions. Decoder work must not discover new Sonic
   functions; a new Sonic function is a warning/failure unless independently
   justified.

The current blocker is item 2. **Foreground/background plane scrolling is
still visibly broken in manual gameplay. Do not report this port complete.**

## Repository and build layout

- Active engine worktree: `F:\Projects\segagenesisrecomp\segagenesisrecomp`
- Branch: `feat/rka-oracle-parity`
- RKA CMake/build wrapper:
  `F:\Projects\segagenesisrecomp\RocketKnightAdventuresRecomp`
- Executables:
  `F:\Projects\segagenesisrecomp\RocketKnightAdventuresRecomp\build\Release`
- ROM beside the executables: `rka.bin`
- Ghidra MCP server name: `genesis`
- Active Ghidra program at handoff:
  `Rocket Knight Adventures (USA).md`, language `68000:BE:32:default`

Build the recompiler:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  recompiler\build\GenesisRecomp.sln /m /p:Configuration=Release /p:Platform=x64
```

Build both RKA native and oracle runners:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  F:\Projects\segagenesisrecomp\RocketKnightAdventuresRecomp\build\RocketKnightAdventuresRecomp.sln `
  /m /p:Configuration=Release /p:Platform=x64
```

Launch a real user-controlled native run (normal rate, no input script):

```powershell
cd F:\Projects\segagenesisrecomp\RocketKnightAdventuresRecomp\build\Release
.\RKARecomp.exe rka.bin --no-launcher
```

Default controls: arrows move, `X` is Genesis B/jump, `Z`/`C` attack, Enter is
Start. There are no live RKA processes at handoff.

## New commits in this handoff

All work is committed. The worktree is clean other than deliberately ignored
large local audit directories under `scratch/decoder_*`.

- `3a4108e runner: support RKA RAM handlers and audio timing`
  - Executes narrow RAM-resident RTS/shift helpers and exact copied-ROM raster
    handlers through the native dispatcher.
  - Fixes Z80 `BIT b,(HL)` erroneously writing back to the YM2612 port.
  - Adds CPU-visible YM2612 timer-B status behavior needed by RKA's sound
    driver.
  - Adds IRQ debt ownership/latching and watchdog yield handling.
  - Adds generic WRAM hash/memory-write diagnostics.
- `6449ae7 recompiler: allow explicitly bounded long dispatch tables`
  - Decoder-only and intentionally isolated so it can be reverted/cherry-picked.
  - A matching explicit `abs`, stride-4 `[[jump_table]]` may exceed the global
    64-entry heuristic cap; entries beyond the cap are non-recursive roots.
- `5c13225 rka: expand discovered object and attract handlers`
  - Adds RKA late roots and the exact 391-entry object dispatch table.
  - Regenerates RKA C and dispatch audit output.
  - Adds the 12,000-frame executed-PC evidence file referenced by `game.toml`.
- `d3bec4f rka: add attract and parity diagnostic artifacts`
  - Adds input scripts, coverage/function-set trials, interior-label logs, and
    `tools/rka_cosim.py`.
  - The co-sim tool is explicitly marked experimental; its forced checkpoint
    is not valid acceptance evidence.

Earlier precision-discovery commits on this branch are:

- `6edf3de recompiler: add precision-gated late discovery`
- `bb8c6af recompiler: close direct calls from decoded switch bodies`

Do not amend the decoder commits. Keep any further decoder change isolated.

## What is working now

- RKA boots and runs attract content.
- Sparkster was initially invisible; he is now visible, and his attacks/actions
  work.
- A previously stationary cart now moves.
- A native 4,000-frame attract run completed with zero true dispatch misses.
- A native 12,000-frame run completed without a crash. It recorded three
  handled interior-label targets late in the run (`$016BD4`, `$016A0A`,
  `$016A1C`), 354 raw events, and zero unique true missing addresses.
- Earlier captured WAV diagnostics were dynamic, had no >=1-second gaps, and
  had sane peak/RMS. Real-time speaker acceptance still needs a final normal-
  rate human pass after graphics are fixed.
- The old title/attract watchdog crash around frame 1694 does not reproduce
  with the bounded table build.

Do not overread the 12,000-frame result: it proves the process survived that
window, not that every visual transition or the entire multi-part attract loop
is correct.

## Current reproducible failure

In real manual gameplay, holding Right advances Sparkster's physics correctly:
he rises and falls according to terrain collision that is not visually in the
right place. The visual scrolling/streaming is stale or only partially updated.

The clearest user observation/screenshot showed:

- Sparkster moving over physically correct invisible terrain contours.
- A half-tree and a fire/tree fragment appearing/disappearing as he moves.
- The terrain/sprite logic remains active; the scene planes do not compose or
  refill correctly.

This strongly moves the investigation away from generic object dispatch and
toward the camera -> VDP scroll -> plane tilemap/VRAM streaming pipeline.

Likely categories to distinguish, in order:

1. Camera coordinates advance in WRAM but the VDP horizontal scroll value/table
   does not.
2. Scroll advances but new Plane A or Plane B name-table columns are not
   streamed into VRAM at tile boundaries.
3. The right data is produced but DMA destination, VDP command construction,
   auto-increment, or write timing is wrong.
4. VRAM/VDP state matches an emulator but the native renderer composes the
   plane incorrectly.
5. A visual-update routine exists but is semantically miscompiled internally.
   Zero dispatch misses does not rule this out.

The screenshot evidence means collision/object logic alone is not a useful
success proxy.

## Why the previous investigation kept missing it

Several real missing handlers caused visible symptoms earlier (invisible
Sparkster and stationary cart), so the investigation overgeneralized that
pattern and treated scrolling as another missing-function problem.

That was not supported by the later evidence:

- Adding `$023BD2`, object-table index 65, closed a real coverage hole but did
  not fix scrolling.
- Expanding RKA's central object table from the old hard cap of 64 to all 391
  structurally valid pointers did not fix scrolling.
- No true dispatch miss only proves that indirect control flow had an execution
  destination. It does not prove correct VDP writes or correct instruction
  semantics inside a discovered function.
- Static screenshots and a frame-count completion test cannot verify continuous
  tile-column streaming.

Therefore the 391-entry decoder change is useful ecosystem coverage but is not
the scrolling fix. Retain/revert it on its own merits, not as a claimed fix for
this issue.

## Decoder details and Sonic safety audit

RKA's central object dispatcher is at ROM `$23B6` and indexes an absolute-long
pointer table at `$23D8`:

```asm
lea     $23D8,A0
move.w  (A1),D0
add.w   D0,D0
add.w   D0,D0
movea.l (A0,D0),A0
jmp     (A0)
```

The table contains exactly 391 consecutive even in-ROM pointers. Its exclusive
end is `$29F4`; the next longword is `$4E750000` (RTS plus padding), providing a
structural bound. Entries over 64 are added as non-recursive roots.

Current RKA generation reported approximately:

- FunctionFinder: 1,394 functions
- bounded promotions: 327
- final codegen entries: 1,601
- dispatch audit: 456 sites, zero unsuppressed indirect sites
- five interior-unresolved warnings
- unsupported total: 117 (`MN_OTHER=93`, invalid store EA=20,
  EA fallback=4 at the time of generation)

The bounded decoder was compared address-for-address against the stored
pre-change Sonic baselines, excluding the dump header. Results:

| Game | Baseline | Current | New | Lost |
|---|---:|---:|---:|---:|
| Sonic 1 | 2,076 | 2,076 | 0 | 0 |
| Sonic 2 | 5,409 | 5,409 | 0 | 0 |
| Sonic 3 | 11,051 | 11,051 | 0 | 0 |
| Sonic 3K | 30,475 | 30,475 | 0 | 0 |
| S&K | 19,314 | 19,314 | 0 | 0 |

The comparison products are local/ignored under
`scratch/decoder_audit/baseline/*/{baseline,bounded}_funcs.txt`. They are not
committed because that tree contains copied ROM/disassembly/build products and
is roughly 469 MB.

This is only a function-set audit. Sonic runtime validation remains pending by
explicit project policy until RKA is fully playable.

## Ghidra findings useful for navigation

The main game-mode dispatcher is function `$000536`. It reads `$FFB002`, uses
the PC-relative word table at `$000550`, and jumps to a mode handler.

Relevant mode routes observed while building diagnostics:

- Title/menu is mode 1, with submode in `$FFB004`.
- Title submode 4 processes the two-choice selector at `$FFB010`.
- Selection 0 routes through modes 3 -> 4 (the long opening/prologue path).
- Selection 1 routes through mode 5 and returns to title (options/password
  path), rather than entering gameplay.
- Mode 2 routes to mode 6, then mode 7 (gameplay initialization/dispatcher).
- `$FFB194 == 0` was used as the manual-input/no-demo flag.

These observations were useful for understanding navigation, but writing mode
variables directly is not a valid way to establish an oracle parity checkpoint.

## Co-sim work and why its current result is invalid

`tools/rka_cosim.py` can query the native and oracle frame rings and diff WRAM,
VRAM, VDP, and Z80 records. It exposed several harness problems:

- Native and oracle wall-frame counts do not correspond to the same logical
  attract scene. At the same scripted wall frame, screenshots showed different
  demos/stages.
- RKA's attract loop is multi-part (gameplay demo, title, long story/cinematic
  content), so merely seeing the demo flag clear once is not "end of attract."
- The existing `rka_title.state` is backend-specific: native loads it; the
  oracle rejects it as failed/truncated. It cannot seed both machines.
- Forcing `$FFB002/$FFB004/$FFB194` into mode 2/7 produced native and oracle
  states that already differed massively in WRAM/VRAM at relative frame zero
  and exited in roughly 70 frames. Those diffs are checkpoint contamination,
  not a scrolling root cause.
- The normal frame ring is 600 frames. A temporary 2,048-frame diagnostic build
  was tried and then restored; no ring-size source change remains.

Do not cite the tool's current "FIRST DIVERGENCE at manual tick 0" as evidence
of an engine bug. The tool needs a naturally reached semantic alignment key.

## Recommended next investigation

Stop adding object handlers unless a specific visual-pipeline call edge proves
one is missing. Establish the earliest wrong visual write instead.

1. In Ghidra, identify:
   - RKA's camera X/Y variables.
   - The routine that converts camera position to VDP horizontal/vertical
     scroll state.
   - Plane A and Plane B tilemap column/row streaming routines.
   - DMA/VDP command builders used when camera X crosses an 8/16-pixel tile
     boundary.
2. Instrument native and oracle VDP control/data writes (or a normalized VDP
   command stream), not just final framebuffer hashes.
3. Run both through a **natural** manual start independently with identical
   held-right input. Align samples by level ID + camera X (and, if necessary,
   player X), not by host frame.
4. At the first matching camera boundary, compare:
   - camera coordinates in WRAM,
   - horizontal-scroll register/table values,
   - VRAM name-table destination addresses,
   - tile words written for each plane,
   - VDP register 15 auto-increment,
   - DMA length/source/destination commands.
5. If VDP/VRAM streams first differ, trace the producer backward to the first
   differing WRAM value or instruction. If streams match but framebuffer does
   not, focus on the native VDP renderer/plane wrap/composition implementation.
6. Re-run the visible no-script hold-right test after every candidate fix. The
   acceptance signal is visual alignment with collision, not absence of
   dispatch logs.

A compact regression test should eventually assert a sequence at known camera
X boundaries: expected plane-column writes plus framebuffer hashes. A single
static screenshot is insufficient.

## Important files

- `rka/game.toml` — roots, runtime evidence, exact object table bound.
- `rka/generated/rka_dispatch_audit.log` — indirect-site classification.
- `rka/interior_label_misses.log` — collected interior targets.
- `rka/rka_attract_visual.input` — attract screenshots/coverage run.
- `rka/rka_post_attract_fuzz.input` and `rka/rka_post_title_fuzz.input` —
  earlier fuzz attempts; note that fixed wall-frame navigation is not a valid
  native/oracle alignment method.
- `tools/rka_cosim.py` — experimental parity scaffolding with forced-checkpoint
  warning.
- `runner/glue.c` — RAM copied-handler support and interrupt scheduling.
- `runner/video/genesis_bus.c` — YM timer/status model and bus behavior.
- `recompiler/src/function_finder.c` — bounded long-table mechanism.

## Final status at handoff

- Build: native and oracle Release builds succeeded after restoring the normal
  600-frame ring.
- Decoder build: succeeded.
- Decoder Sonic function-set audit: zero new/lost addresses for all five Sonic
  configurations.
- RKA attract process stability: substantially improved, with 4K and 12K native
  runs completing.
- RKA manual visuals: **failed**; scrolling/tile streaming remains broken.
- RKA real-time final audio acceptance: pending.
- Sonic runtime regressions: intentionally pending.
- Git worktree: clean; large local scratch audit directories ignored.
