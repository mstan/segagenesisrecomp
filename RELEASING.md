# Releasing — compliance requirements (READ BEFORE PUBLISHING ANY BINARY)

Every shipped binary in this project statically links **clownmdemu** and
**clownz80** (AGPL-3.0), plus the recompiled game code. A distributed binary is
therefore an **AGPL-3.0 combined work**. There is no way to ship the binary
today without complying with AGPL — see `THIRD-PARTY-LICENSES.md` and
`FEATURES.md`.

> To ship under your own (noncommercial/commercial) terms instead, you must
> FIRST remove the AGPL dependency — replace clownmdemu/clown68000 with a
> non-AGPL core, or obtain alternate licensing from Clownacy. Until then,
> **every binary release is AGPL-3.0.**

## Hard checklist — ALL must be true before publishing a binary

1. **License.** The release is AGPL-3.0 and the **AGPL-3.0 license text is
   inside the zip** (`LICENSE`).
2. **Corresponding source.** The complete source that builds this binary is
   publicly available at the release tag — **including the `mstan/clownmdemu`
   and `mstan/clown68000` forks (these must be PUBLIC)** and the generated C.
   The release notes link to it.
3. **Attribution.** `THIRD-PARTY-LICENSES.md` is in the zip.
4. **No ROM, no dumps, no junk.** The zip contains **no** `*.bin/*.gen/*.smd`
   (ROM), `ramdump*`, `*_save_*.bin` / `savestate*` / `*.srm` (saves), `*.log`,
   or `*.map`. Users bring their own ROM.
5. **README** states: bring-your-own-ROM, the AGPL-3.0 license, and the source
   link.
6. **Network builds (§13).** If a build is ever playable over a network / in a
   browser, remote users must also be offered the corresponding source.

## Do it mechanically — never zip a build folder by hand

The CMake copies the ROM (`sonic*.bin`) next to the exe, so zipping a build
folder leaks the ROM (this happened — `release-v0.3.0` shipped `sonic.bin`).
Always package with the allowlist tool, which **refuses** if a ROM/dump/junk is
present:

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
- `gh repo view mstan/clownmdemu --json visibility` and `mstan/clown68000` → must be **PUBLIC** (AGPL corresponding-source requirement).
- Open the final zip and confirm it contains **only**: the exe, `SDL2.dll`,
  `README`, `LICENSE` (AGPL), `THIRD-PARTY-LICENSES.md`.
