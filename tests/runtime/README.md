# Runtime Validation Harnesses

These harnesses are opt-in checks for runtime changes that need focused
coverage outside the normal ROM-independent CTest set.

## SDL Texture Rectangle Upload

Build and run with the bundled Windows SDL2 package:

```powershell
New-Item -ItemType Directory -Force -Path 'F:\Projects\segagenesisrecomp\segagenesisrecomp\build\runtime-validation' | Out-Null
& 'C:\msys64\mingw64\bin\gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror `
  -I 'F:\Projects\segagenesisrecomp\segagenesisrecomp\runner\external\SDL2\SDL2-2.28.5\include' `
  'F:\Projects\segagenesisrecomp\segagenesisrecomp\tests\runtime\sdl_texture_rect_test.c' `
  -L 'F:\Projects\segagenesisrecomp\segagenesisrecomp\runner\external\SDL2\SDL2-2.28.5\lib\x64' `
  -lSDL2 `
  -o 'F:\Projects\segagenesisrecomp\segagenesisrecomp\build\runtime-validation\sdl_texture_rect_test.exe'

$oldPath = $env:Path
$env:Path = 'F:\Projects\segagenesisrecomp\segagenesisrecomp\runner\external\SDL2\SDL2-2.28.5\lib\x64;' + $oldPath
& 'F:\Projects\segagenesisrecomp\segagenesisrecomp\build\runtime-validation\sdl_texture_rect_test.exe'
$env:Path = $oldPath
```

Set `SDL_RECT_TEST_RAW_LINEAR_RECT=1` to reproduce the Direct3D linear-filter
edge-texel failure for raw active-rectangle uploads. Set
`SDL_RECT_TEST_TIMING=1` for the optional upload-call timing sanity check.

## VDP Renderer Differential

Build and run with the helper script:

```powershell
python 'F:\Projects\segagenesisrecomp\segagenesisrecomp\tests\runtime\run_vdp_render_diff.py' `
  --baseline-ref 997bd1f1c754f38dd598318661b2a52553d33cc0
```

The script extracts `runner/video/genesis_vdp.c` from the baseline ref as raw
Git bytes, compiles both baseline and candidate with `-DNDEBUG`, symbol-prefixes
their public VDP symbols, links them into one executable under
`build/runtime-validation`, and runs the comparison. Use `--baseline-source`
when comparing against a specific saved source file.

## Sonic 1 Cross-State Plan

Run the reproducible matrix helper:

```powershell
python 'F:\Projects\segagenesisrecomp\segagenesisrecomp\tests\runtime\run_sonic1_cross_state.py'
```

The helper copies the baseline and candidate executables into isolated
timestamped directories under `build/runtime-validation`, writes local
`settings.ini` files, creates baseline and candidate save states, then verifies:

- baseline loads baseline-made state
- candidate loads baseline-made state
- baseline loads candidate-made state
- candidate loads candidate-made state

It requires successful save/load log markers, post-load RAM dumps, screenshots,
RAM movement after post-load input, and matching RAM/screenshot hashes for each
baseline-vs-candidate load pair. It also runs one candidate load with
`linear_filter = 1` unless `--skip-linear` is passed, and one candidate load
with `linear_filter = 0` plus `SDL_RENDER_SCALE_QUALITY=1` unless
`--skip-env-quality-override` is passed.

Pass `--out-dir <fresh-path>` to choose an artifact directory. The helper
refuses to reuse an existing directory so prior validation outputs are never
deleted.
