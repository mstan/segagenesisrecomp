# CLAUDE.md — segagenesisrecomp

This file is the entry point for any Claude Code session working on the
Genesis static recompiler. It assumes you've also read `PRINCIPLES.md` and
`DEBUG.md` in this directory.

## What this repo is

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROMs
into native C, paired with a runner that links the generated C against
clownmdemu-core for VDP/audio/Z80 emulation. Currently used by:

- **SonicTheHedgehogRecomp** — Sonic 1 release; Green Hill Zone fully
  playable.
- **SonicTheHedgehog2Recomp** — Sonic 2 release; bring-up in progress.
- **sonic3k/** — placeholder, awaiting skdisasm wiring.

## Repo topology (read this first)

There are THREE repos in play:

```
F:\Projects\segagenesisrecomp-release\
├── SonicTheHedgehogRecomp\           (Sonic 1 release repo)
│   ├── CMakeLists.txt                ← references segagenesisrecomp/runner/
│   ├── segagenesisrecomp\            ← submodule; THIS REPO
│   └── tools\                        ← Sonic-1-flavored probes
│
├── SonicTheHedgehog2Recomp\          (Sonic 2 release repo)
│   ├── CMakeLists.txt                ← references segagenesisrecomp/runner/
│   │                                   (via ../SonicTheHedgehogRecomp/segagenesisrecomp/)
│   └── tools\                        ← Sonic-2 probes (cleaner, fewer)
│
└── (this submodule, checked out as SonicTheHedgehogRecomp/segagenesisrecomp/)
    ├── PRINCIPLES.md                 ← rules; READ FIRST
    ├── CLAUDE.md                     ← (this file)
    ├── DEBUG.md                      ← ring inventory + TCP commands
    ├── recompiler\                   ← C++ tool; builds GenesisRecomp.exe
    ├── runner\                       ← SHARED ENGINE; consumed by both releases
    │   ├── glue.c, cmd_server.c, oracle_trace.c, frame_snapshots.c, ...
    │   ├── main.c, audio.c, audio/, crash_report.c, hybrid*.c, ...
    │   ├── stub_clown68000.c         ← native-only (linked into native target)
    │   ├── external/SDL2/            ← bundled SDL2
    │   └── include/genesis_runtime.h ← shared interface header
    ├── tools\                        ← shared genesis-agnostic tooling
    ├── sonicthehedgehog\             ← Sonic 1 game.toml + generated/ + sonic1_spec.c
    │                                   + sonic1_hybrid_table.c + sonic_extras.{c,h}
    ├── sonicthehedgehog2\            ← Sonic 2 game.toml + generated/ + sonic2_spec.c
    │                                   + sonic2_hybrid_table.c
    ├── sonic3k\                      ← placeholder
    ├── tests\
    │   └── tools\                    ← gen_disasm_*, recompiler-side
    └── clownmdemu-core\              ← embedded emulator
```

**Topology invariant**: shared runner is at `segagenesisrecomp/runner/`.
Per-game code (`<game>_spec.c`, `<game>_hybrid_table.c`, `<game>_extras.{c,h}`)
lives next to that game's `generated/` directory. Sonic 2's release repo
has no runner of its own — it reaches through Sonic 1's release repo only
to get to the submodule.

When in doubt about "which runner is built": grep the relevant
`CMakeLists.txt` for `RUNNER_ROOT` — that's the source of truth.

## Per-game contract

Shared runner code reads two tables:

### `g_game_spec` (function-pointer hooks)

Defined in `runner/game_spec.h`. Each game project provides exactly one
TU defining `const GameSpec g_game_spec` (e.g.
`sonicthehedgehog/sonic1_spec.c`, `sonicthehedgehog2/sonic2_spec.c`).
Fields include identity (name, CRC32, ROM size), entry/IRQ/periodic
callbacks, lifecycle hooks (`on_post_reset`, `on_frame_pre/post`), CLI
handler, dispatch override, frame-record packer, per-game TCP commands,
and hybrid table for oracle build.

### `g_game_layout` (WRAM addresses)

Defined in `runner/game_layout.h`. Populated from `[ram_layout]` in
`game.toml` by the recompiler, emitted as `<prefix>_layout.c` per game. Current fields: `game_mode_addr`,
`vint_runcount_addr`, `vint_routine_addr`, `plc_pending_addr`,
`initial_ssp`, `vbla_stack`, `intr_stack`, `player_object_addr`,
`level_modes[]`.

**Adding a new layout field** (until Wave 1B's X-macro lands):
1. Add to `GameRamLayoutCfg` in `recompiler/src/game_config.h`.
2. Parse in `recompiler/src/game_config.c`.
3. Emit in `recompiler/src/game_config_emit_layout()`.
4. Add to `GameRamLayout` in `runner/game_layout.h`.
5. Set in every per-game `game.toml`.
6. Make the recompiler hard-fail if the field is missing.

## The dispatch-miss loop

After EVERY game run, check `dispatch_misses.log` next to the executable.

```bash
cat /f/Projects/segagenesisrecomp-release/SonicTheHedgehog2Recomp/build/Release/dispatch_misses.log
```

Empty file → done. Non-empty → resolve via the disasm-driven pipeline
(NOT by hand-adding extra_func entries from the log alone — see
PRINCIPLES.md #16).

## The always-on ring philosophy

See `DEBUG.md` for the full ring inventory. The short version:

- bus_ring captures every M68K bus access.
- frame_record captures full per-frame state.
- reverse_debug Tier-1 captures every WRAM write with frame + caller.
- oracle_trace Tier-3 captures per-instruction CPU state (oracle build).
- crash_report captures recent function entries.

Probes connect → query backward → analyze. **Do NOT arm-then-record.** See
PRINCIPLES.md #17.

## Build commands

### Recompiler

```bash
powershell.exe -NoProfile -Command "& 'F:/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/segagenesisrecomp/recompiler/_build_recomp.bat'"
```

### Sonic 1 — regen + native build

```bash
cd /f/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/segagenesisrecomp/sonicthehedgehog
python ../tests/tools/gen_disasm_seeds.py      -o sonic1.disasm_seeds.toml
python ../tests/tools/gen_disasm_subs.py       -o sonic1.disasm_subs.toml
python ../tests/tools/gen_disasm_jumptables.py -o sonic1.disasm_jumptables.toml
"/f/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/segagenesisrecomp/recompiler/build/Release/GenesisRecomp.exe" sonic.bin --game game.toml
powershell.exe -NoProfile -Command "& 'F:/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/_build_native.bat'"
/c/Windows/System32/taskkill.exe //F //IM SonicTheHedgehogRecomp.exe 2>/dev/null
cd /f/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/build/Release
cmd.exe //C "start /B SonicTheHedgehogRecomp.exe sonic.bin --port 4380 > native_run.log 2>&1"
```

### Sonic 2 — regen + native + oracle

```bash
cd /f/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/segagenesisrecomp/sonicthehedgehog2
python ../tests/tools/gen_disasm_labels.py     s2disasm/s2.lst > sonic2.disasm_labels.toml
python ../tests/tools/gen_disasm_jumptables.py --lst s2disasm/s2.lst --code-addrs ../tests/fixtures/sonic2/l1/code_addresses.txt --prefix sonic2 --rom-max 0x100000 -o sonic2.disasm_jumptables.toml
"/f/Projects/segagenesisrecomp-release/SonicTheHedgehogRecomp/segagenesisrecomp/recompiler/build/Release/GenesisRecomp.exe" sonic2.bin --game game.toml
powershell.exe -NoProfile -Command "& 'F:/Projects/segagenesisrecomp-release/SonicTheHedgehog2Recomp/_build_native.bat'"
/c/Windows/System32/taskkill.exe //F //IM SonicTheHedgehog2Recomp.exe 2>/dev/null
cd /f/Projects/segagenesisrecomp-release/SonicTheHedgehog2Recomp/build/Release
cmd.exe //C "start /B SonicTheHedgehog2Recomp.exe sonic2.bin --port 4380 > native_run.log 2>&1"
# oracle (paired)
cmd.exe //C "start /B SonicTheHedgehog2Recomp_oracle.exe sonic2.bin --port 4381 > oracle_run.log 2>&1"
```

VS-bundled cmake is required. PATH-resolved cmake from devkitPro/msys2
doesn't pick up the BuildTools generator. The `_build_*.bat` wrappers
already use the right one.

## Hard rules (PRINCIPLES.md cheat sheet)

- Disasm is ground truth (#16). Function discovery via `gen_disasm_*`,
  not `dispatch_misses.log` feedback.
- Always-on rings, never arm-then-attach (#17). No pause/step.
- No printf telemetry in hot paths (#18).
- Never edit generated C (#19). Fix recompiler → regenerate.
- Submodule commit order: this submodule first → release repos second
  (#20).
- Per-game data through `g_game_spec` / `g_game_layout`, never literal
  hex in shared runner (#21).
- Free-running differ over pause/step (#22).
- Boot-smoke baseline changes commit alongside their code change (#23).
- One runtime instance at a time (#24); `taskkill` before relaunch.
- User verifies end-to-end (#25). Don't claim "fixed" without their
  confirmation.
- **Never commit without explicit user instruction.** The user runs
  `git status` themselves and asks for commits when ready.
- **Never publish a binary release without following `RELEASING.md`.** Release
  (native) binaries are AGPL-free — clean-room own backend, zero clownmdemu
  objects linked AND zero clownmdemu headers compiled (native include lists
  contain no clownmdemu-core paths). They ship under the project license
  (PolyForm Noncommercial 1.0.0) + `THIRD-PARTY-LICENSES.md` (ymfm BSD-3,
  superzazu MIT, clowncommon ISC, SDL2 zlib) and contain NO ROM / dumps /
  saves / logs. NEVER ship an `_oracle` exe or `GenesisRecomp.exe` — those
  statically link AGPL code and are dev-only. Package with
  `tools/package_release.py` (it refuses if a ROM/junk slips in) — never zip
  a build folder by hand.

## Active improvement plan

The `humming-wibbling-hammock.md` plan (in the user's `.claude/plans/`
directory) lays out waves 0A → 5 of tooling/discipline upgrades. We are
currently in Wave 0A (this docs landing).

## Pointers

- `PRINCIPLES.md` — full rule set (25 principles).
- `DEBUG.md` — always-on ring inventory + TCP commands.
- `tools/audit_runner_purity.py` — greps shared runner for forbidden
  literals and per-game leakage.
