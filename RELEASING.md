# Releasing — compliance requirements (READ BEFORE PUBLISHING ANY BINARY)

This checklist describes cartridge-runner packages. For Sega CD, follow
[CD setup and packaging](docs/SEGA_CD_SETUP.md#package-a-standalone-release)
and the consuming game's release process, such as
[Sonic CD's Windows/AppImage packaging](https://github.com/mstan/SonicCDRecomp/blob/main/RELEASING.md).
CD packages require their own runtime notices and external owned BIOS/disc
inputs; Sonic CD's strict native target has no interpreter fallback.

**Release (native) binaries are AGPL-free.** They run the clean-room own
backend: recompiled 68K, own VDP/bus/machine scheduler, ymfm FM (BSD-3),
superzazu Z80 (MIT), clean-room SN76489, SDL2 (zlib), clowncommon helpers
(ISC, vendored). The native target also bundles the shared recomp-ui pre-boot
launcher UI — Dear ImGui (MIT), stb_image / stb_truetype (public domain),
tinyfiledialogs (zlib), Lato font (OFL-1.1), all permissive. As of 2026-07-27
there is no emulator core in the tree at all, so this is true by construction
rather than by enforcement.

Release binaries therefore ship under the project's own license
(**PolyForm Noncommercial 1.0.0**, `LICENSE.md`) plus the permissive
third-party notices — NOT under AGPL.

The retired clownmdemu oracle and cycle-probe targets exist only in Git
history. Current validation uses the clean-room interpreter, synthetic
harnesses, and recomp-vs-interpreter co-simulation.

## Hard checklist — ALL must be true before publishing a binary

1. **Right target.** A game ZIP contains the NATIVE game exe only, never
   `GenesisRecomp.exe` or a `*_cosim.exe`. The recompiler has its own separate
   tool-package contract below.
2. **License.** The project license (`LICENSE.md`, PolyForm Noncommercial
   1.0.0) is inside the zip as `LICENSE`.
3. **Attribution.** `THIRD-PARTY-LICENSES.md` is in the zip (ymfm BSD-3,
   superzazu MIT, clowncommon ISC, minicoro Unlicense/MIT-0 (LuaCoco MIT asm),
   SDL2 zlib, plus the launcher deps —
   Dear ImGui MIT, stb_image/stb_truetype public-domain, tinyfiledialogs zlib,
   Lato OFL-1.1 — full texts ship with the vendored components). The `assets/`
   folder (fonts/ + img/) ships next to the exe so the launcher can load.
4. **No ROM, no dumps, no junk.** The zip contains **no** `*.bin/*.gen/*.smd`
   (ROM), `ramdump*`, `*_save_*.bin` / `savestate*` / `*.srm` (saves),
   `*.log`, or `*.map`. Users bring their own ROM.
5. **No dev diagnostics.** The exe was built with the default (prod)
   configuration: `GEN_DEV_TRACE=OFF` and `SONIC_REVERSE_DEBUG=OFF` where the
   game repo offers the option.
6. **README** states: bring-your-own-ROM, the project license, and where the
   source lives.

## Do it mechanically — never zip a build folder by hand

The CMake copies the ROM (`sonic*.bin`) next to the exe, so zipping a build
folder leaks the ROM (this happened — `release-v0.3.0` shipped `sonic.bin`).
Always package with the allowlist tool, which **refuses** if a ROM/dump/junk
is present:

```
python segagenesisrecomp/tools/package_release.py \
    --exe       build/Release/<Game>.exe \
    --extra     build/Release/SDL2.dll \
    --asset-dir build/Release/assets \
    --license   LICENSE \
    --notices   segagenesisrecomp/THIRD-PARTY-LICENSES.md \
    --readme    release/README.txt \
    --out       <Game>-vX.Y.Z-win64.zip
```

`--asset-dir build/Release/assets` stages the recomp-ui launcher's runtime
assets (`assets/fonts/*.ttf`, `assets/img/*.tga`) into the zip preserving the
`assets/` top folder, so the exe finds them next to itself. The 3-mode
Sonic3AndKnucklesRecomp repo packages each exe separately; its per-mode box art
(`boxart-<mode>.tga`) all live under the one `assets/img/`, so the same
`--asset-dir` covers every mode.

## Quick pre-publish audit

- `git -C <repo> ls-files | grep -iE '\.(bin|gen|smd)$'` → must be empty (no ROM tracked).
- `git -C <repo> log --all --diff-filter=A --name-only --pretty=format: | grep -iE '\.(bin|gen|smd)$'` → empty (no ROM ever committed).
- Confirm the exe is the native target (no `_cosim` suffix), and confirm the
  repository's submodule list contains only `m68k-recomp-core`,
  `z80-recomp-core`, and `recomp-net`. If a link map is available, verify the
  native executable contains no clownmdemu/clown68000 objects.
- Open the final zip and confirm it contains **only**: the exe, `SDL2.dll`,
  `README`, `LICENSE` (PolyForm Noncommercial), `THIRD-PARTY-LICENSES.md`, and
  the launcher's `assets/` folder (`assets/fonts/*.ttf`, `assets/img/*.tga`).

## Standalone Windows recompiler tool

The current `GenesisRecomp` target uses the clean-room timing model and links
no emulator core; the historical AGPL restriction no longer applies (see
`LICENSING.md`). Build `recompiler/CMakeLists.txt` in Release from the tagged
engine source with the pinned `m68k-recomp-core` initialized. Package the
executable separately using `tools/package_release.py --kind recompiler`.
Include its actual non-OS DLL closure, `LICENSE.md` as `LICENSE`,
`THIRD-PARTY-LICENSES.md`, a tool-specific README and exact source/build metadata.
Do not put game runners, ROMs, generated game code or launcher assets into this
tool archive. Verify `GenesisRecomp.exe --help` and regenerate a qualified
private cartridge fixture with the extracted tool before publication.

## Historical licensing

Until 2026-06-10 every release statically linked clownmdemu/clownz80 and was
therefore an AGPL-3.0 combined work (see git history of this file for the
old requirements — past releases published under those terms stay AGPL).
The own-backend migration (engine `dev-genesisrecomp-runners` →
`declown-headers`) replaced every AGPL component in the release path and
then removed the last compiled AGPL headers.
