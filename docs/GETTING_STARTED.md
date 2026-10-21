# Start a Genesis game integration

This guide is for a developer creating a port with the framework. The tools
are ordinary command-line programs; no AI assistant is required. To play an
existing port, choose it from the [game list](../README.md#games) and follow
that game's release instructions.

This page covers the cartridge pipeline. For Sega CD / Mega-CD, start with
[Set up a Sega CD recomp](SEGA_CD_SETUP.md): it covers the released Sonic CD
reference project, owned BIOS/disc inputs, Main/Sub 68000 and Z80 catalogs,
and the work required to integrate another CD title. Sega CD's released native
runner has a separate build and stops on missing generated instructions.

## 1. Set up one toolchain

For the Windows x64 path, install Git for Windows and Visual Studio 2022
with **Desktop development with C++** and its CMake tools. Open **Developer
PowerShell for VS 2022**, and check `git --version` and `cmake --version`.

Keep the workspace, generated output and temporary files on your development
drive. Replace the paths below with yours:

```powershell
New-Item -ItemType Directory -Force D:\GenesisDev\Temp | Out-Null
$env:TEMP = 'D:\GenesisDev\Temp'
$env:TMP = $env:TEMP
Set-Location D:\GenesisDev
git clone --recurse-submodules https://github.com/mstan/segagenesisrecomp.git
Set-Location segagenesisrecomp
cmake -S recompiler -B build/recompiler -G 'Visual Studio 17 2022' -A x64
cmake --build build/recompiler --config Release --parallel
```

The compiler is `build\recompiler\Release\GenesisRecomp.exe`.

## 2. Start with a separate game project

Use your own unmodified cartridge dump. Check whether a port already exists
before creating another. Keep game-specific configuration, source and assets
in that game's repository, with the framework pinned as a submodule. The
legacy game directories inside this framework are migration history rather
than the layout to copy for new projects.

Use a current small game project as a build-layout reference. Your new
`game.toml` must describe your game's ROM identity, discovered routines,
dispatch and bank/data behavior. A Sonic configuration does not automatically
describe another 68000 game. The
[disassembly guide](DISASSEMBLIES.md) explains how verified labels and
annotations feed that analysis.

## 3. Generate and integrate the host

From your game folder, run the compiler you built, pointing it at your ROM
and configuration:

```powershell
& D:\GenesisDev\segagenesisrecomp\build\recompiler\Release\GenesisRecomp.exe `
  'D:\ROMs\My Game.bin' --game game.toml --output-dir generated
```

Generated C is one part of the port. The consuming game's CMake and adapter
must connect it to the runner, define frame execution, provide ROM checking
and handle the title's runtime integration. Current game repositories show
the complete builds. Keep generated files in ignored output directories;
fix analysis or runtime behavior instead of hand-editing emitted functions.

## 4. Validate the stock game first

Choose one short route: cold boot, title selection, first gameplay scene.
Compare input, scenery, collision, scrolling, audio and transitions against
the original game in a trusted emulator. Record passed, failed and untested
checks. A title screen is not a campaign validation.

Keep the interpreter fallback available for unresolved targets while you
establish correct behavior. Read the [framework test instructions](../README.md#running-framework-tests)
and current game-specific test scripts for repeatable checks.

Add widescreen, party play or imported campaigns only after that baseline.
Each needs its own visibility, state and compatibility rules. Imported ROM
content requires explicit converters and verified source identities; a
different ROM filename is not a supported content variant. The player's
ROMs, converted packs, saves and generated assets stay local.

For multiplayer, use the [netplay contract](NETPLAY.md). For shipping, read
[RELEASING.md](../RELEASING.md) and include the required third-party notices.
The framework is beta, while each game's feature and platform coverage must
be stated separately.
