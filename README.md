# Genesis 68K Static Recompiler

> **WARNING: This is an early prototype / work in progress.** It is not production-ready, not feature-complete, and has significant known bugs. The generated code works well enough to boot Sonic 1 and play Green Hill Zone, but core gameplay mechanics are broken. This repo is published for educational and archival purposes — expect rough edges everywhere.

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROM binaries into native C code. Paired with the [SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/) runner, the game boots to the title screen, loads Green Hill Zone, and gameplay runs — but **jumping doesn't work, audio is garbled, and several runtime hacks paper over timing issues** that a real Genesis handles naturally.

## Status

This project is in active development. What exists today is a proof of concept demonstrating that static recompilation of Genesis 68K ROMs is viable. Major unsolved problems remain:

- The recompiler does not handle all 68K instructions or addressing modes
- Interrupt timing (VBlank/HBlank) is approximate, not cycle-accurate
- The runner requires 6 runtime workarounds to avoid crashes and state corruption
- Only Sonic 1 has been tested — game-agnostic support is a future goal

| Feature | Status | Notes |
|---------|--------|-------|
| Rendering (VDP, sprites, tilemaps, scroll planes) | Works | |
| SEGA logo + voice sample | Works | |
| Title screen / menus | Works | |
| All zones (GHZ and others via attract demo) | Works | |
| Sonic movement (run, roll, slopes, loops, springs) | Works | |
| Ring pickups | Works | |
| Enemies, item monitors | Works | |
| HUD (score, lives, rings, timer) | Works | |
| Palette fades | Works | Was broken, fixed via register save/restore |
| Function dispatch | Works | 337 discovered functions, zero misses so far. Undiscovered functions likely exist on untested code paths |
| **Jumping** | **Broken** | No height — joypad timing offset causes yvel to be overwritten to 0 |
| **Scattered rings (damage)** | **Broken** | Rings scatter visually when Sonic takes damage, but can't be picked back up |
| **Audio** | **Partial** | "SEGA!" sample plays, music/SFX faintly audible but garbled. Z80/FM don't advance during game code |
| **Some sprite art** | **Broken** | Flower tiles use wrong art (VRAM timing issue) |
| **Boot sequence transitions** | **Broken** | SEGA -> Sonic Team Presents -> title -> level transitions too fast. In-game transitions work fine |

See [SonicTheHedgehogRecomp/STATUS.md](../SonicTheHedgehogRecomp/STATUS.md) for runtime workarounds, architecture details, and failed approaches.

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `genesisrecomp/recompiler/` | The recompiler tool — analyzes a ROM binary and emits native C for every 68K subroutine |
| `genesisrecomp/runner/` | Shared runtime headers (`genesis_runtime.h`, `game_extras.h`) used by both the recompiler output and the game runner |
| `clownmdemu-core/` | [clownmdemu](https://github.com/Clownacy/clownmdemu) emulator core — provides VDP rendering, Z80/FM/PSG audio, and I/O |
| `sonicthehedgehog/generated/` | Generated output for Sonic 1 — 337 native C functions (**READ-ONLY**) |
| `sonicthehedgehog/game.cfg` | Recompiler configuration — function entry points, yield hints, extra_func entries |

## How It Works

The recompiler (`genesisrecomp/recompiler/src/code_generator.c`) decodes every 68K instruction in the ROM and emits equivalent C code. Each 68K subroutine becomes a C function that operates on the same `M68KState` (D0-D7, A0-A7, SR) and memory layout as the original. The generated code calls `m68k_read/write` for bus access, which the game runner routes through clownmdemu's bus layer.

The game runner lives in a separate repo: **[SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/)**. It hosts the generated code inside clownmdemu using a Windows Fiber model for cooperative game/emulator interleaving.

## Prerequisites

- Visual Studio 2022 (MSVC)
- CMake 3.16+

## Cloning

This repo uses git submodules for [clownmdemu](https://github.com/Clownacy/clownmdemu). Clone with `--recursive`:

```bash
git clone --recursive <url>
```

Or if you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Building the Recompiler

```bash
cd genesisrecomp
cmake -S recompiler -B build/recompiler -G "Visual Studio 17 2022" -A x64
cmake --build build/recompiler --config Release
```

## Regenerating Output

```bash
build/recompiler/Release/GenesisRecomp.exe <path-to-sonic.bin> --game sonicthehedgehog/game.cfg
```

This overwrites `sonicthehedgehog/generated/sonic_full.c` and `sonic_dispatch.c`. After regenerating, rebuild the game runner in [SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/).

## Building and Running the Game

See **[SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/)** for build instructions, controls, and known issues.

## Key Files

| File | Size | Purpose |
|------|------|---------|
| `genesisrecomp/recompiler/src/code_generator.c` | 103K | Main codegen engine — emits native C for 68K instructions |
| `genesisrecomp/recompiler/src/m68k_decoder.c` | 35K | 68K instruction decoder |
| `genesisrecomp/runner/include/genesis_runtime.h` | ~3K | Shared interface between generated code and runner |
| `sonicthehedgehog/generated/sonic_full.c` | 4.3M | All 337 generated functions (**READ-ONLY**) |
| `sonicthehedgehog/generated/sonic_dispatch.c` | 40K | Address-to-function dispatch table (**READ-ONLY**) |
| `sonicthehedgehog/game.cfg` | ~380 lines | 337 extra_func entries + yield hints |

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md) — free for non-commercial use. See [LICENSE.md](LICENSE.md) for details.

Note: `clownmdemu-core/` is third-party code with its own license. See `clownmdemu-core/LICENCE.txt`.
