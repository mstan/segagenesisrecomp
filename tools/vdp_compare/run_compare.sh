#!/usr/bin/env bash
# run_compare.sh - VDP state/picture parity harness: our clean-room genesis_vdp.c
# vs die-accurate BlastEm, compared at a CONTENT-locked sync point (the Sonic 1
# Sega logo). Layered-parity safe: absolute-frame matching across the two
# backends is INVALID (boot timelines drift) -- we lock by palette content.
#
# Measurement only. One runtime instance at a time. Dev-only BlastEm (GPLv3).
#
# Prereqs: blastem.exe built (../blastem, MSYS2 MINGW64, see BLASTEM_ORACLE_README.md)
#          SonicTheHedgehogRecomp.exe + sonic.bin in REL_DIR below.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/_out"; mkdir -p "$OUT"
BLASTEM="$HERE/../blastem/blastem.exe"
REL_DIR="F:/Projects/segagenesisrecomp/SonicTheHedgehogRecomp/build/Release"
ROM="$REL_DIR/sonic.bin"
PORT=4380
TK=/c/Windows/System32/taskkill.exe

cp -f "$ROM" "$OUT/sonic.bin"
"$TK" //F //IM blastem.exe 2>/dev/null || true
"$TK" //F //IM SonicTheHedgehogRecomp.exe 2>/dev/null || true

# 1) Oracle: scan VDP-hash timeline, find a fully-static window, dump it.
( cd "$OUT" && BLASTEM_VDP_DUMP="$OUT/scan" "$BLASTEM" -b 700 sonic.bin 2>scan.log )
python "$HERE/find_stable_window.py" "$OUT/scan.vdphash.csv" --min-run 20
# Frame 400 sits inside the settled Sega-logo static run (318-655) for Sonic 1:
( cd "$OUT" && BLASTEM_VDP_DUMP="$OUT/be_sega" BLASTEM_VDP_FRAME=400 "$BLASTEM" -b 420 sonic.bin 2>be_sega.log )
python "$HERE/fb_to_png.py" "$OUT/be_sega"

# 2) Ours: launch, content-lock on the oracle palette, dump surfaces + screenshot.
( cd "$REL_DIR" && cmd.exe //C "start /B SonicTheHedgehogRecomp.exe sonic.bin --port $PORT > native_run_vdp.log 2>&1" )
python "$HERE/catch_by_content.py" "$PORT" "$OUT/our_sega" "$OUT/be_sega.cram.bin" --min-match 50 --timeout 25
"$TK" //F //IM SonicTheHedgehogRecomp.exe 2>/dev/null || true

# 3) Compare the three state surfaces (CRAM/VSRAM/VRAM), normalised.
python "$HERE/compare_state.py" --ours "$OUT/our_sega" --oracle "$OUT/be_sega"
echo "Visual: $OUT/our_sega.png (ours) vs $OUT/be_sega.fb.png (oracle)"
