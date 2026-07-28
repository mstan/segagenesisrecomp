# HANDOFF — Puyo Puyo (Japan) bring-up

**Branch:** `feat/puyo-puyo-bringup` (engine worktree)
**Written:** 2026-07-27
**Goal:** attract demo runs end-to-end with no visual, audio or logic errors,
then fuzz gameplay by navigating menus. Tracked in `.claude/GOAL.md` at the
workspace root.

**Current state: boots, animates, then freezes at frame 120. Not playable.**

---

## 1. Where things are

| Thing | Path |
|---|---|
| Engine worktree (this branch) | `F:\Projects\segagenesisrecomp\_wt-puyo` |
| Game project | `F:\Projects\segagenesisrecomp\PuyoPuyoRecomp` |
| Engine junction from the project | `PuyoPuyoRecomp\engine-local` → `_wt-puyo` |
| Game data | `_wt-puyo\puyo\{game.toml, puyo_spec.c, puyo.bin}` |

`puyo.bin` is **gitignored and deliberately not committed**. Supply your own
dump: 512 KB, CRC32 `7F26614E`, serial `GM G-4082  -00`, region J,
`(C)SEGA 1992.SEP`. Header checksum `$BCE7` matches computed.

Only `external/m68k-recomp-core` needs initialising. Netplay is opt-in as of
engine `b77f56c`, so `recomp-net` is not required, and `clownmdemu` no longer
exists in this project at all.

```bash
cd F:\Projects\segagenesisrecomp\_wt-puyo
git submodule update --init --recursive external/m68k-recomp-core
```

### Build and run

```bash
cmake -S F:/Projects/segagenesisrecomp/PuyoPuyoRecomp -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PuyoRecomp
cd build/Release
./PuyoRecomp.exe puyo.bin --no-launcher --turbo --max-frames 1800 --hash-frames 300
```

`--turbo --max-frames N --hash-frames K` is the validation recipe used across
this project: it exits gracefully, prints `[DONE]`, a `[FBHASH]` per
checkpoint, `[INTERP] N unique true-miss addrs`, and writes
`dispatch_misses.log`. **Distinct hashes are the pass signal** — a live process
proves nothing, because a frozen screen and a working one look identical from
outside.

---

## 2. The symptom, precisely

- Frames 0–50: static (boot).
- Frames 60–110: **six distinct framebuffer hashes** — something animates.
- Frame 120 onward: one hash, `0x71EE868680B99451`, forever. Ran to 1800.
- The 68K is **still executing**: `--framelog` shows the WRAM hash `wh=`
  changing every single frame. This is a live spin loop, not a hang.
- PC cycles among `$00032C`, `$002B26`, `$001020`, `$010D0E`.

⚠️ **Do not trust the framelog's `mode` / `vbl` / `cnt` / `scrl` / `plc`
columns.** They read through `g_game_layout`, and `puyo/game.toml` sets those
to `0x000000` because they are genuinely unknown — so they are reading WRAM
offset 0 and are meaningless. Only `wh=`, `pc=` and the frame index are real.
I nearly misread `vbl=00` as evidence about V-blank. It is not.

---

## 3. What has been ruled out

**Dispatch misses are not the cause.** Discovery walked `$007E68` for `0x7104`
bytes without finding a terminator and swallowed the `$00A4xx` object-handler
cluster, so those handlers landed *inside* a 29 KB function: undispatchable.
The Tier-3 floor correctly declined them (capsule `exit_pc=$000324` vs native
return `$002A44`). The callers are `MOVEA.L $0002(A0),A1; JSR (A1)` at
`$002A42` — per-object function pointers, the same shape as RKA's dispatcher.

Seeding six decode-validated handlers as `late_extra` took discovery 139 → 151
functions and moved the first miss from frame 53 to frame 173 — **but the
freeze stayed at frame 120.** So the handler gap is a real and separate
problem, not this one.

---

## 4. Prime hypothesis for the freeze

A **wait-for-V-int loop**: the main loop polls a flag the V-int handler
(`$000524`) is supposed to update, and either V-int is not being delivered or
the handler is not updating that flag.

Supporting: `$00032C` is adjacent to `$000324`, the `exit_pc` the floor kept
reporting, which suggests that region is the wait loop.

**Next steps, in order:**
1. Confirm whether V-int is being delivered at all.
2. Disassemble `$00032C`, `$002B26`, `$001020` and identify the poll and the
   flag address it reads.
3. That flag address is also the real `vint_runcount` for `[ram_layout]`,
   which is currently `0x000000`. Filling it in makes the framelog columns
   meaningful and unblocks the normal debugging workflow.

---

## 5. Tooling you now have (and its one limit)

Executed-PC coverage was restored on the clean-room Tier-3 interpreter this
session (`aa79489`) — it had been oracle-only and died with the emulator core.

```bash
./PuyoRecomp.exe puyo.bin --no-launcher --turbo --max-frames 300 \
    --exec-coverage-out cov.txt
```

Always-on bitmap marked in `exec_one()`, so it captures every interpreted
path. Verified: a 300-frame Puyo run records 122 PCs across
`$000FB8–$01DBFA`, including both handlers the floor is known to have run.

**LIMIT — read this before relying on it.** Whole-program interpretation
(`GENESIS_FORCE_INTERP`) is still behind `#ifdef GENESIS_COSIM` in `glue.c`
(the function is at `glue.c:483`, call sites around 577/809/879/965). In a
plain native build the dump therefore covers **only Tier-3 floor capsules**,
not the recompiled code — which is most of the program, and probably most of
the spin loop.

To get a complete executed-PC set, either:
- build with `-DGENESIS_BUILD_COSIM=ON` and run with `GENESIS_FORCE_INTERP=1`
  (note `PuyoPuyoRecomp/CMakeLists.txt` was copied from RKA and has **no**
  cosim target yet — you would need to add one), **or**
- un-gate `genesis_force_interp()` and its call sites from `GENESIS_COSIM` so
  any native build can interpret the whole program. This is our own clean-room
  interpreter with no third-party code, so there is no licensing reason for
  the gate. This is the higher-value fix and would fully replace the old
  oracle coverage.

Also available: `floor_coverage.txt` (written next to the exe; addresses the
floor executed, as `extra_func` leads for the next regen) and `--framelog`.

---

## 6. Ground rules that apply here

From `PRINCIPLES.md` — these bit during this session, so they are not
theoretical:

- **#16 — disasm is ground truth.** Do not seed `extra`/`late_extra` straight
  from `dispatch_misses.log`. Every one of the six handlers already seeded was
  decode-checked against the raw ROM first; the opcodes are recorded in
  `game.toml` next to each address so the audit is reviewable.
- **#17 — always-on rings, never arm-then-record.** Query coverage and the
  framelog after a free run; do not pause/step.
- **#19 — never edit generated C.** Fix the recompiler or `game.toml` and let
  CMake regenerate.
- **#21 — no per-game literals in the shared runner.** `puyo_spec.c`
  deliberately leaves the Sonic-shaped WRAM fields at 0 rather than inventing
  addresses. Fill them in only once disasm proves them.
- **Never commit the ROM.**

## 7. Known open items beyond the freeze

- One PC-indexed JMP dispatch site with no static table coverage
  (`puyo.unresolved_jumptables.log` in the generation-stage directory).
- The `$00A4xx` object handlers are being unblocked one per regen. The real
  fix is static enumeration, but a scan for abs-long ROM tables pointing into
  `$00A000–$00B000` found none containing them — `object+$02` appears to be
  written at spawn time per object type rather than from a single table.
  Finding those producers is the actual task.
