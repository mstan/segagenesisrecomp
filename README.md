<p align="center">
  <img src="logo.png" alt="Genesis Recomp" width="640" />
</p>

# Genesis 68K Static Recompiler

A static-recompilation framework that translates Sega Genesis (Mega Drive)
68000 ROM code into native C and runs it against a clean-room Genesis runtime.
Game repositories such as
[SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp)
provide the ROM-specific build, assets, and release packaging.

## Status

| Feature | Status | Notes |
|---------|--------|-------|
| 68K frontend | Active | Shared decoder, validator, discovery, and emitter from `m68k-recomp-core` |
| Runtime fallback | Active | Clean-room Tier-3 interpreter handles supported static-dispatch misses |
| Runtime hardware | Active | Own VDP, bus, scheduler, YM2612, SN76489, and Z80 integration |
| Function discovery | Evidence-driven | Static analysis plus disassembly and runtime evidence declared by `game.toml` |
| Validation | Local | Synthetic harnesses, boot/regression scripts, and recomp-vs-interpreter cosim |
| Sound Z80 static recompilation | Experimental | Optional backend; see [docs/Z80_STATIC_RECOMP.md](docs/Z80_STATIC_RECOMP.md) |
| Widescreen injection | Per game | Current status and remaining conversions are in [WIDESCREEN_ISSUES.md](WIDESCREEN_ISSUES.md) |

## What's In This Repo

