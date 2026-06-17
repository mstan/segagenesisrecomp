# Releasing — compliance requirements (READ BEFORE PUBLISHING ANY BINARY)

**Release (native) binaries are AGPL-free.** They run the clean-room own
backend: recompiled 68K, own VDP/bus/machine scheduler, ymfm FM (BSD-3),
superzazu Z80 (MIT), clean-room SN76489, SDL2 (zlib), clowncommon helpers
(ISC, vendored). The native target also bundles the pre-boot launcher UI —
RmlUi (MIT), FreeType (FTL), stb_image (public domain), Lato font (OFL-1.1),
all permissive. They link **zero** clownmdemu/clown68000/clownz80 objects
AND compile **zero** AGPL headers — the native targets build with no
`clownmdemu-core` include paths at all (enforced in each game repo's
CMakeLists; any reintroduced AGPL include is a compile error).

Release binaries therefore ship under the project's own license
(**PolyForm Noncommercial 1.0.0**, `LICENSE.md`) plus the permissive
third-party notices — NOT under AGPL.

> AGPL still exists in this repository, in two dev-only places that must
> NEVER be shipped:
>
> 1. **The `_oracle` targets** statically link clownmdemu-core (AGPL) as the
>    parity/ground-truth reference. Distributing an oracle binary would make
>    it an AGPL combined work with corresponding-source obligations. Don't.
> 2. **The recompiler tool** (`GenesisRecomp.exe`) statically links
>    clown68000 for `cycle_probe`. Same rule: it is a build tool, not a
>    release artifact.

## Hard checklist — ALL must be true before publishing a binary

1. **Right target.** The zip contains the NATIVE exe only — never an
   `*_oracle.exe`, never `GenesisRecomp.exe`.
2. **License.** The project license (`LICENSE.md`, PolyForm Noncommercial
   1.0.0) is inside the zip as `LICENSE`.
3. **Attribution.** `THIRD-PARTY-LICENSES.md` is in the zip (ymfm BSD-3,
   superzazu MIT, clowncommon ISC, SDL2 zlib, plus the launcher deps —
   RmlUi MIT, FreeType FTL, stb_image public-domain, Lato OFL-1.1 — full
   texts ship with the vendored components). The `launcher/` assets folder
   (launcher.rml + fonts/ + img/) ships next to the exe so the GUI can load.
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
    --exe     build/Release/<Game>.exe \
    --extra   build/Release/SDL2.dll \
    --license LICENSE \
    --notices segagenesisrecomp/THIRD-PARTY-LICENSES.md \
    --readme  release/README.txt \
    --out     <Game>-vX.Y.Z-win64.zip
```

## Quick pre-publish audit

- `git -C <repo> ls-files | grep -iE '\.(bin|gen|smd)$'` → must be empty (no ROM tracked).
- `git -C <repo> log --all --diff-filter=A --name-only --pretty=format: | grep -iE '\.(bin|gen|smd)$'` → empty (no ROM ever committed).
- Confirm the exe is the native target (window title / no `_oracle` suffix)
  and was built from a tree where the native include lists contain no
  `clownmdemu-core` paths (`grep clownmdemu-core CMakeLists.txt` → only the
  `_ORACLE` include list and the oracle-only `add_subdirectory`).
- Open the final zip and confirm it contains **only**: the exe, `SDL2.dll`,
  `README`, `LICENSE` (PolyForm Noncommercial), `THIRD-PARTY-LICENSES.md`.

## History

Until 2026-06-10 every release statically linked clownmdemu/clownz80 and was
therefore an AGPL-3.0 combined work (see git history of this file for the
old requirements — past releases published under those terms stay AGPL).
The own-backend migration (engine `dev-genesisrecomp-runners` →
`declown-headers`) replaced every AGPL component in the release path and
then removed the last compiled AGPL headers.
