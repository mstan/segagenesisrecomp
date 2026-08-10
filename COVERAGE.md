# 68000 Coverage and Validation

This document describes the current validation surface for the Genesis 68000
recompiler. It is not a claim that every MC68000 opcode, effective-address
combination, exception, or timing edge is exhaustively proven.

## Source of truth

The active frontend lives in the pinned `external/m68k-recomp-core` submodule:

- `common/m68k_decoder.c` and `common/m68k_validator.c` implement shared
  instruction decoding and legality checks.
- `profiles/genesis/function_finder.c` owns Genesis discovery policy.
- `profiles/genesis/code_generator.c` and `codegen_diag.c` implement emission,
  timing, and machine-readable unsupported-path diagnostics.

The tiny files with matching names under `recompiler/src/` are compatibility
forwarders for older consumers and test include paths; they are not independent
implementations.

The CPU target is the MC68000-compatible main processor in the Sega Genesis.
Later-family encodings are out of scope unless a profile explicitly documents
and tests them. The authoritative architecture references are the
[MC68000 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf)
and [M68000 Family Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).

## Failure and fallback policy

- The validator rejects known illegal encodings before they are treated as
  executable code.
- Code generation records every `MN_OTHER`, unsupported dynamic transfer,
  invalid store, effective-address fallback, missing branch target, and
  misaligned function entry through `codegen_diag`.
- `--fail-on-unsupported` turns recorded unsupported generation paths into a
  failed recompilation instead of silently accepting comment-only output.
- At runtime, the clean-room Tier-3 interpreter is the correctness floor for
  supported dispatch misses and RAM-resident handlers. Unsupported execution
  halts loudly with the offending PC and opcode.
- Recomp-vs-interpreter co-simulation is the supported semantic cross-check.
  The retired clown68000 L3 oracle and its generated manifest are not part of
  the repository.

## Automated harnesses

The ROM-independent CTest suite contains:

| Test | Coverage |
|---|---|
| `m68k_validator` | Synthetic legal/illegal encoding decisions |
| `m68k_decoder_synth` | Decoder families and forms not guaranteed to occur in one game fixture |
| `codegen_diag` | Diagnostic recording, counts, ordering, and summaries |
| `return_capture` | Callees that consume or rewrite JSR return slots |
| `m68k_effective_address` | Shared brief-extension index/address semantics |

`l1_decoder_test` is intentionally manual because it requires a user-supplied
Sonic ROM. It checks fixture bytes, decoded lengths, and mnemonic classes; it
does not prove semantics or full opcode coverage.

Build and run the self-contained suite with:

```bash
cmake -S tests -B build/tests
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

## Known limits

- Some dynamic JSR/JMP effective-address forms remain unsupported and are
  reported by `codegen_diag`.
- Memory-predecrement ADDX/SUBX generation remains a diagnosed gap.
- Static discovery is conservative and uses per-game disassembly, annotation,
  jump-table, and runtime-execution evidence declared in `game.toml`.
- Synthetic harnesses cover important classes and regressions, not the full
  opcode × size × effective-address matrix.
- Instruction-cost validation, whole-game behavior, video/audio fidelity, and
  discovery completeness are separate axes. A passing decoder test does not
  imply those axes are complete.

When expanding coverage, add a focused synthetic fixture, preserve the loud
diagnostic/failure behavior for unsupported cases, and validate both generated
and interpreter semantics where the instruction is executable at runtime.
