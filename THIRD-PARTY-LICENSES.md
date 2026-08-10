# Third-Party Components and Licenses

Third-party licenses remain separate from the project's
[PolyForm Noncommercial license](LICENSE.md). Current source, recompiler, and
native release paths contain no clownmdemu, clown68000, or clownz80 dependency.
Those retired oracle components exist only in Git history.

## Framework dependencies

| Component | Role | License | Source |
|---|---|---|---|
| **ymfm** (`runner/external/ymfm/`) | YM2612 FM synthesis in native releases | BSD-3-Clause | <https://github.com/aaronsgiles/ymfm> |
| **superzazu/z80** (`runner/external/superzazu/`) | Z80 sound-CPU core in native releases | MIT | <https://github.com/superzazu/z80> |
| **clowncommon** (`runner/external/clowncommon/`) | Integer types and small C helpers | ISC | <https://github.com/Clownacy/clowncommon> |
| **SDL2** (`runner/external/SDL2/`) | Windowing, input, rendering, and audio delivery | zlib | <https://libsdl.org> |
| **tomlc99** (`recompiler/src/toml.{c,h}`) | TOML parsing for game configuration | MIT | <https://github.com/cktan/tomlc99> |
| **recomp-net** (`external/recomp-net/`) | Optional netplay transport and lobby support | MIT | <https://github.com/TechnicallyComputers/recomp-net> |
| **ShadowVerifier and color-science core** (`runner/audio/audio_shadow.{c,h}`, `runner/video/color_lut.{c,h}`) | Opt-in verified audio/video enhancements | MIT OR Apache-2.0 | <https://github.com/JRickey/gba-recomp>, ported through the gbarecomp/snesrecomp implementations with permission |

License texts are retained with the vendored or submodule sources:

- `runner/external/ymfm/LICENSE`
- `runner/external/superzazu/LICENSE`
- `runner/external/clowncommon/LICENCE.txt`
- `runner/external/SDL2/SDL2-2.28.5/COPYING.txt`
- the MIT notice embedded at the top of `recompiler/src/toml.c` and `toml.h`
- `external/recomp-net/LICENSE`

`m68k-recomp-core` and `z80-recomp-core` are project-owned shared components
and carry their own license and provenance files in their submodules.

## Game-repository launcher dependencies

Game repositories normally add the shared `recomp-ui` launcher at build time;
it is not vendored by this framework repository. A release that enables it also
contains the following permissive components and must ship their notices and
assets from the selected `recomp-ui` revision:

| Component | Role | License |
|---|---|---|
| **Dear ImGui** | Immediate-mode launcher UI | MIT |
| **stb_image / stb_truetype / stb_image_write** | Image, font, and image-write helpers | Public domain or MIT |
| **tinyfiledialogs** | Native ROM file picker | zlib |
| **Lato** | Launcher typeface | SIL Open Font License 1.1 |

## Compliance notes

- Native release binaries contain no AGPL code. Release packaging must still
  follow [RELEASING.md](RELEASING.md) and include every applicable notice.
- The shipped binary must not contain a game ROM. Users supply their own ROM;
  `*.bin` is ignored and the runtime loads it separately.
- Generated C compiled into a game executable is a machine translation of ROM
  code. The project's own license cannot grant rights to third-party game code.
- This inventory is informational, not legal advice.
