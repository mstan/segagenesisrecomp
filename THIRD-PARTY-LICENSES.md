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
| **minicoro** (`runner/external/minicoro/`) | Game-fiber context switch on an engine-owned, snapshottable stack | Unlicense OR MIT-0; its embedded context-switch assembly derives from LuaCoco (MIT, Mike Pall) | <https://github.com/edubart/minicoro> |
| **SDL2** (`runner/external/SDL2/`) | Windowing, input, rendering, and audio delivery | zlib | <https://libsdl.org> |
| **ares Mega-CD device algorithms** (`runner/cd/stamp.c`, `dma.c`, `cdd.c`) | Stamp graphics, CDC DMA and drive motor/TOC progression in the experimental Sega CD backend | ISC | [Pinned source and port details](docs/SEGA_CD_REUSE.md) |
| **tomlc99** (`recompiler/src/toml.{c,h}`) | TOML parsing for game configuration | MIT | <https://github.com/cktan/tomlc99> |
| **recomp-net** (`external/recomp-net/`) | Optional netplay transport and lobby support | MIT | <https://github.com/TechnicallyComputers/recomp-net> |
| **xxHash v0.8.3** (`runner/external/xxhash/`) | Complete rollback-state section checksums (portable XXH64) | BSD-2-Clause | <https://github.com/Cyan4973/xxHash/tree/v0.8.3> |
| **ShadowVerifier and color-science core** (`runner/audio/audio_shadow.{c,h}`, `runner/video/color_lut.{c,h}`) | Opt-in verified audio/video enhancements | MIT OR Apache-2.0 | <https://github.com/JRickey/gba-recomp>, ported through the gbarecomp/snesrecomp implementations with permission |

License texts are retained with the vendored or submodule sources:

- `runner/external/ymfm/LICENSE`
- `runner/external/superzazu/LICENSE`
- `runner/external/clowncommon/LICENCE.txt`
- `runner/external/minicoro/LICENSE`, plus the LuaCoco MIT notice embedded in
  `runner/external/minicoro/minicoro.h`
- `runner/external/SDL2/SDL2-2.28.5/COPYING.txt`
- the MIT notice embedded at the top of `recompiler/src/toml.c` and `toml.h`
- `external/recomp-net/LICENSE`
- `runner/cd/ARES-LICENSE.txt` for builds containing the adapted Sega CD devices

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

## xxHash v0.8.3 license

Header copyright (C) 2012-2023 Yann Collet. Repository license text follows.

xxHash Library
Copyright (c) 2012-2021 Yann Collet
All rights reserved.

BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


## Optional portable PNG capture encoder

`external/shared-cores/recomp-presentation-core/include/retro_presentation/png.h`
uses the following license:

MIT License

Copyright (c) 2026 Alex Vanderveen / TechnicallyComputers
Copyright (c) 2026 Matthew Stanley

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

