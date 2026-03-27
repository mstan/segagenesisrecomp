# Genesis 68K Static Recompiler

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROM binaries into native C code. Paired with the [SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp) runner, Sonic the Hedgehog's Green Hill Zone is fully playable — all 3 acts including the Robotnik boss fight.

## Status

| Feature | Status | Notes |
|---------|--------|-------|
| 68K instruction decoding | Works | Covers all instructions used by Sonic 1 |
| Static function discovery | Works | Follows JSR/BSR/BRA, seeds from vectors + game.cfg |
| Code generation | Works | Emits native C with correct flag computation |
| `addq.l #4,sp` + `rts` pattern | **Fixed** | Pre-scans for SP adjustments, emits `_sp_popped` tracking |
| **Blacklist support** | **New** | `blacklist_file` directive prevents interior labels from becoming functions |
| Boundary splitting | Works | Auto-discovers cross-function branch targets |
| 530+ functions generated | Works | Zero dispatch misses on GHZ |

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `recompiler/` | The recompiler tool — analyzes a ROM binary and emits native C for every 68K subroutine |
| `runner/` | Shared runtime headers (`genesis_runtime.h`, `game_extras.h`) used by generated code and the game runner |
| `clownmdemu-core/` | [clownmdemu](https://github.com/Clownacy/clownmdemu) emulator core (submodule) |
| `sonicthehedgehog/generated/` | Generated output for Sonic 1 (**READ-ONLY** — regenerated from ROM) |
| `sonicthehedgehog/game.cfg` | Recompiler configuration — function entry points, blacklist, yield hints |
| `sonicthehedgehog/blacklist.txt` | Interior label addresses that must not become function entry points |

## How It Works

The recompiler (`recompiler/src/code_generator.c`) decodes every 68K instruction in the ROM and emits equivalent C code. Each 68K subroutine becomes a C function that operates on the same `M68KState` (D0-D7, A0-A7, SR) and memory layout as the original. The generated code calls `m68k_read/write` for bus access, which the game runner routes through clownmdemu's bus layer.

### Blacklist

Some ROM addresses look like function entry points but are actually **interior labels** — branch targets or loop points inside larger functions. Compiling these as separate functions splits the parent in half, causing corrupted state, wrong graphics, or broken audio.

The `blacklist_file` directive in `game.cfg` points to a file of addresses that must never become function entry points. The blacklist is enforced at three points:
1. **Function finder**: blacklisted `extra_func` seeds are skipped
2. **Initial function list**: blacklisted addresses filtered before codegen
3. **Boundary splitting**: auto-discovered interior labels blocked from becoming new functions

### game.cfg Directives

```
output_prefix sonic                    # Prefix for generated file names
annotations annotations_from_disasm.csv  # Label set for readable output
blacklist_file blacklist.txt           # Interior labels to exclude
vblank_yield 0029A8                    # WaitForVBlank function address
extra_func 071B4C                      # Additional function entry point
```

## Prerequisites

- Visual Studio 2022 (MSVC)
- CMake 3.16+

## Cloning

This repo uses git submodules for [clownmdemu](https://github.com/Clownacy/clownmdemu). Clone with `--recursive`:

```bash
git clone --recursive https://github.com/mstan/segagenesisrecomp.git
```

Or if you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Building the Recompiler

```bash
cmake -S recompiler -B build/recompiler -G "Visual Studio 17 2022" -A x64
cmake --build build/recompiler --config Release
```

## Regenerating Output

```bash
build/recompiler/Release/GenesisRecomp.exe <path-to-sonic.bin> --game sonicthehedgehog/game.cfg
```

This overwrites `sonicthehedgehog/generated/sonic_full.c` and `sonic_dispatch.c`. After regenerating, rebuild the game runner in [SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp).

## Discovering New Functions

The recompiler only compiles functions it knows about (from static analysis + `extra_func` entries in `game.cfg`). To expand coverage for new zones or gameplay paths:

1. **Build the interpreter version** of SonicTheHedgehogRecomp (see its README)
2. **Play through the target area** — the interpreter tracks every PC address executed
3. **Dump coverage** via TCP: `python tools/tcp_cmd.py '{"id":1,"cmd":"coverage_dump"}'`
4. **Diff against the dispatch table**: `python tools/diff_coverage.py`
5. **Add new entries** to `game.cfg`, checking each against `blacklist.txt`
6. **Audit for bad splits**: `python tools/audit_all_splits.py`
7. **Regenerate** and rebuild

## Building and Running the Game

See **[SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp)** for build instructions, controls, and known issues.

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md) — free for non-commercial use. See [LICENSE.md](LICENSE.md) for details.

Note: `clownmdemu-core/` is third-party code with its own license. See `clownmdemu-core/LICENCE.txt`.
