# segagenesisrecomp/runner/ — DEAD scaffolding

This directory is **not built** by the current Sonic 1 or Sonic 2 targets.

## What it is

A scaffolded "submodule-internal runner" shape, exposed via
`runner.cmake`'s `GENESISRECOMP_RUNNER_SOURCES` variable. The intent was
that game projects would `include(.../runner.cmake)` and pick up a clean
shared runner. That migration never landed.

## What's actually built

Both Sonic 1 (`SonicTheHedgehogRecomp/CMakeLists.txt`) and Sonic 2
(`SonicTheHedgehog2Recomp/CMakeLists.txt`) consume the production runner
at `SonicTheHedgehogRecomp/runner/` directly via
`RUNNER_ROOT = .../SonicTheHedgehogRecomp/runner`. That is the canonical
runner. See `../CLAUDE.md` for the full topology.

## Why these files still exist

- `runner/include/genesis_runtime.h` is referenced by Sonic 2's CMakeLists
  via `${RECOMP_ROOT}/runner/include`, so the headers are still part of
  the build path. Some include paths transit this directory even though
  no `.c` from `src/` is compiled.
- `runner/src/runtime.c` contains Sonic-1-bring-up-era diagnostic
  `fprintf` calls that violate `../PRINCIPLES.md` #18 (no printf telemetry
  in hot paths). It carries a DEAD CODE banner.
- `runner.cmake` documents the intended future shape.

## What to do

- **Don't extend `runner/src/`.** Production runner work goes in
  `SonicTheHedgehogRecomp/runner/`.
- **Don't reference `runner/src/runtime.c` as a pattern.** Real
  m68k_read/m68k_write live in
  `SonicTheHedgehogRecomp/runner/glue.c:1007/1108/1134`.
- **Wave 4 will decide** whether to delete this skeleton entirely or
  promote it into the canonical shared-runner location after the
  production runner relocates here.

See `../humming-wibbling-hammock.md`-style plan docs (or
`../CLAUDE.md`) for context.
