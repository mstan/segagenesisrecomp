# Statically recompiled Genesis sound Z80

The native Genesis backend can optionally execute a cartridge's uploaded sound
driver through statically generated Z80 C instead of decoding every opcode in
SuperZazu at runtime. The Genesis scheduler remains authoritative for Z80
reset, BUSREQ, interrupt delivery, per-scanline cycle slices, save states, and
the YM2612/PSG bus.

The generator lives in `smsggrecomp` and reuses its Z80 decoder, semantic
emitters, packed flag state, and timing tables. `SmsRecomp --flat-step` emits a
single `<prefix>_step()` entry point that executes one instruction and returns
to the Genesis scheduler.

## Local generation workflow

1. Build the game normally with `GEN_DEV_TRACE=ON`.
2. Run far enough for the cartridge to upload and initialize its sound driver,
   then use `--snd-dump-frame N` to produce `z80_ram.bin`.
3. Point a minimal SMS/GG `game.toml` at that 8 KiB image, set a unique
   `output_prefix`, and run:

   ```sh
   SmsRecomp --game path/to/game.toml --flat-step
   ```

   Games that replace or patch their driver later can supply additional RAM
   captures with repeated `--flat-step-variant path/to/z80_ram.bin` arguments.

4. Configure the game with `GENESIS_Z80_RECOMP=ON`, the SMS/GG framework root,
   and the generated C source. Each game repository's feature branch exposes
   `GENESIS_Z80_CORE_ROOT` and `GENESIS_Z80_AOT_SOURCE` cache variables.

The ROM-derived RAM image and generated C are local build artifacts and must
not be committed. Ordinary builds leave `GENESIS_Z80_RECOMP` off and retain the
existing interpreter path.

## Correctness and fallback

The generated output contains a case for every byte address in the captured
image (plus any captured variants) and validates the live bytes for the selected instruction. During the
initial 68K-to-Z80 RAM upload, or if code is modified or does not match the
capture, execution falls back for that instruction to SuperZazu. Once the live
driver matches, execution resumes in generated code automatically. This avoids
running a final captured image prematurely during boot while keeping runtime
opcode decode out of the matching path.

The experiment was regression-tested in turbo mode against the interpreter:

- Sonic the Hedgehog: 1,800 frames, byte-identical WAV output.
- Sonic 3 standalone: byte-identical WAV, chip stream, and final Z80 RAM.
- Sonic 3 & Knuckles lock-on: 1,800 frames with byte-identical WAV output and
  framebuffer hashes using seven observed driver snapshots. Its continuously
  self-modifying upper-RAM routine uses transparent one-instruction fallback.
- Rocket Knight Adventures: 600 frames with identical framebuffer hashes,
  WAV, chip stream, and final Z80 RAM.

Sonic 1, Sonic 3, and RKA used only seven interpreter fallback instructions
during their initial uploads and no later fallback in the tested windows.
Sonic 3&K additionally exercises the fallback for code sequences that cannot
be represented by a finite set of static snapshots; matching instructions
continue to execute through the generated backend.

Fallback diagnostics are silent by default because a self-modifying hot loop
can miss many times per frame. Set `GENESIS_Z80_AOT_LOG_MISSES=1` to log the
first miss at each distinct PC for debugging.
