# Genesis 68K Static Recompiler

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROM binaries into native C code. This is a **tech demo with known bugs** — paired with the [SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/) runner, it boots Sonic 1 to the title screen, loads Green Hill Zone, and gameplay runs, but jumping doesn't work and audio is garbled.

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `genesisrecomp/recompiler/` | The recompiler tool — analyzes a ROM binary and emits native C for every 68K subroutine |
| `genesisrecomp/runner/` | Shared runtime headers (`genesis_runtime.h`, `game_extras.h`) used by both the recompiler output and the game runner |
| `clownmdemu-core/` | [clownmdemu](https://github.com/Clownacy/clownmdemu) emulator core — provides VDP rendering, Z80/FM/PSG audio, and I/O |
| `sonicthehedgehog/generated/` | Generated output for Sonic 1 — 337 native C functions (READ-ONLY) |
| `sonicthehedgehog/game.cfg` | Recompiler configuration — function entry points, yield hints, extra_func entries |

## How It Works

The recompiler (`genesisrecomp/recompiler/src/code_generator.c`) decodes every 68K instruction in the ROM and emits equivalent C code. Each 68K subroutine becomes a C function that operates on the same `M68KState` (D0-D7, A0-A7, SR) and memory layout as the original. The generated code calls `m68k_read/write` for bus access, which the game runner routes through clownmdemu's bus layer.

The game runner lives in a separate repo: **[SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/)**. It hosts the generated code inside clownmdemu using a Windows Fiber model for cooperative game/emulator interleaving.

## Prerequisites

- Visual Studio 2022 (MSVC)
- CMake 3.16+

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
| `sonicthehedgehog/generated/sonic_full.c` | 4.3M | All 337 generated functions (READ-ONLY) |
| `sonicthehedgehog/generated/sonic_dispatch.c` | 40K | Address-to-function dispatch table (READ-ONLY) |
| `sonicthehedgehog/game.cfg` | ~380 lines | 337 extra_func entries + yield hints |
