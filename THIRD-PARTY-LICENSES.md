# Third-Party Components & Licenses

This project links and/or bundles third-party code. Their licenses are
**separate from** this project's own license and continue to apply. The most
significant fact below: **clownmdemu and its CPU cores are AGPLv3**, a strong
copyleft license — see "Compliance notes" at the end.

| Component | Role | Author | License | Source |
|---|---|---|---|---|
| **clownmdemu** (`clownmdemu-core/`) | Mega Drive hardware emulation: VDP, FM/PSG, memory bus, scheduler, cartridge SRAM — links into the **runner** | Clownacy | **AGPL-3.0** | upstream: <https://github.com/Clownacy/clownmdemu> · our fork: `mstan/clownmdemu` (private) |
| **clown68000** (`clownmdemu-core/libraries/clown68000/`) | 68000 interpreter. **Not in the shipped game** — `stub_clown68000.c` excludes it from the native link. Present only in the **oracle/debug** build and compiled into the **recompiler** (`GenesisRecomp`) for `cycle_probe` | Clownacy | **AGPL-3.0** | upstream: <https://github.com/Clownacy/clown68000> · our fork: `mstan/clown68000` (private) |
| **clownz80** (`clownmdemu-core/libraries/clownz80/`) | Z80 interpreter — runs the sound (SMPS) co-processor in every build | Clownacy | **AGPL-3.0** | <https://github.com/Clownacy/clownz80> (upstream, unmodified) |
| **clowncommon** (`clownmdemu-core/libraries/clowncommon/`) | Shared C helpers | Clownacy | Permissive (ISC/0BSD-style) | <https://github.com/Clownacy/clowncommon> (upstream, unmodified) |
| **SDL2** (`runner/external/SDL2/`) | Windowing, input, audio output | SDL community | zlib | <https://libsdl.org> |

Full license texts ship with each component:
`clownmdemu-core/LICENCE.txt`, `clownmdemu-core/libraries/*/LICEN*.txt`,
`runner/external/SDL2/SDL2-2.28.5/COPYING.txt`.

## Our changes to clownmdemu / clown68000

We maintain modifications (runner integration hooks: audio event-queue/cycle
stamps, scanline/interrupt path, hybrid pre-instruction hook, etc.). These live
**committed directly in the `mstan/clownmdemu` and `mstan/clown68000` forks** —
there is no build-time patch step. Because the upstream code is AGPL-3.0, **our
modifications to it are also AGPL-3.0**, and must be offered as source to anyone
we distribute binaries to (see below).

## Compliance notes (AGPL-3.0)

- AGPL obligations trigger on **distribution** of binaries and on **network
  interaction** (§13) — not on purely private local use.
- The **shipped game binary** statically links **clownmdemu-core (including
  clownz80)** — both AGPL, both load-bearing at runtime — so the binary is a
  combined work that must be conveyed under **AGPL-3.0** with **complete
  corresponding source**. It does **not** contain clown68000 (excluded by
  `stub_clown68000.c`), so stripping debug tooling does not remove the AGPL
  obligation: the emulator core itself is AGPL.
- The **recompiler tool** (`GenesisRecomp`) separately statically links
  clown68000 (AGPL). That only creates a distribution obligation if you ship
  the recompiler binary; building with it privately does not.
- A *private* fork does not satisfy the source requirement — at distribution
  the corresponding modified source must reach recipients (public repo or a
  source archive shipped alongside the binary).
- The shipped binary does **not** contain the game ROM — users supply their own
  (`*.bin` is gitignored; the loader reads it at runtime). The recompiled C
  (`<game>_full.c`) compiled into the binary is, however, a machine translation
  of the ROM's code — the same derivative-work gray area every recomp/decomp
  operates in; your own license cannot grant rights to it.
- AGPL §7/§10 forbid adding **further restrictions** (e.g. "noncommercial
  only") to a distributed combined work.
- This is informational, not legal advice.
