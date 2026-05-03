# 68K Recompiler Coverage Audit

This audit focuses only on the static Genesis 68K recompiler:

- Decoder: `recompiler/src/m68k_decoder.c`
- Code generator: `recompiler/src/code_generator.c`
- Function discovery: `recompiler/src/function_finder.c`
- Recompiler tests: `tests/`

Generated Sonic output, runner code, and clown68000 are referenced only as evidence or test infrastructure. They are not part of the coverage target.

## Source Of Truth

Primary CPU target is the Sega Genesis main CPU, an MC68000-compatible 68K. The instruction-set comparison should therefore use the MC68000 subset and treat later-family forms as out of scope unless the project intentionally accepts them.

Reference manuals:

- NXP MC68000 User's Manual: https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf
- NXP M68000 Family Programmer's Reference Manual: https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf

Important scope note: the family PRM includes later 68010/68020/CPU32-era behavior. The recompiler currently tolerates at least one later-family branch form, `Bcc/BSR/BRA` with 32-bit displacement when opcode displacement byte is `0xFF`. That is marked in the decoder as `68020+` and should not be considered required MC68000 coverage.

## Current Coverage Model

The decoder is permissive. `m68k_decode()` almost always returns `true` after reading at least the opcode word, classifies unsupported or unrecognized instructions as `MN_OTHER`, and consumes extension words only for the patterns it recognizes. It does not consistently reject invalid effective-address combinations.

The code generator emits C for many decoded mnemonics, but several decoded instructions emit only comments and no semantic behavior. `MN_OTHER` emits an `unimplemented opcode` comment in generated C, so unsupported instructions can compile without failing loudly.

Function discovery is static and worklist-based. It follows static calls, conditional branches, unconditional branches, and static jumps. Unknown dynamic jumps terminate the static scan. Runtime dispatch fallback exists for some computed calls and jumps in codegen, but static function discovery still depends heavily on `game.cfg`, disassembly seed files, coverage logs, and manual extra function entries.

## Implemented Or Mostly Implemented

These instruction families have explicit decoder classes and non-stub codegen paths:

- Data movement and address work: `MOVE`, `MOVEA`, `MOVEQ`, `MOVEM`, `LEA`, `PEA`, `LINK`, `UNLK`, `EXG`, `MOVE_USP`, `MOVE_SR`
- Integer arithmetic and logical operations: `ADD`, `ADDA`, `ADDI`, `ADDQ`, `SUB`, `SUBA`, `SUBI`, `SUBQ`, `AND`, `ANDI`, `OR`, `ORI`, `EOR`, `EORI`, `CMP`, `CMPA`, `CMPI`
- Unary and bit operations: `TST`, `CLR`, `NEG`, `NEGX`, `NOT`, `EXT`, `SWAP`, `TAS`, `BTST`, `BCHG`, `BCLR`, `BSET`
- Shifts and rotates: `ASL`, `ASR`, `LSL`, `LSR`, `ROL`, `ROR`, `ROXL`, `ROXR`
- Multiply/divide: `MULS`, `MULU`, `DIVS`, `DIVU`
- Control flow: `JSR`, `BSR`, `JMP`, `BRA`, `Bcc`, `DBcc`, `RTS`, `RTE`, `STOP`, `NOP`
- Conditional set: `Scc`

This does not mean every addressing mode and flag edge case is proven correct. It only means the recompiler has a non-comment codegen path for the family.

## Highest Priority Gaps

### Decoded But Stubbed Or Partially Stubbed

These are the most direct coverage misses because the decoder recognizes them but codegen emits no real behavior or only partial behavior:

- `MOVEP`: emits `TODO: MOVEP`; no alternating-byte memory transfer semantics.
- `ABCD`, `SBCD`, `NBCD`: emit TODO comments; no packed-BCD arithmetic or flag behavior.
- `CHK`: emits a comment only; no bounds check, exception behavior, or flags/trap modeling.
- `ADDX` and `SUBX`: data-register form is emitted, but memory predecrement form `-(Ay),-(Ax)` emits TODO only.
- `TRAP`: decoded and emitted, but codegen currently ignores the trap rather than vectoring or reporting unsupported behavior.
- `STOP`: emitted as `return;`; acceptable for a halted game path, but not a full CPU halt/interrupt resume model.

### Valid 68000 Instructions Currently Collapsed To `MN_OTHER`

These valid or architecturally meaningful MC68000 instructions are decoded as `MN_OTHER`, causing generated output to contain only `unimplemented opcode` comments:

