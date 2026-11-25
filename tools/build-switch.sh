#!/usr/bin/env bash
# Build native Switch NROs using the normal game projects and original ROMs.
# Run in devkitPro MSYS2 on Windows, or a devkitPro environment on Linux/macOS.
# Usage: bash tools/build-switch.sh [--game all|sonic1|sonic2|sonic3|sonic3k|sandk|sonic3-variants]
#   [--games-root DIR] [--build-dir DIR] [--out DIR] [--jobs N]
#   [--host-recompiler FILE] [--with-roms]
#   [--engine DIR] [--sonic1-repo DIR] [--sonic2-repo DIR] [--sonic3-repo DIR]
#   [--sonic2-engine DIR] [--host-step-recompiler FILE]
#   [--rom FILE] (single-game builds only)
# Or: bash tools/build-switch.sh --profile FILE.json [overrides...]
# --with-roms is for private owner packages only. Public packages omit ROMs.
set -euo pipefail
if [ "${1:-}" = --profile ]; then
    args_file="$(mktemp)"
    trap 'rm -f "$args_file"' EXIT
    python3 -c 'import json,sys; a=json.load(open(sys.argv[1]))["args"]; assert isinstance(a,list) and all(isinstance(x,str) and "\0" not in x for x in a); sys.stdout.buffer.write(b"\0".join(x.encode() for x in a)+b"\0")' "$2" > "$args_file"
    mapfile -d '' -t profile_args < "$args_file"
    rm -f "$args_file"; trap - EXIT
    shift 2
    exec bash "$0" "${profile_args[@]}" "$@"
fi
engine="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
script_engine="$engine"
games_root="$(dirname "$engine")"
build="$engine/build/switch"
out="$engine/release-switch"
game=all jobs=2 host_recompiler="" with_roms=0
sonic1_repo="" sonic2_repo="" sonic3_repo=""
sonic2_engine="" host_step_recompiler=""
rom_override=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --game) game="$2"; shift 2;;
        --engine) engine="$2"; shift 2;;
        --sonic2-engine) sonic2_engine="$2"; shift 2;;
        --host-step-recompiler) host_step_recompiler="$2"; shift 2;;
        --rom) rom_override="$2"; shift 2;;
        --sonic1-repo) sonic1_repo="$2"; shift 2;;
        --sonic2-repo) sonic2_repo="$2"; shift 2;;
        --sonic3-repo) sonic3_repo="$2"; shift 2;;
        --games-root) games_root="$2"; shift 2;;
        --build-dir) build="$2"; shift 2;;
        --out) out="$2"; shift 2;;
        --jobs) jobs="$2"; shift 2;;
        --host-recompiler) host_recompiler="$2"; shift 2;;
        --with-roms) with_roms=1; shift;;
        -h|--help) sed -n '2,8p' "$0"; exit 0;;
        *) echo "Unknown argument: $1" >&2; exit 2;;
    esac
done
case "$game" in all) games=(sonic1 sonic2 sonic3 sonic3k sandk);;
    sonic3-variants) games=(sonic3 sonic3k sandk);;
    sonic1|sonic2|sonic3|sonic3k|sandk) games=("$game");;
    *) echo "Invalid --game: $game" >&2; exit 2;; esac
if [ -n "$rom_override" ] && [ "${#games[@]}" -ne 1 ]; then
    echo "--rom requires a single --game" >&2; exit 2
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid --jobs" >&2; exit 2; }
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
cross_host_flags=()
case "$(uname -s)" in MSYS*|MINGW*|CYGWIN*)
    export DEVKITPRO="$(cygpath -u "$DEVKITPRO")"
    export PATH="/usr/bin:/c/msys64/mingw64/bin:$PATH"
    cross_host_flags=(-DGENESIS_HOST_WINDOWS_PATHS=ON);;
esac
export PATH="$DEVKITPRO/portlibs/switch/bin:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
cmake_bin=cmake
host_cmake=cmake
generator_flags=()
toolchain="$DEVKITPRO/cmake/Switch.cmake"
case "$(uname -s)" in MSYS*|MINGW*|CYGWIN*)
    # SDK MSYS CMake/Ninja understand GCC's Windows dependency paths. MSYS
    # Makefiles misparse drive letters in depfiles emitted by devkitA64.
    host_cmake="/c/Program Files/CMake/bin/cmake.exe"
    generator_flags=(-G Ninja -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja.exe);;
esac
test -f "$DEVKITPRO/portlibs/switch/lib/libSDL2.a" ||
    { echo "Install switch-sdl2 switch-mesa switch-glad using devkitPro pacman." >&2; exit 1; }
