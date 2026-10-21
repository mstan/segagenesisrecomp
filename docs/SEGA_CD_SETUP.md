# Set up a Sega CD recomp

Sega CD / Mega-CD support lives on
[`feature/segacd-backend`](https://github.com/mstan/segagenesisrecomp/tree/feature/segacd-backend).
The reusable NTSC machine and native CPU integration have a released reference
consumer: [SonicCDRecomp 0.0.1](https://github.com/mstan/SonicCDRecomp/releases/tag/v0.0.1).
The backend has not been merged into engine master. This guide covers the
released game first, then the integration work for another CD title.

## What is supported

| Component | Current implementation and scope |
|---|---|
| Original CPU programs | Generated native Main 68000, Sub 68000 and Z80 code; missing instruction variants stop execution. The shipping Sonic CD runner has no interpreter fallback. |
| Shared hardware | Main/Sub buses, PRG/Word RAM ownership, interrupts, CDD/CDC, raw-sector delivery, DMA, stamp graphics and RF5C164 PCM. |
| Audio | Clocked FM/PSG, PCM and CDDA mixed into host audio; timestamped queues and fault counters. |
| Persistence | Original 8 KiB backup RAM, validated loading and atomic host-file replacement. |
| Reference game | Original Sonic CD US, including stage/time travel, bosses, special stages, ending and credits; see its [qualified coverage](https://github.com/mstan/SonicCDRecomp/blob/main/docs/VALIDATION.md). |
| Presentation | Sonic CD's recomp-ui and adaptive normal-stage renderer; special stages retain their centered original projection. These presentation rules are game-specific. |
| Released platforms | Windows 10/11 x64 and Linux x86_64 with glibc 2.39+; other platforms and input revisions need separate qualification. |

The runtime is reusable; the current Sonic CD recipe, input checks, program
registration and renderer are specific to that game. Another disc needs its
own integration and validation. A successful BIOS boot alone does not establish
support for that title.

## Play the released Sonic CD build

Download the [Windows ZIP or Linux AppImage](https://github.com/mstan/SonicCDRecomp/releases/tag/v0.0.1).
On Windows, extract the ZIP and run `SonicCD.cmd`. On Linux, make the AppImage
executable and run it; Python 3 and Zenity/KDialog provide input selection.
Explicit `--bios` and `--disc` arguments also work on Linux. Use
`--appimage-extract-and-run` when FUSE is unavailable.

Users supply their own **US Sonic CD disc** and **US Model 1 v1.10 BIOS**:

| Input | Qualified 0.0.1 identity |
|---|---|
| BIOS | 131,072 bytes; SHA-256 `807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`; CRC32 `c6d10268` |
| Converted disc | SGCD payload SHA-256 `4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1` |

Extract archives first. Select an existing `.sgcd` image or a `.cue` with its
referenced raw `.bin` tracks. The supported mixed-mode disc contains data and
audio tracks; an ISO-only dump does not contain that full layout. The converter
supports BINARY `MODE1/2352` and AUDIO tracks. Read the game's
[input details](https://github.com/mstan/SonicCDRecomp/blob/main/docs/INPUTS.md)
for the exact format and revision contract.

The launcher remembers input paths and keeps settings, converted media and
backup RAM under `userdata` beside the package. It initializes an empty save
only when no save exists. Startup is windowed with normal audio; F1/Esc opens
recomp-ui. See [usage and controls](https://github.com/mstan/SonicCDRecomp/blob/main/docs/USAGE.md).
No BIOS, original disc or personal save is distributed.

## Build Sonic CD from owned inputs

Use the [game repository](https://github.com/mstan/SonicCDRecomp) as the
working reference. Its wrapper builds the required frontends, converts a CUE
when needed, generates guarded code catalogs and compiles the native game.
It pins the engine and Z80 generation frontend independently.

Requirements: Git, Python 3.10+, CMake 3.21+, a C11/C++17 compiler and SDL2
development files. Windows uses a Visual Studio 2022 developer shell and the
engine's SDL2 SDK. Linux needs a compiler, Ninja, SDL2 development files and
pkg-config; the qualified release toolchain was GCC 13.3 on Ubuntu 24.04.

Clone into your development workspace and initialize the required submodules:

```sh
git clone https://github.com/mstan/SonicCDRecomp.git
cd SonicCDRecomp
git submodule update --init external/segagenesisrecomp external/z80-frontend
git -C external/segagenesisrecomp submodule update --init external/m68k-recomp-core external/z80-recomp-core
```

The 0.0.1 reference pins are:

| Dependency | Commit |
|---|---|
| Sega CD engine | `c8bcab94babc217955c8aab195d7de8d4b532331` |
| M68K core | `7233852a6e45d8779b9dae8b29afd6b94d5231dc` |
| Z80 core | `4dfc5fc29fd67d23714126348e678e45952e9462` |
| SmsRecomp Z80 frontend | `224d5bb2c150a2c295033d35dec629ef9ee42940` |

Use the pins in your selected game checkout; record them when validating a
different revision. The unrelated cartridge/disassembly/netplay submodules
are not required by this CD target.

### Windows

In Developer PowerShell, choose a fresh build directory on a development
drive. The generation tools reject build output on C:. This example redirects
temporary/cache/data output before invoking tools:

```powershell
$cdWork = 'G:/Builds/SonicCD'
foreach ($name in @('TEMP','TMP','TMPDIR','APPDATA','LOCALAPPDATA',
                    'XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME',
                    'XDG_STATE_HOME','PYTHONPYCACHEPREFIX','CCACHE_DIR')) {
  $location = "$cdWork/host-state/$name"
  New-Item -ItemType Directory -Force -Path $location | Out-Null
  [Environment]::SetEnvironmentVariable($name, $location, 'Process')
}
python tools/build.py --bios 'G:/Owned/segacd-us-1.10.bin' `
  --disc 'G:/Owned/Sonic CD.cue' --work-dir $cdWork --jobs 4
```

The executable is normally `G:/Builds/SonicCD/game/runtime/Release/SonicCD.exe`.
With a single-configuration generator, omit the `Release` directory.

### Linux

Choose a build filesystem with enough space for generated sources, object
files and archives. Native Linux storage avoids the high metadata cost of
WSL Windows-mounted paths. For this project's F:/G: development setup, verify
that WSL's backing disk and all scratch output are off C: before building;
a project path on G: does not redirect a C:-backed `/tmp` or home directory.

```sh
cdWork="$PWD/build-native"
mkdir -p "$cdWork/host-state/tmp" "$cdWork/host-state/python-cache"
export TMPDIR="$cdWork/host-state/tmp" TEMP="$cdWork/host-state/tmp" TMP="$cdWork/host-state/tmp"
export PYTHONPYCACHEPREFIX="$cdWork/host-state/python-cache"
python3 tools/build.py --bios '/path/to/segacd-us-1.10.bin' \
  --disc '/path/to/Sonic CD.cue' --work-dir "$cdWork" --generator Ninja --jobs 4
```

The executable is `build-native/game/runtime/SonicCD`. Both platforms produce
`WORK/build-result.json`; the game build directory also contains
`source-build-provenance.json` and `segacd-native-initial-cache.cmake`.
Generation produces `catalog-manifest.json`, `base-catalogs.txt` and
`append-overlays.txt` under `WORK/catalogs`.

The full reference generation contains 333 catalogs and 11,290 C/header files.
The recipe records owned source identities, roots, transient instruction
captures and expected guards. Diagnostic floor probes collect offline evidence;
the shipping Main/Sub/Z80 runner remains strict native. Generated programs,
captures and source catalogs stay in private ignored build directories.

For lower-level frontend, recipe and CMake commands, see the engine's
[owned-input build guide](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/SONIC_CD_NATIVE_BUILD.md)
and the game's [build guide](https://github.com/mstan/SonicCDRecomp/blob/main/docs/BUILDING.md).

### Reuse completed work

Add `--resume` to the same build command to verify and reuse completed
generation. `--catalogs /path/to/catalogs` selects an existing complete source
bundle; its hashes and input identities are still checked. Catalog manifests
contain filesystem paths, so copying a Windows bundle to Linux requires proper
path/provenance handling rather than assuming it is portable as-is.

For runtime-only changes after configuration, use the existing build:

```sh
cmake --build /path/to/WORK/game --config Release --target SonicCD --parallel 4
```

This retains unchanged generated objects. Regenerate only when input identity,
catalog discovery, recipe or frontend changes require it. Fix generation inputs
or runtime behavior rather than hand-editing emitted C or suppressing misses.

## Start a different Sega CD title

Keep the game-specific project separate from the engine and pin the engine,
CPU cores and generation tools. The existing Sonic CD target is currently
declared under `tests/segacd`; its game repository wraps that target. It is a
working reference integration, not a universal disc-to-game project generator.

1. **Qualify inputs and inventory programs.** Record the owned BIOS and disc
   hashes. Convert supported CUE/BIN media with `tools/segacd/disc_image.py`.
   For compatible ISO9660 media, `sgcd_owned_extract.py` records program files,
   hashes and disc extents. Derive actual CPU/load addresses separately;
   filesystem offsets are not execution addresses.
2. **Describe executable states.** Inventory BIOS/boot code, Main/Sub program
   overlays, interrupt entries, dynamically copied or modified code and Z80
   sound-bank variants. Create the title's source mappings, roots and capture
   recipe. The Sonic CD recipe and its validator's BIOS/disc/prefix checks
   are specific to Sonic CD and must not be relabeled for another game.
3. **Generate and register native catalogs.** Reuse the module frontend and
   native CPU interfaces. Preserve original instruction-byte/cycle guards and
   qualify transient variants from their actual writers/captures. Diagnostic
   interpretation may aid offline discovery; the shipped title must stop on
   missing executed code rather than attaching a silent interpreter or stub.
4. **Connect the native machine.** Reuse `runner/cd/machine.*`,
   `segacd_coordinator.*`, `z80_native.*` and the device/audio/backup interfaces.
   Supply the title's program registration, input validation and launch/build
   target in its game repository. Keep game addresses, stage logic and any
   renderer/culling enhancements game-owned; Sonic CD's renderer is not a
   generic renderer for other CD games.
5. **Validate original behavior.** Start with boot, controls, first gameplay,
   audio and a real save/restart boundary. Use stage selection, teleports and
   protection for broad coverage; let original completion/result/save code
   execute. Record bypassed prerequisites, misses, audio faults and
   passed/failed/unverified checks. Compare final visuals independently before
   release, then qualify each enhancement and supported platform separately.

Current backend accuracy qualifications are instruction-boundary rather than
electrical bus/T-state timing, analog DAC/display conversion, and marginal
media/drive-error behavior beyond the qualified image. These are separate from
gameplay acceptance and do not permit missing executed code to be skipped.

## Synthetic checks

In a checkout of `feature/segacd-backend`, initialize its two CPU submodules:

```sh
git clone --branch feature/segacd-backend https://github.com/mstan/segagenesisrecomp.git
cd segagenesisrecomp
git submodule update --init external/m68k-recomp-core external/z80-recomp-core
cmake -S tests/segacd -B build/segacd-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/segacd-tests --config Debug --target segacd_test_suite --parallel 4
ctest --test-dir build/segacd-tests -C Debug --output-on-failure
```

Use the same process-local scratch/cache redirection as above. These checks
need no owned disc or BIOS. They validate reusable contracts; game coverage
and owned-input builds are recorded separately in the
[native acceptance ledger](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/SONIC_CD_NATIVE_ACCEPTANCE.md).
Mute unattended launches while retaining emulated audio for capture/analysis.
Owner playtests use a normal window with normal audio.

## Package a standalone release

The consuming game owns its launcher, supported input identities, build
manifest, notices and public artifacts. Follow
[Sonic CD's packaging process](https://github.com/mstan/SonicCDRecomp/blob/main/RELEASING.md)
for Windows allowlisted staging and Linux AppDir/AppImage tooling. Include
the required runtime libraries and license texts; verify each platform's
actual launcher and preserve existing settings and backup RAM.

Keep BIOS/discs, extracted programs, generated catalogs, dumps, diagnostics,
personal saves and machine-specific input paths out of public packages.
A qualified empty BRAM seed may initialize a new profile only. Record source,
core and executable identities, publish checksums, and verify downloads.

## Backend references

- [Current backend architecture](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/SEGA_CD_BACKEND.md)
- [Media format and conversion](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/SEGA_CD_MEDIA.md)
- [Permissive device implementation reuse and attribution](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/SEGA_CD_REUSE.md)
- [Original-program module frontend](https://github.com/mstan/segagenesisrecomp/blob/feature/segacd-backend/docs/M68K_STEP_MODULES.md)
