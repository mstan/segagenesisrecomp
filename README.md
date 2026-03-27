# Genesis 68K Static Recompiler

A static recompiler that translates Sega Genesis (Mega Drive) 68000 ROM binaries into native C code. Paired with the [SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp) runner, **Green Hill Zone (all 3 acts + boss) is fully playable** with correct jumping, audio, sprite art, and object interactions.

## Status

| Feature | Status | Notes |
|---------|--------|-------|
| 68K instruction coverage | ✅ Comprehensive | All common instructions and addressing modes |
| `addq.l #4,sp` + `rts` pattern | ✅ Fixed | Function-local `_sp_popped` tracking for early-exit stack manipulation |
| Function discovery | ✅ 530+ functions | Static analysis + runtime dispatch miss logging + interpreter coverage |
| Interior label detection | ✅ Automated | Binary search on dispatch table prevents split-function bugs |
| JMP/JSR table dispatch | ✅ Works | Computed jumps route through `call_by_address` |
| Per-instruction cycle costs | ✅ Estimated | Drives VBlank timing via `glue_check_vblank` |
| Generated code correctness | ✅ GHZ verified | All 3 acts completable, boss fight works |
| Later zones | ⚠️ Partial | Functions discovered progressively via gameplay |

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `recompiler/src/` | The recompiler tool — analyzes ROM binary, emits native C |
| `runner/include/` | Shared runtime headers (`genesis_runtime.h`) |
| `clownmdemu-core/` | [clownmdemu](https://github.com/Clownacy/clownmdemu) emulator core (submodule) |
| `sonicthehedgehog/generated/` | Generated output for Sonic 1 — 530+ native C functions (**READ-ONLY**) |
| `sonicthehedgehog/game.cfg` | Recompiler config — 530 extra_func entries |

## How It Works

The recompiler (`recompiler/src/code_generator.c`) decodes every 68K instruction in the ROM and emits equivalent C code. Each 68K subroutine becomes a C function operating on the same `M68KState` (D0–D7, A0–A7, SR) and memory layout as the original.

Key recompiler features:
- **`addq.l #4,sp` early-exit detection**: Pre-scans each function for stack pointer adjustments. Emits local `_sp_popped` counter so `rts` propagates returns through the caller's post-JSR check via `g_rte_pending`.
- **Dispatch table accessor generation**: `game_dispatch_table_size()` and `game_dispatch_table_addr()` enable runtime interior label detection.
- **Per-instruction cycle estimation**: Each instruction emits `g_cycle_accumulator += N` for VBlank timing.

## Cloning

```bash
git clone --recursive https://github.com/mstan/segagenesisrecomp.git
```

## Building the Recompiler

```bash
cd recompiler
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Regenerating Output

```bash
cd sonicthehedgehog
../recompiler/build/Release/GenesisRecomp.exe <path-to-sonic.bin> --game game.cfg
```

This overwrites `generated/sonic_full.c` and `sonic_dispatch.c`. After regenerating, rebuild the game runner.

## Building and Running the Game

See **[SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp)** for build instructions, controls, and known issues.

## Key Files

| File | Purpose |
|------|---------|
| `recompiler/src/code_generator.c` | Main codegen — 68K → C translation, `_sp_popped` pattern, cycle estimation |
| `recompiler/src/m68k_decoder.c` | 68K instruction decoder |
| `runner/include/genesis_runtime.h` | Shared interface: `M68KState`, `g_rte_pending`, `g_early_return`, bus access |
| `sonicthehedgehog/generated/sonic_full.c` | 530+ generated functions (**READ-ONLY**, regenerate with recompiler) |
| `sonicthehedgehog/generated/sonic_dispatch.c` | Dispatch table + interior label accessors (**READ-ONLY**) |
| `sonicthehedgehog/game.cfg` | 530 extra_func entries — discovered via runtime logging + interpreter coverage |

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md) — free for non-commercial use.

`clownmdemu-core/` is third-party code with its own license. See `clownmdemu-core/LICENCE.txt`.