mkdir -p "$build" "$out"
if [ -z "$host_recompiler" ]; then
    # Configure without the cross toolchain: this program runs on the build host.
    host_flags=()
    case "$(uname -s)" in MSYS*|MINGW*|CYGWIN*)
        host_flags=(-G Ninja -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe
            -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe);;
    esac
    "$host_cmake" -S "$engine/recompiler" -B "$build/host" -DCMAKE_BUILD_TYPE=Release "${host_flags[@]}"
    "$host_cmake" --build "$build/host" --target GenesisRecomp --parallel "$jobs"
    if [ -f "$engine/recompiler/src/main_step.c" ]; then
        "$host_cmake" --build "$build/host" --target M68kStepRecomp --parallel "$jobs"
    fi
    host_recompiler="$build/host/GenesisRecomp"
    test ! -f "$host_recompiler.exe" || host_recompiler="$host_recompiler.exe"
fi
test -f "$host_recompiler" || { echo "Missing host recompiler: $host_recompiler" >&2; exit 1; }
if [ -z "$host_step_recompiler" ]; then
    host_step_recompiler="$(dirname "$host_recompiler")/M68kStepRecomp"
    test ! -f "$host_step_recompiler.exe" || host_step_recompiler="$host_step_recompiler.exe"
fi
for selected in "${games[@]}"; do
    active_engine="$engine"
    if [ "$selected" = sonic2 ] && [ -n "$sonic2_engine" ]; then active_engine="$sonic2_engine"; fi
    case "$selected" in
        sonic1) repo="${sonic1_repo:-$games_root/SonicTheHedgehogRecomp}"; target=SonicTheHedgehogRecomp
            prefix=sonic; short=Sonic1; name="Sonic the Hedgehog"
            rom="$games_root/SonicTheHedgehogRecomp/Sonic the Hedgehog (JUE) [!].bin";;
        sonic2) repo="${sonic2_repo:-$games_root/SonicTheHedgehog2Recomp}"; target=SonicTheHedgehog2Recomp
            prefix=sonic2; short=Sonic2; name="Sonic the Hedgehog 2"; rom="$games_root/SonicTheHedgehog2Recomp/sonic2.bin";;
        sonic3) repo="${sonic3_repo:-$games_root/Sonic3AndKnucklesRecomp}"; target=Sonic3Recomp
            prefix=sonic3; short=Sonic3; name="Sonic the Hedgehog 3"; rom="$games_root/Sonic3AndKnucklesRecomp/game/sonic3/sonic3.bin";;
        sonic3k) repo="${sonic3_repo:-$games_root/Sonic3AndKnucklesRecomp}"; target=Sonic3KRecomp
            prefix=sonic3k; short=Sonic3K; name="Sonic 3 & Knuckles"; rom="$games_root/Sonic3AndKnucklesRecomp/sonic3k.bin";;
        sandk) repo="${sonic3_repo:-$games_root/Sonic3AndKnucklesRecomp}"; target=SonicAndKnucklesRecomp
            prefix=sandk; short=SonicK; name="Sonic & Knuckles"; rom="$games_root/Sonic3AndKnucklesRecomp/game/sandk/sandk.bin";;
    esac
    if [ -n "$rom_override" ]; then
        rom="$(cd -- "$(dirname -- "$rom_override")" && pwd)/$(basename -- "$rom_override")"
    fi
    test -f "$rom" || { echo "Missing original image: $rom" >&2; exit 1; }
    "$cmake_bin" -S "$repo" -B "$build/$selected" "${generator_flags[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release \
        -DGENESIS_RECOMP_ROOT="$active_engine" -DGENESIS_HOST_RECOMPILER="$host_recompiler" \
        -DGENESIS_RECOMPILER_EXE="$host_recompiler" \
        -DGENESIS_HOST_STEP_RECOMPILER="$host_step_recompiler" \
        -DCMAKE_PROJECT_INCLUDE="$script_engine/cmake/GenesisSwitch.cmake" \
        -DGENESIS_SWITCH_NAME="$name" "-DGENESIS_ROM_$prefix=$rom" \
        "${cross_host_flags[@]}" \
        -DGEN_ENABLE_TRACE=OFF -DGEN_DEV_TRACE=OFF -DSONIC_REVERSE_DEBUG=OFF \
        -DBUILD_TESTING=OFF -DBUILD_TESTS=OFF -DGENESISRECOMP_NETPLAY=OFF \
        -DSONIC_CLONE_DRIVER_SUPPORT=OFF -DS3_CLONE_DRIVER_SUPPORT=OFF
    "$cmake_bin" --build "$build/$selected" --target "${target}_nro" --parallel "$jobs"
    pack_flags=()
    [ "$with_roms" -eq 0 ] || pack_flags+=(--with-roms)
    python3 "$script_engine/tools/package_switch.py" --build "$build/$selected" \
        --target "$target" --short "$short" --rom "$rom" --out "$out" \
        --repo "$repo" --engine "$active_engine" "${pack_flags[@]}"
done