| Directory | Purpose |
|-----------|---------|
| `recompiler/` | Genesis-specific CLI, ROM parser, and TOML configuration loader |
| `external/m68k-recomp-core/` | Shared 68000 decoder, validator, discovery, and code-emission profiles |
| `external/z80-recomp-core/` | [Shared Z80 generated-code ABI and verified instruction semantics](https://github.com/mstan/z80-recomp-core) — pinned submodule shared with SMS/GG Recomp |
| `runner/` | Clean-room runtime, debugger, audio, video, input, and netplay integration |
| `tests/` | ROM-independent synthetic harnesses plus optional ROM-backed decoder fixtures |
| `sonicthehedgehog/`, `sonicthehedgehog2/`, `sonic3/`, `sandk/`, `sonic3k/`, `puyo/`, `rka/` | Per-game configuration, evidence, and hooks |
| `<game build>/generated/<prefix>/` | Ignored output regenerated from the ROM, config, and current recompiler |

## How It Works

The Genesis CLI loads a ROM and `game.toml`, then invokes the Genesis profile in
`m68k-recomp-core`. Discovered 68K routines become C functions operating on the
shared `M68KState` and runtime bus. Generated dispatch code falls back to the
clean-room interpreter for supported dynamic targets that were not statically
materialized.

Key recompiler features:
- **`addq.l #4,sp` early-exit detection**: Pre-scans each function for stack pointer adjustments. Emits local `_sp_popped` counter so `rts` propagates returns through the caller's post-JSR check via `g_rte_pending`.
- **Dispatch table accessor generation**: `game_dispatch_table_size()` and `game_dispatch_table_addr()` enable runtime interior label detection.
- **Per-instruction cycle estimation**: Emitted costs come from the clean-room
  MC68000 timing model in the shared Genesis profile, so generation has no
  emulator dependency.

## Cloning

```bash
git clone --recursive https://github.com/mstan/segagenesisrecomp.git
```

The recursive clone checks out the shared 68000, Z80, and netplay submodules.
There is no emulator-core or AGPL dependency in the repository.

## Platform Support

The runner builds and runs natively on **Windows (MSVC)**, **macOS (Apple
Silicon & Intel)**, and **Linux**. The cooperative game-fiber scheduler is
backed by Win32 Fibers on Windows and by `ucontext` on macOS/Linux via
`runner/fiber_compat.{h,c}`; SDL2 handles windowing, rendering, audio, and
`SDL_GameController` gamepads on all platforms. See the
[SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp)
README for per-platform build steps.

## Building the Recompiler

The recompiler is C11 and builds with CMake and a C toolchain.

```bash
cd recompiler

# Windows (MSVC)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# macOS / Linux (Ninja)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

## Running Framework Tests

The ROM-independent harnesses can be configured directly:

```bash
cmake -S tests -B build/tests
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

`l1_decoder_test` is built but not registered with CTest because it requires a
user-supplied Sonic ROM. Its `--help` output documents the fixture and ROM
arguments.

## Regenerating Output

```bash
cd sonicthehedgehog
../recompiler/build/Release/GenesisRecomp.exe sonic.bin --game game.toml --output-dir generated
```

The manual command writes ignored files under `generated/`. Normal game CMake
builds isolate them under that build tree's `generated/<prefix>/` directory
and rebuild whenever the ROM, game config, discovery inputs, annotations,
recompiler, or generation options change. Generated C is deliberately not
tracked in Git.

## Building and Running the Game

See **[SonicTheHedgehogRecomp](https://github.com/mstan/SonicTheHedgehogRecomp)** for build instructions, controls, and known issues.

## Audio & Video Enhancements (opt-in)

The runner ships an optional **verified-enhancement shadow** layer: quality-of-life
audio/video improvements that run *alongside* the authentic hardware emulation
without ever replacing it as ground truth. The governing rule (see
[`docs/SHADOW_ENHANCEMENTS.md`](docs/SHADOW_ENHANCEMENTS.md)):

- The authentic (canon) path stays the authoritative output **and** the verify
  oracle — the shadow is never ground truth.
- The shadow is continuously diff-checked against canon and substitutes **only
  after a proven correlation window**.
- It **reverts loudly** (logs `[AUDIO-SHADOW] DEGRADED …`) the instant it stops
  matching.
- It is **off by default**. With it off, output is byte-identical to the raw
  hardware emulation (the `[FBHASH]` framebuffer hash, PNG dumps, and WAV capture
  all stay on the raw canon). Worst case when on is "you see/hear authentic
  hardware output," and it cannot mask a recompiler bug because the canon path it
  shadows is still the thing being diffed.

All options are environment variables, default OFF:

| Variable | Values | Effect |
|----------|--------|--------|
| `GENESIS_SCREEN` | `raw` (default), `crt`, `trinitron`, `composite`, `linear` | Present-time color model. `raw` reproduces the existing 9-bit→ARGB8888 expansion bit-identically. The others map the Genesis 9-bit (BGR 3-3-3) gamut through a CIE color core (panel primaries → sRGB via Bradford + sRGB OETF): `crt` = generic SMPTE-C phosphor + γ≈2.4 + lifted black, `trinitron` = Sony P22-ish primaries, `composite` = gentler γ + higher black floor, `linear` = clean linear-light reference. Per-pixel color only — it does **not** synthesize composite/RF spatial artifacts (dot crawl, cross-color, NTSC blur). |
| `GENESIS_AUDIO_SHADOW` | `0` (default) / `1` | Arms the YM2612 FM shadow. A second `ymfm` chip is fed the identical register-write stream as the canon chip but rendered with a **relaxed output low-pass** (near-passthrough) instead of the aggressive filter canon uses to match the reference — preserving the cleaner, less-aliased high end. Gain is identical; the verifier substitutes the FM stereo samples only once proven. The SN76489 (PSG) path is untouched. |
| `GENESIS_FM_LADDER` | unset (default) / `off` | When `off`, the FM shadow renders through ymfm's **`ym3438`** chip — the same FM engine without the YM2612's DAC "ladder" discontinuity (the ~7-LSB crossover gap around zero, i.e. the audible low-level crunch). This is the later CMOS Genesis FM revision that fixed it; no hand-rolled correction curve. Requires `GENESIS_AUDIO_SHADOW=1`. Verifier-policed like any other enhancement. |

Example (all on):

```bash
GENESIS_AUDIO_SHADOW=1 GENESIS_FM_LADDER=off GENESIS_SCREEN=crt \
    ./Sonic3KRecomp sonic3k.bin
```

These are present-time/runtime only and link no clownmdemu — they are part of
the AGPL-free native runtime and safe to ship. To wire them into a game build,
add `runner/audio/audio_shadow.c`, `runner/audio/fm_shadow.cpp`, and
`runner/video/color_lut.c` to that game's source list (the `video/` and
`external/ymfm/src` include dirs are already present). See
[`docs/SHADOW_ENHANCEMENTS.md`](docs/SHADOW_ENHANCEMENTS.md) for the full design,
verifier algorithm, and integration points.

### Widescreen (16:9, opt-in)

`GENESIS_WIDESCREEN=1` renders a 16:9 viewport (default 448px wide, configurable
via `GENESIS_WIDESCREEN_COLUMNS=<cells/side>`) in place of the authentic 4:3
320px. The clean-room VDP draws extra **centered** columns; the recompiled 68K
reads a per-frame `Widescreen_extra` RAM word (written by the runner) to widen
its own tile-load, object-cull, ring-window, and sprite bounds. The word is `0`
when the toggle is off, so **the authentic 4:3 output stays byte-identical**. It
is per-game and gameplay-gated — authentic 4:3 on menus, title cards, and
2-player split-screen. Supported in Sonic 1, Sonic 2, and the Sonic 3 family
(S3-alone and S&K-alone; the combined S3&K build is bring-up). See the
`[widescreen]` block in each `game.toml`.

Converted games ingest the canonical ROM and apply declarative
`[[widescreen_site]]` transforms while emitting C. The original ROM is not
patched. See [WIDESCREEN_ISSUES.md](WIDESCREEN_ISSUES.md) for games that still
use the older patched-disassembly path.

#### Disassembly sources

The optional widescreen builds are produced from these community Sega Genesis
disassemblies (build-time sources only — the shipped binaries are recompiled C,
and the 4:3 path reassembles byte-identically to the canonical ROMs):

- **Sonic the Hedgehog** — [Sonic Retro `s1disasm`](https://github.com/sonicretro/s1disasm)
- **Sonic the Hedgehog 2** — [Sonic Retro `s2disasm`](https://github.com/sonicretro/s2disasm)
- **Sonic 3 & Knuckles** — [Sonic Retro `skdisasm`](https://github.com/sonicretro/skdisasm)

With thanks to the Sonic Retro community for maintaining these disassemblies.

## Key Files

| File | Purpose |
|------|---------|
| `external/m68k-recomp-core/profiles/genesis/code_generator.c` | Genesis 68K → C emitter and timing profile |
| `external/m68k-recomp-core/common/m68k_decoder.c` | Shared 68K instruction decoder |
| `recompiler/src/game_config.c` | Genesis `game.toml` loader and validation |
| `runner/include/genesis_runtime.h` | Shared interface: `M68KState`, `g_rte_pending`, `g_early_return`, bus access |
| `<game build>/generated/<prefix>/*_partNN.c` | Ignored build output containing generated functions |
| `<game build>/generated/<prefix>/*_dispatch.c` | Ignored build output containing the dispatch table |
| `sonicthehedgehog/game.toml` | Sonic 1 configuration, evidence inputs, and optional transforms |

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md) — free for non-commercial use.
Third-party notices are collected in
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
