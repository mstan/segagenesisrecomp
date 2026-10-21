# Owned-input Sonic CD build and portable package

The supported inputs are the US Model 1 v1.10 BIOS (128 KiB, SHA-256
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`)
and the qualified Sonic CD US disc (SGCD payload SHA-256
`4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1`).
The runner rejects other inputs. BIOS and media are supplied externally.

`tools/segacd/disc_image.py` converts supported raw BIN/CUE images; its
`inspect-cue` subcommand binds the canonical CUE and every referenced track to
content hashes. The portable launcher uses that identity for conversion caches.

## Generate from the owned inputs

Use this checkout and its pinned M68K/Z80 core submodules. Build the diagnostic
tools from `tests/segacd`, targeting `segacd_module_recompiler` and
`segacd_bios_floor_probe`. The floor probe is an explicit discovery tool;
the shipping `SonicCD` runner has strict native Main/Sub/Z80 execution.

The Z80 frontend is `SmsRecomp` from the separate
[smsggrecomp source](https://github.com/mstan/smsggrecomp), revision
`224d5bb2c150a2c295033d35dec629ef9ee42940`. Configure its `recompiler/`
directory and build target `SmsRecomp`; it is a standalone C11 tool with no
external target dependencies. `FLAT_STEP.md` in that checkout describes its
flat-step interface. The frontend executable is a build dependency, not a
component of the playtest package.

For example, use a fresh G: work directory and redirect process-local state
before configuring either tool:

```powershell
$cdWork = 'G:/SonicCD'
$cdCmake = 'C:/Program Files/CMake/bin/cmake.exe'
foreach ($name in @('TEMP','TMP','TMPDIR','APPDATA','LOCALAPPDATA',
                    'XDG_CACHE_HOME','XDG_DATA_HOME','XDG_STATE_HOME','XDG_CONFIG_HOME')) {
  $location = "$cdWork/host-state/$name"
  New-Item -ItemType Directory -Path $location -Force | Out-Null
  [Environment]::SetEnvironmentVariable($name, $location, 'Process')
}
& $cdCmake -S tests/segacd -B "$cdWork/tools"
& $cdCmake --build "$cdWork/tools" --config Release `
  --target segacd_module_recompiler segacd_bios_floor_probe --parallel 2
& $cdCmake -S G:/Tools/smsggrecomp/recompiler -B "$cdWork/z80-frontend"
& $cdCmake --build "$cdWork/z80-frontend" --config Release --target SmsRecomp --parallel 2

python tools/segacd/owned_native_generate.py `
  --recipe tools/segacd/sonic_cd_us_recipe.json.gz `
  --source-root . `
  --bios G:/OwnedInputs/segacd-us-1.10.bin `
  --disc G:/OwnedInputs/sonic-cd-us.sgcd `
  --generator "$cdWork/tools/Release/segacd_module_recompiler.exe" `
  --probe "$cdWork/tools/Release/segacd_bios_floor_probe.exe" `
  --z80-generator "$cdWork/z80-frontend/Release/SmsRecomp.exe" `
  --output-dir "$cdWork/catalogs" `
  --cmake 'C:/Program Files/CMake/bin/cmake.exe' --parallel 2
```

These executable paths assume a Visual Studio multi-configuration generator.
Single-configuration generators place executables directly in the build directory.

The recipe contains input identities, source mappings, guarded entry addresses
and deterministic capture procedures. It formats private empty BRAM through
the original BIOS, captures transient program states, and bootstraps native Z80
execution in two stages. Every required M68K guard is checked against its source
bytes, length and nominal cycles. Z80 captures feed guarded native variants at
all 16384 logical PCs, with terminal misses. The output includes the source-only
catalog manifest and CPU lists used below; no historical archives are imported.

`--resume` verifies input identities, root sets, capture provenance and generated
source hashes before reusing completed work. A mismatched or unfinished cached
step fails visibly. Generated images and capture data are private build outputs.
The compressed JSON recipe can be expanded with Python's standard `gzip` module
for inspection; it contains no copied instruction images.

## Build the generated source bundle

`tools/segacd/native_catalog_build.py` validates all generated C/header files
against the catalog manifest and CPU/prefix lists, checks the owned inputs,
and writes a CMake initial cache. With `--build`, the existing `SonicCD` target
compiles the catalogs, runtime, devices, disc reader and presentation from source.
Historical imported archives in the manifest are ignored.

After the generation command succeeds:

```powershell
python tools/segacd/native_catalog_build.py `
  --source-root . `
  --catalog-manifest G:/SonicCD/catalogs/catalog-manifest.json `
  --base-catalogs G:/SonicCD/catalogs/base-catalogs.txt `
  --append-overlays G:/SonicCD/catalogs/append-overlays.txt `
  --bios 'G:/OwnedInputs/segacd-us-1.10.bin' `
  --disc 'G:/OwnedInputs/sonic-cd-us.sgcd' `
  --build-dir G:/SonicCD/build `
  --sdl2-dir runner/external/SDL2/SDL2-2.28.5/cmake `
  --cmake 'C:/Program Files/CMake/bin/cmake.exe' --parallel 2 --build
```

Omit `--build` for validation/cache generation, or use `--configure` for configure
only. The provenance file records source/core revisions, input identities and
catalog hashes. Build process temporary/cache/data locations are beneath the
selected build directory. Keep generated data on F: or G:.

## Windows owner package

Use `tools/segacd/package_windows.py create` with `--exe`, `--sdl2-dll`,
`--licenses-dir` and a fresh `--destination`. Optional `--bios` and `--disc`
store existing paths as configuration; neither file is copied. An optional
8192-byte blank backup seed is used only when no save exists. An existing package
destination is refused, preserving its settings and saves.

Use the same tool's `validate --package PATH` command, or the package's
`SonicCD.cmd -ValidateOnly`, to check all recorded file sizes and hashes.
The included [portable instructions](../tools/segacd/README-portable-windows.md)
describe input selection, controls, recomp-ui, adaptive widescreen and data paths.
Owner launches use a normal window and normal audio. Automated QA invokes the
runner separately with muted output.
