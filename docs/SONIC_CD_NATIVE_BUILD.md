# Owned-input Sonic CD build and portable package

The supported inputs are the US Model 1 v1.10 BIOS (128 KiB, SHA-256
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`)
and the qualified Sonic CD US disc (SGCD payload SHA-256
`4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1`).
The runner rejects other inputs. BIOS and media are supplied externally.

`tools/segacd/disc_image.py` converts supported raw BIN/CUE images; its
`inspect-cue` subcommand binds the canonical CUE and every referenced track to
content hashes. The portable launcher uses that identity for conversion caches.

## Source build from a generated catalog bundle

`tools/segacd/native_catalog_build.py` validates all generated C/header files
against the catalog manifest and CPU/prefix lists, checks the owned inputs,
and writes a CMake initial cache. With `--build`, the existing `SonicCD` target
compiles the catalogs, runtime, devices, disc reader and presentation from source.
Historical imported archives in the manifest are ignored.

This command consumes a locally generated catalog bundle. Complete BIOS/disc to
catalog discovery and generation is still being qualified; this command alone
does not establish that reproduction step. See the acceptance ledger.

Example for a prepared bundle and a Visual Studio CMake installation:

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
