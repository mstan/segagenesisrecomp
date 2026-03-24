# GenesisRecomp — Sega Genesis 68K Static Recompiler

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROM binaries into native C code. Given a ROM and a game configuration file, it emits one C function per 68K subroutine — preserving the original memory layout, register state, and calling conventions while executing as compiled x64.

This is a **tech demo with known bugs**. Paired with a game-specific runner, it boots Sonic 1 to the title screen, loads Green Hill Zone, and gameplay runs, but jumping doesn't work and audio is garbled.

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `recompiler/src/` | The recompiler tool |
| `runner/include/` | Shared runtime headers (`genesis_runtime.h`, `game_extras.h`) used by generated code and game runners |
| `runner/external/` | Bundled SDL2 for Windows builds |

### Key Source Files

| File | Size | Purpose |
|------|------|---------|
| `recompiler/src/code_generator.c` | 103K | Main codegen engine — emits native C for each 68K instruction |
| `recompiler/src/m68k_decoder.c` | 35K | 68K instruction decoder and operand parser |
| `recompiler/src/game_config.c` | 4K | Parses game.cfg (extra_func entries, yield hints) |
| `recompiler/src/function_finder.c` | 5K | Discovers function boundaries in the ROM |
| `recompiler/src/main_genesis.c` | 4K | Entry point for GenesisRecomp.exe |
| `runner/include/genesis_runtime.h` | ~3K | Shared interface: M68KState, m68k_read/write, dispatch, cycle tracking |

## How It Works

1. **Decode**: `m68k_decoder.c` parses each 68K instruction in the ROM
2. **Find functions**: `function_finder.c` identifies subroutine boundaries from entry vectors, JSR/BSR targets, and `game.cfg` extra_func entries
3. **Generate**: `code_generator.c` emits a C function for each 68K subroutine. Each instruction becomes C code that manipulates `g_cpu` (D0-D7, A0-A7, SR) and calls `m68k_read/write` for bus access
4. **Output**: Two files — `sonic_full.c` (all functions) and `sonic_dispatch.c` (address-to-function dispatch table)

The generated code is standalone C that links against any runner implementing `genesis_runtime.h`. The runner provides bus access (m68k_read/write), VDP, audio, and I/O.

## Prerequisites

- Visual Studio 2022 (MSVC)
- CMake 3.16+

## Building the Recompiler

```bash
cmake -S recompiler -B build/recompiler -G "Visual Studio 17 2022" -A x64
cmake --build build/recompiler --config Release
```

## Usage

```bash
build/recompiler/Release/GenesisRecomp.exe <rom.bin> --game <game.cfg>
```

This reads the ROM, finds functions, generates C code, and writes `generated/sonic_full.c` and `generated/sonic_dispatch.c` to the game directory specified in `game.cfg`.

## Related Repos

- **[SonicTheHedgehogRecomp](../SonicTheHedgehogRecomp/)** — Game runner: SDL2 host + clownmdemu integration. Build instructions, controls, and known issues.
- **[sonicthehedgehog](../sonicthehedgehog/)** — Generated output, game config, and annotations for Sonic 1.
