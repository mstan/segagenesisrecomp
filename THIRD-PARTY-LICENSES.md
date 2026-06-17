# Third-Party Components & Licenses

This project links and/or bundles third-party code. Their licenses are
**separate from** this project's own license and continue to apply.

> **clownmdemu is a development dependency — absent from production release
> builds.** clownmdemu and its CPU cores (clown68000, clownz80; all AGPL-3.0)
> are used only by the debug/oracle targets and the recompiler tool. The
> release binaries (Sonic 1/2/3) run the clean-room own backend and neither
> link any clownmdemu object **nor compile any clownmdemu header** — the
> native targets build with no `clownmdemu-core` include paths (enforced in
> each game repo's CMakeLists).

| Component | Role | Author | License | Source |
|---|---|---|---|---|
| **ymfm** (`runner/external/ymfm/`) | YM2612 FM synthesis — **in release builds** | Aaron Giles | BSD-3-Clause | <https://github.com/aaronsgiles/ymfm> |
| **superzazu/z80** (`runner/external/superzazu/`) | Z80 core (SMPS sound CPU), embedded by the own-backend machine — **in release builds** | Nicolas Allemand | MIT | <https://github.com/superzazu/z80> |
| **clowncommon** (`runner/external/clowncommon/`, vendored from clownmdemu-core) | Integer typedefs / small C helpers — **in release builds** | Clownacy | Permissive (ISC/0BSD-style) | <https://github.com/Clownacy/clowncommon> (unmodified) |
| **SDL2** (`runner/external/SDL2/`) | Windowing, input, audio output — **in release builds** | SDL community | zlib | <https://libsdl.org> |
| **RmlUi** (`runner/launcher/deps/RmlUi/`) | Pre-boot launcher UI (HTML/CSS-like GUI) — **in release builds** (native target only) | Michael Ragazzon & contributors | MIT | <https://github.com/mikke89/RmlUi> |
| **FreeType** (`runner/launcher/deps/freetype/`) | Font rasteriser for the launcher UI — **in release builds** (native target only) | The FreeType Project | FTL **or** GPLv2 (we use FTL) | <https://freetype.org> |
| **stb_image** (`runner/launcher/third_party/stb_image.h`) | PNG decode for launcher box/controller art — **in release builds** (native target only) | Sean Barrett | Public Domain (MIT alt) | <https://github.com/nothings/stb> |
| **Lato** (`runner/launcher/assets/fonts/`) | Launcher UI typeface — **in release builds** (native target only) | Łukasz Dziedzic | SIL Open Font License 1.1 | <https://www.latofonts.com> |
| **ShadowVerifier + color-science core** (`runner/audio/audio_shadow.{c,h}`, CIE core in `runner/video/color_lut.c`) | Verified-enhancement shadow self-check + present-time color LUT — **in release builds**, opt-in/default-off | Jrickey (algorithm); our C port | MIT OR Apache-2.0 | <https://github.com/JRickey/gba-recomp> (`crates/gba-core/src/shadow.rs`, `crates/screen/src/{color,profile,lut}.rs`), via the gbarecomp C++ / snesrecomp C ports, used with permission |
| **clownmdemu** (`clownmdemu-core/`) | Mega Drive hardware emulation. **Dev only** — the `_oracle` targets' ground-truth reference; not in any release binary | Clownacy | **AGPL-3.0** | upstream: <https://github.com/Clownacy/clownmdemu> · our fork: `mstan/clownmdemu` (private) |
| **clown68000** (`clownmdemu-core/libraries/clown68000/`) | 68000 interpreter. **Dev only** — oracle targets and the recompiler's `cycle_probe`; not in any release binary | Clownacy | **AGPL-3.0** | upstream: <https://github.com/Clownacy/clown68000> · our fork: `mstan/clown68000` (private) |
| **clownz80** (`clownmdemu-core/libraries/clownz80/`) | Z80 interpreter. **Dev only** — oracle targets; release builds run superzazu instead | Clownacy | **AGPL-3.0** | <https://github.com/Clownacy/clownz80> (upstream, unmodified) |

Full license texts ship with each component:
`runner/external/ymfm/LICENSE`, `runner/external/superzazu/LICENSE`,
`runner/external/clowncommon/LICENCE.txt`,
`runner/external/SDL2/SDL2-2.28.5/COPYING.txt`,
`runner/launcher/deps/RmlUi/LICENSE.txt`,
`runner/launcher/deps/freetype/LICENSE.TXT` (FTL: `docs/FTL.TXT`),
`clownmdemu-core/LICENCE.txt`, `clownmdemu-core/libraries/*/LICEN*.txt`.

The launcher dependencies (RmlUi MIT, FreeType FTL, stb_image public-domain,
Lato OFL-1.1) are all permissive and ship in the native release binaries. They
are linked **only into the native (shipped) target** — the dev-only `_oracle`
target does not build the launcher.

## Our changes to clownmdemu / clown68000

We maintain modifications (runner integration hooks: audio event-queue/cycle
stamps, scanline/interrupt path, hybrid pre-instruction hook, etc.). These live
**committed directly in the `mstan/clownmdemu` and `mstan/clown68000` forks** —
there is no build-time patch step. Because the upstream code is AGPL-3.0, our
modifications to it are also AGPL-3.0. This only creates obligations if a
binary containing that code is distributed — which release binaries do not.

## Compliance notes

- **Release binaries (Sonic 1/2/3 native)** contain no AGPL code and were
  compiled from translation units that include no AGPL headers. They are not
  AGPL combined works; they ship under the project license
  (PolyForm Noncommercial 1.0.0) with the permissive notices above.
- **Oracle (`*_oracle`) binaries and the recompiler (`GenesisRecomp`)**
  statically link AGPL code. They are dev tools; distributing either would
  trigger AGPL-3.0 obligations (license + complete corresponding source,
  including the private forks). Do not ship them — see `RELEASING.md`.
- The shipped binary does **not** contain the game ROM — users supply their
  own (`*.bin` is gitignored; the loader reads it at runtime). The recompiled
  C (`<game>_full.c`) compiled into the binary is, however, a machine
  translation of the ROM's code — the same derivative-work gray area every
  recomp/decomp operates in; our own license cannot grant rights to it.
- This is informational, not legal advice.
