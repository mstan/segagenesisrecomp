#!/usr/bin/env bash
# Build the Tier-3 interpreter vs clown68000 differential harness (gcc/MinGW).
# Standalone: links only the interpreter + decoder + clown68000 + flat memory.
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENG="$(cd "$HERE/../.." && pwd)"                     # segagenesisrecomp/
C68K="$ENG/clownmdemu-core/libraries/clown68000/source"

gcc -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter \
    -I"$ENG/runner" -I"$ENG/runner/include" \
    -I"$ENG/recompiler/src" \
    -I"$C68K/interpreter" -I"$C68K/common" \
    "$HERE/m68k_interp_diff.c" \
    "$ENG/runner/m68k_interp.c" \
    "$ENG/recompiler/src/m68k_decoder.c" \
    "$ENG/recompiler/src/rom_parser.c" \
    "$C68K/interpreter/clown68000.c" \
    "$C68K/common/opcode.c" \
    -o "$HERE/m68k_interp_diff.exe"
echo "built: $HERE/m68k_interp_diff.exe"