- `RESET`
- `TRAPV`
- `RTR`
- `CMPM`
- Immediate-to-status forms such as `ORI/ANDI/EORI #imm,CCR` and `ORI/ANDI/EORI #imm,SR`
- `ILLEGAL`, A-line, and F-line exception classes are not modeled as traps; they collapse to `MN_OTHER`

`MOVE_CCR` also needs review. The decoder distinguishes `MOVE <ea>,CCR` and `MOVE CCR,<ea>` using the opcode pattern, but both map to one mnemonic. Codegen always treats `MN_MOVE_CCR` as loading from the effective address into CCR, so any `MOVE CCR,<ea>` form would be mis-emitted rather than stored.

### Opcode Legality And Invalid Effective Addresses

The decoder recognizes broad bit patterns but does not enforce the full MC68000 legal effective-address matrix. This creates two risks:

- Invalid opcodes embedded in data can be decoded as real instructions, extending function scans or generating misleading C.
- Valid instruction families with illegal addressing modes can emit comments such as `cannot store to EA 7/x` instead of failing or trapping.

Examples worth hardening:

- `MOVE.B` with address-register destination can be decoded as `MOVEA`, even though `MOVEA` is word/long only.
- Arithmetic/logical groups can accept reserved size encodings or invalid destination modes because legal EA filtering is not centralized.
- PC-relative and immediate modes can reach store helpers and produce `cannot store` comments.
- Branch displacement byte `0xFF` is decoded as a 32-bit branch displacement despite being marked `68020+`, outside Genesis MC68000 scope.

## Control-Flow Coverage Gaps

Function discovery has a known gap for jump tables. `function_finder.c` documents `JMP table(PC,Dn.W)` as a target pattern, but the implementation still has a TODO and terminates the path when it sees a non-static `JMP`. This means indexed dispatch tables are not automatically enumerated by static analysis.

Codegen handles some computed control flow at runtime:

- Some dynamic `JSR/BSR` forms use `call_by_address(...)`.
- Some dynamic `JMP` forms use `call_by_address(...)`.
- PC-indexed jump-table `JMP (d8,PC,Xn)` emits `hybrid_jmp_interpret(...)`.

Remaining control-flow miss cases:

- Unsupported dynamic `JSR/BSR` modes emit a TODO comment and then continue to the post-call stack-pop path.
- Unsupported dynamic `JMP` modes emit a TODO comment and immediately return.
- `BRA` or `Bcc` without a target emits a comment path. This should normally not occur for valid decoded MC68000 branch forms, so it is a good candidate for a hard diagnostic.
- Static discovery still relies on manually maintained `game.cfg`, `sonic1.disasm_seeds.txt`, `sonic1.disasm_subs.txt`, runtime coverage logs, and blacklist maintenance.

## Test Coverage

Current test coverage is useful but not exhaustive:

- `l1_decoder_test` checks ROM byte integrity, decoded instruction length, and mnemonic class against Sonic 1 fixture rows. It does not verify all opcodes, all legal/illegal EA combinations, instruction semantics, or flags.
- `l3_oracle_test` compares selected generated Sonic functions against clown68000. This catches semantic bugs in covered generated functions but only for functions and paths represented in the L3 manifest and initial-state fixture strategy.
- The tests are Sonic-ROM centered. They do not provide a synthetic instruction matrix that exercises every MC68000 opcode family, addressing mode, flag edge case, trap behavior, or invalid opcode class.

Cycle coverage is separate from semantic coverage. The code generator estimates cycles from clown68000 probing or PRM-derived fallback tables, but a valid cycle estimate does not imply an instruction has complete emitted behavior.

## Recommended Next Work

1. Add hard diagnostics for unsupported generated behavior. `MN_OTHER`, comment-only decoded instructions, unsupported dynamic control-flow modes, and impossible branch-without-target cases should be counted and reported during codegen so coverage misses are visible before runtime.
2. Implement automatic static enumeration for `JMP table(PC,Dn.W)` and related indexed dispatch tables, or make manual `game.cfg` jump-table coverage explicit and machine-checkable.
3. Add an opcode legality layer after decode. It should validate instruction-specific legal sizes, source EA modes, destination EA modes, and MC68000-only scope before function discovery or codegen treats an opcode as executable.
4. Fill high-value decoded-but-stubbed semantics: `CMPM`, immediate-to-CCR/SR operations, `RTR`, `TRAPV`, `CHK`, memory-form `ADDX/SUBX`, then `MOVEP` and BCD instructions.
5. Add synthetic decoder/codegen fixtures for each instruction family and addressing mode, plus explicit negative tests for invalid MC68000 encodings.
6. Keep L3 oracle tests for game-specific regression protection, but do not rely on them as proof of full 68K instruction coverage.

