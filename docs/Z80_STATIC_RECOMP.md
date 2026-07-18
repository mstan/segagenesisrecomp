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

4. Configure the game with `GENESIS_Z80_RECOMP=ON`, the SMS/GG framework root,
   and the generated C source. Each game repository's feature branch exposes
   `GENESIS_Z80_CORE_ROOT` and `GENESIS_Z80_AOT_SOURCE` cache variables.

The ROM-derived RAM image and generated C are local build artifacts and must
not be committed. Ordinary builds leave `GENESIS_Z80_RECOMP` off and retain the
existing interpreter path.

## Correctness and fallback

The generated output contains a case for every byte address in the captured
image and validates the live bytes for the selected instruction. During the
initial 68K-to-Z80 RAM upload, or if code is modified or does not match the
capture, execution falls back for that instruction to SuperZazu. Once the live
driver matches, execution resumes in generated code automatically. This avoids
running a final captured image prematurely during boot while keeping runtime
opcode decode out of the matching path.

The experiment was regression-tested in turbo mode against the interpreter:

- Sonic the Hedgehog: 1,800 frames, byte-identical WAV output.
- Sonic 3 standalone: byte-identical WAV, chip stream, and final Z80 RAM.
- Sonic 3 & Knuckles lock-on: byte-identical WAV, chip stream, and final Z80
  RAM using its own distinct captured driver.
- Rocket Knight Adventures: 600 frames with identical framebuffer hashes,
  WAV, chip stream, and final Z80 RAM.

All four cases used only seven interpreter fallback instructions during the
initial upload and no fallback after the final driver image became live.
