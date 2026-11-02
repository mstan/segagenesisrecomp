# Statically recompiled Genesis sound Z80

The native Genesis backend can optionally execute a cartridge's uploaded sound
driver through statically generated Z80 C instead of decoding every opcode in
SuperZazu at runtime. The Genesis scheduler remains authoritative for Z80
reset, BUSREQ, interrupt delivery, per-scanline cycle slices, save states, and
the YM2612/PSG bus.

The generator lives in [`smsggrecomp`](https://github.com/mstan/smsggrecomp)
and reuses its Z80 decoder and emitter. The packed state, generated host ABI,
and verified instruction semantics live in the pinned
[`z80-recomp-core`](https://github.com/mstan/z80-recomp-core) submodule shared
by SMS/GG and Genesis. `SmsRecomp --flat-step` emits a single `<prefix>_step()`
entry point that executes one instruction and returns to the Genesis scheduler.

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

4. Configure the supported game build with:

   ```sh
   cmake -S <game> -B <build> \
     -DGENESIS_Z80_RECOMP=ON \
     -DGENESIS_Z80_AOT_SOURCE=<absolute-path>/<prefix>_step.c
   ```

   `cmake/GenesisRecompZ80.cmake` supplies the adapter, generated source,
   pinned `external/z80-recomp-core` headers and compile definitions. The
   shared runner reconciler applies this to consumers listing runner sources
   manually, as well as targets using `genesisrecomp_runner_target()`.
   Initialize the pinned core submodule before enabling the option.

   The entry defaults to the source basename, e.g. `proof_step.c` means
   `proof_step()`. Set `GENESIS_Z80_AOT_ENTRY` for a different C identifier.
   An explicit `GameSpec.z80_step` still takes precedence. Legacy consumer
   specs conditionally include a generated header and name their own entry:
   use `rkaz80` for RKA and `s1z80` for Sonic1. Consumers with a NULL hook use
   the configured entry directly. Do not substitute one title's captured
   image for another. The helper suppresses the SMS function-form
   `call_by_address` declaration so it cannot conflict with the68000 ABI.

The ROM-derived RAM image and generated C are local build artifacts and must
not be committed. Ordinary builds leave `GENESIS_Z80_RECOMP` off and retain the
existing interpreter path.

## Correctness and fallback

The generated output contains a case for every byte address in the captured
image (plus any captured variants) and validates the live opcode bytes for the
selected instruction. Common self-modified immediate, absolute-address, and
indexed-displacement operands are read live while their opcode shape remains
statically decoded. During the initial 68K-to-Z80 RAM upload, or if code does
not match any compiled shape, execution falls back for that instruction to
SuperZazu. Once the live driver matches, execution resumes in generated code
automatically. This avoids running a final captured image prematurely during
boot while keeping runtime opcode decode out of the matching path.

The experiment was regression-tested in turbo mode against the interpreter:

- Sonic the Hedgehog: 1,800 frames, byte-identical WAV output.
- Sonic the Hedgehog 2: 300 frames with byte-identical WAV output, followed by
  a normal-speed listening pass.
- Sonic 3 standalone: byte-identical WAV, chip stream, and final Z80 RAM.
- Sonic 3 & Knuckles lock-on: 7,300 frames with a WAV SHA-256 byte-identical
  to the archived known-good interpreter golden, including its self-modifying
  loop and later attract states.
- Rocket Knight Adventures: 1,800 frames with byte-identical WAV output and
  identical framebuffer hashes.

Sonic 1, Sonic 3, Sonic 3 & Knuckles, and Rocket Knight Adventures used only
seven interpreter fallback instructions during their initial uploads and no
later fallback in the tested windows. Sonic 2 additionally exercised one
recurring self-modified PC through the faithful interpreter fallback.

Fallback diagnostics are structured and silent: the `z80_state` TCP command
reports `z80_recomp.fallback_steps` and
`z80_recomp.fallback_unique_pcs` without writing per-instruction logs.

## Native slice ownership and current qualification

The cartridge scheduler now runs the existing scanline ownership interval
inside the adapter. Every instruction still performs live guards, bus access,
its original audio start stamp and IRQ acceptance. The complete legacy
register/flag representation is published at the slice edge and explicitly
before interpreter fallback. `GEN_DEV_TRACE` builds retain publication after
every instruction because trace bus callbacks observe the previous completed
legacy PC. Reset/BUSREQ are settled at the same boundaries as the prior
scheduler; this does not change cartridge CPU interleaving.

The one-instruction API remains available. The internal compile-time
`GENESIS_Z80_STEP_PUBLICATION_REFERENCE` restores the old scheduler loop for
qualification; it is not a runtime backend switch. CD already owns native
continuous intervals and retains its coordinator/WAIT/terminal-miss contract.

Fresh E06 diligence found and repaired missing HALT refresh, premature IRQ
after fallback EI, missing IRQ WZ, Windows64-bit cycle loss through the
32-bit legacy cycle field, and previously absent shared CMake integration.
Real-producer variant tests also exposed missing live-operand WZ updates,
fixed in the SMS/GG producer. See [E06 evidence](ENGINE_Z80_BACKEND.md) for
the actual scope, identities and per-game qualification; the historical
driver checks above are retained as earlier evidence, not rerun claims.
