# Releasing — compliance requirements (READ BEFORE PUBLISHING ANY BINARY)

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

1. **Right target.** The zip contains the NATIVE exe only — never
   `GenesisRecomp.exe`, never a `*_cosim.exe`.
2. **License.** The project license (`LICENSE.md`, PolyForm Noncommercial
   1.0.0) is inside the zip as `LICENSE`.
3. **Attribution.** `THIRD-PARTY-LICENSES.md` is in the zip (ymfm BSD-3,
   superzazu MIT, clowncommon ISC, SDL2 zlib, plus the launcher deps —
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

## History

Until 2026-06-10 every release statically linked clownmdemu/clownz80 and was
therefore an AGPL-3.0 combined work (see git history of this file for the
old requirements — past releases published under those terms stay AGPL).
The own-backend migration (engine `dev-genesisrecomp-runners` →
`declown-headers`) replaced every AGPL component in the release path and
then removed the last compiled AGPL headers.
