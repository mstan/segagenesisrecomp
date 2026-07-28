# HANDOFF — Puyo Puyo (Japan) bring-up

**Branch:** `feat/puyo-puyo-bringup`
**Updated:** 2026-07-27
**State:** attract and deterministic Stage-1 fuzz probes run natively with no
dispatch/floor fallback artifacts.

## Layout

| Thing | Path |
|---|---|
| Engine worktree | `F:\Projects\segagenesisrecomp\_wt-puyo` |
| Game project | `F:\Projects\segagenesisrecomp\PuyoPuyoRecomp` |
| Local engine junction | `PuyoPuyoRecomp\engine-local` → `_wt-puyo` |
| Game config/scripts | `_wt-puyo\puyo\` |

`puyo.bin` is gitignored. The expected dump is 512 KiB, CRC32 `7F26614E`,
serial `GM G-4082 -00`, region J, header checksum `$BCE7`.

## Build

```powershell
cmake -S F:/Projects/segagenesisrecomp/PuyoPuyoRecomp `
      -B F:/Projects/segagenesisrecomp/PuyoPuyoRecomp/build-vs `
      -G "Visual Studio 17 2022" -A x64
cmake --build F:/Projects/segagenesisrecomp/PuyoPuyoRecomp/build-vs `
      --config Release --target PuyoRecomp
```

## What fixed the bring-up

The apparent frame-120 freeze was a short static scene, not a permanent
V-blank stall. Native execution resumed afterward and eventually corrupted at
the first missing object handler. A whole-program forced-interpreter run
completed cleanly, proving the clean-room interpreter, interrupts, and game
logic were sound and isolating the fault to native discovery.

Puyo's object allocator helpers at `$002A54/$002AB0` consume a handler pointer
loaded into A1:

```asm
lea     Handler.l,a1
bsr     $2A54
```

`[functions].function_pointer_helpers` now expresses that per-game semantic
edge. The finder promotes the absolute A1 target when the immediately following
direct call names a configured helper. This found 97 structurally proven edges
in the final build. Return-address-capturing helper continuations are also
promoted during late discovery, which closes Puyo's object state chains.

Additional audited roots matter:

- `$0055A4` is a script-referenced constructor; exposing it lets the helper
  edge discover `$005666`.
- `$00F90C` owns the 16-slot long-pointer table at `$00F926`. It is an early
  root so the normal strong table validator enumerates `$F966/$F968/$F9A6`.
- `$00CAD0` is a late object-handler entry first reached during lesson cleanup.
- `$00BC2C` is the first entry of a late attract state chain. Promoting that
  one decoded root lets ordinary direct-control-flow and captured-continuation
  discovery split all six handlers through `$00BD38`.

All remaining `late_extra` entries came from runtime leads and were decoded
against the raw ROM before promotion. Do not seed downstream garbage after a
failed handler; only the first valid missed entry is evidence.

Two shared 68000 correctness fixes were required after discovery was complete:

- Puyo's `$002C88` collision path conditionally executes
  `MOVEM.L (A7)+,D0; RTS`, skipping its caller's continuation. Generated code
  now tracks net longword stack pops through MOVE/MOVEA/PEA/MOVEM and propagates
  the skip only on the path that actually consumes it. The static
  return-capture classifier remains conservative and has a bounded all-path
  proof.
- Brief indexed effective addresses now honor extension bit 11. The old code
  always sign-extended a word index in both native and Tier-3 execution. At
  `$015238`, Puyo requests `D3.L=$0000E040`; the bug read `$FEE04A` as
  `$FFFFFFFF` instead of the score at `$FFE04A`, corrupting every score digit.
  Native and interpreter paths share the tested W/L resolver.

## Interpreter coverage

`GENESIS_FORCE_INTERP=1` is now available in an ordinary native build; it is no
longer gated by `GENESIS_COSIM`. This is the in-tree clean-room interpreter.
`--exec-coverage-out` therefore provides full-ROM executed-PC coverage when
forced interpretation is enabled, while a normal native run records only any
Tier-3 floor work.

Example:

```powershell
$env:GENESIS_FORCE_INTERP = "1"
.\PuyoRecomp.exe .\puyo.bin --no-launcher --turbo --max-frames 1800 `
    --exec-coverage-out .\puyo_full_coverage.bin
Remove-Item Env:\GENESIS_FORCE_INTERP
```

## Final validation

Run from `PuyoPuyoRecomp\build-vs\Release`:

```powershell
.\PuyoRecomp.exe .\puyo.bin --no-launcher --turbo --max-frames 13000 `
    --hash-frames 600 `
    --input-script F:\Projects\segagenesisrecomp\_wt-puyo\puyo\puyo_attract_visual.input

.\PuyoRecomp.exe .\puyo.bin --no-launcher --turbo --max-frames 8000 `
    --hash-frames 600 `
    --input-script F:\Projects\segagenesisrecomp\_wt-puyo\puyo\puyo_gameplay_fuzz.input
```

Results:

- Attract: script exited at frame 12,001 after ten visual checkpoints spanning
  two title/tutorial cycles. The final run passed the former frame-7,096
  `$00BC2C` handler transition with no floor or dispatch event.
- Gameplay fuzz: entered Stage 1, exercised movement/rotation/fast-drop input,
  returned through title/attract/tutorial transitions, and exited at frame
  5,677.
- Both final runs produced no `floor_coverage.txt`, `floor_unsafe.log`,
  `dispatch_misses.log`, or `interior_label_misses.log`.
- A separate normal-cleanup 1,200-frame WAV smoke produced 20.018 seconds of
  stereo 16-bit audio at 223721 Hz: about -30.6 dBFS RMS and -12.0 dBFS peak,
  with no clipping, NaNs, infinities, or denormals.
- The Release build reports zero unsupported codegen operations.
- A forced-interpreter 12,001-frame run also completed. At frame 3,600 its
  corrected score tilemap (`00000062`) matches an independent Genesis Plus GX
  v1.7.4 reference capture exactly. Later AI boards are visually coherent but
  not frame-deterministic across cores because interrupt/cycle timing changes
  RNG progression.

Framework harness status: `m68k_effective_address_test`,
`return_capture_test`, `m68k_validator_test`, and `codegen_diag_test` pass.
`m68k_decoder_synth_test` still has the pre-existing three CMPM classification
failures; this branch does not modify either decoder. The L1 Sonic fixture
cannot run here because this worktree intentionally has only the Puyo ROM.

## Still worth doing

- Human confirmation of music/effects quality and actual controller feel is
  still required; automated WAV statistics only prove healthy, non-silent,
  non-clipping output.
- The Sonic-shaped framelog fields remain unset for Puyo. Its `mode/vbl/cnt`
  columns still read WRAM offset zero and must not be used as Puyo telemetry
  until their real addresses are established.
- Input-script `EXIT` bypasses WAV-header finalization. Use `--max-frames`
  without a script `EXIT` for audio captures until that runner cleanup issue is
  fixed.
- No Puyo Ghidra program exists in the current headless registry. Do not launch
  the GUI; add/import it through the documented registry workflow if deeper
  annotation work becomes necessary.
