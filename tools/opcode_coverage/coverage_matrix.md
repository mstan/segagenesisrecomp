# 68000 Opcode-Coverage Matrix (Axis-1: instruction semantics)

Static measurement of the Genesis 68K recompiler's instruction coverage.
Sources read (worktree `_wt-accuracy`):

- Decoder: `recompiler/src/m68k_decoder.c` (+ `.h` for the `MN_*` enum)
- Codegen: `recompiler/src/code_generator.c`
- Legality validator: `recompiler/src/m68k_validator.c`
- Codegen diagnostics: `recompiler/src/codegen_diag.h`

Companion tools in this directory:

- `scan_generated.py` — REAL-GAME exposure scan (greps shipped generated C).
- `synthetic_sweep.c` — sweeps all 65536 base opcode words through the real
  decoder + validator.

> **COVERAGE.md (repo root) is STALE.** It predates "Phase 7A/7B/7C" and the
> Phase-4 ADDX/SUBX work. It still lists RESET / TRAPV / RTR / CMPM /
> ORI-ANDI-EORI-to-CCR/SR / ILLEGAL as collapsed to `MN_OTHER`, MOVEP / CHK /
> ABCD / SBCD / NBCD as TODO stubs, mem-form ADDX/SUBX as TODO, and the
> `MOVE CCR,<ea>` mis-emit as live. **All of those are now real codegen** (see
> the per-row citations below and `codegen_diag.h:16-24` which documents the
> retirements). Treat this matrix, not COVERAGE.md, as current.

## Headline

- **83 distinct mnemonics** are defined in `M68KMnemonic`
  (`m68k_decoder.h:18-121`), excluding the `MN_OTHER` residual bucket.
- **83 / 83 have a REAL (non-comment, non-stub) codegen path.** Every
  `case MN_*` in `code_generator.c` emits actual semantics.
- **0 STUB, 0 comment-only, 0 known MIS-EMIT** mnemonics remain.
  (The historic `MOVE CCR,<ea>` mis-emit is fixed: `code_generator.c:3290-3305`
  handles both directions via the decoder's `dst_is_ea` flag.)
- **`MN_OTHER` is reached only by genuinely illegal / reserved encodings**
  (reserved size field `ss==3`, undefined group-4 forms, illegal MOVEQ with
  bit 8 set, illegal immediate-to-CCR/SR). No *valid* MC68000 instruction
  lands in `MN_OTHER`.

Synthetic sweep over all 65536 base opcode words (`synthetic_sweep.c`):

| metric | count | pct |
|---|---|---|
| decode to a real mnemonic | 61824 | 94.34% |
| decode to `MN_OTHER` | 3712 | 5.66% |
| validator says LEGAL (MC68000) | 60517 | 92.34% |
| validator says illegal/reserved/68020 | 5019 | 7.66% |
| real-mnemonic **and** LEGAL | 60517 | — |
| real-mnemonic but validator-illegal (decoder over-accepts) | 1307 | — |
| **`MN_OTHER` **and** LEGAL** | **0** | — |

The last row is the key proof: **no legal opcode decodes to `MN_OTHER`**, i.e.
the decoder+codegen cover the entire legal MC68000 opcode space. Every one of
the 83 mnemonics is reachable from some base opcode.

## Per-family matrix

Status legend: **REAL** = real semantics emitted; **REAL\*** = real but with a
documented conservative simplification (noted). "Emitted marker (non-real)" is
the textual string the codegen would emit *only* on an illegal/unsupported
sub-case (illegal EA, unsupported dynamic mode), not in the normal path.

| family / mnemonic | status | source (code_generator.c) | notes |
|---|---|---|---|
| MOVE | REAL | 2260 | MOVE.B→An & MOVE→imm/PCrel dst are illegal; screened by validator at discovery (`m68k_validator.c:64-71`) |
| MOVEA | REAL | 2304 | MOVEA.B illegal; validator flags `M68K_ILLEGAL_SIZE` (`m68k_validator.c:58-60`) |
| MOVEQ | REAL | 2232 | bit-8-set encoding → `MN_OTHER` (decoder `:711-713`) |
| MOVEM | REAL | 3112 | unsupported base EA → `0 /* MOVEM unknown EA */` (`:3190`) |
| LEA | REAL | 2317 | non-addressable EA → `0 /* cannot take addr of mode N */` (`:528`) / `unknown EA addr 7/N` (`:523`) |
| PEA | REAL | 2325 | same addr fallbacks as LEA (e.g. PEA An = mode 1) |
| LINK / UNLK | REAL | 3089 / 3101 | |
| EXG | REAL | 3625 | D-D / A-A / D-A forms |
| MOVE USP | REAL | 3309 | both directions |
| MOVE SR,<ea> / <ea>,SR | REAL | 3274 | both directions via `dst_is_ea`; SR masked to 0xA71F |
| MOVE CCR,<ea> / <ea>,CCR | REAL | 3290 | **mis-emit FIXED**; both directions; CCR masked to 0x1F |
| ADD / ADDA / ADDI / ADDQ | REAL | 2332 / 2337 / 2386 / 2348 | reserved size for ADD screened (`m68k_validator.c:78-82`) |
| SUB / SUBA / SUBI / SUBQ | REAL | 2393 / 2398 / 2442 / 2409 | |
| AND / ANDI | REAL | 2449 / 2474 | |
| OR / ORI | REAL | 2455 / 2467 | |
| EOR / EORI | REAL | 2461 / 2490 | |
| CMP / CMPA / CMPI | REAL | 2530 / 2546 / 2602 | |
| CMPM | REAL | 2565 | promoted out of `MN_OTHER` |
| ORI/ANDI/EORI #imm,CCR | REAL | 2504 / 2512 / 2520 | promoted out of `MN_OTHER` (Phase 7A) |
| ORI/ANDI/EORI #imm,SR | REAL | 2508 / 2516 / 2524 | promoted out of `MN_OTHER` (Phase 7A) |
| MULS / MULU / DIVS / DIVU | REAL | 3016 / 3003 / 3054 / 3029 | |
| TST / CLR / NEG / NEGX / NOT | REAL | 2666 / 2618 / 2634 / 3335 / 2650 | store-dst legality screened (`m68k_validator.c:87-95`) |
| EXT / SWAP | REAL | 2729 / 2717 | |
| TAS | REAL | 3379 | |
| BTST / BCHG / BCLR / BSET | REAL | 2673 / 2702 / 2707 / 2712 | static + dynamic forms |
| ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR | REAL | 2814 / 2752 / 2903 / 2961 | register-count, immediate-count, and memory (1-bit) forms |
| Scc | REAL | 3323 | |
| ABCD / SBCD / NBCD | REAL\* | 3463 / 3530 / 3591 | promoted from TODO (Phase 7B); register + predec-mem forms; sticky-Z; N/V conservative (undefined on HW) |
| ADDX / SUBX | REAL\* | 3647 / 3719 | both Dy,Dx and -(Ay),-(Ax) mem-predec (Phase 4); undefined flags emitted deterministically |
| MOVEP | REAL | 3397 | promoted from TODO (Phase 7C); alternating-byte transfer, .W/.L, both directions |
| CHK | REAL\* | 3353 | promoted from comment (Phase 7C); vector-6 trap; aborts loud if it trips |
| JSR / BSR | REAL | 1885 | static call + dynamic `recomp_call_addr`/`call_by_address`; unsupported dynamic EA → `/* TODO: dynamic JSR/BSR EA m/r */` (`:1962`) |
| JMP | REAL | 1980 | static + dynamic + `hybrid_jmp_interpret` for `(d8,PC,Xn)`; unsupported → `/* TODO: dynamic JMP mode m/r */ return;` (`:2095`) |
| BRA / Bcc | REAL | 2102 / 2122 | target-less form → `/* BRA with no target */` / `/* Bcc no target */` (shouldn't occur for valid decode) |
| DBcc | REAL | 2208 | |
| RTS / RTE / RTR | REAL | 1770 / 1786 / 1850 | RTR promoted (Phase 7A) |
| NOP | REAL | 1765 | |
| STOP | REAL\* | 1808 | sets `SR = imm`; models a halt path, not full halt-until-IRQ resume |
| TRAP | REAL | 1831 | `m68k_trap_vector` |
| TRAPV | REAL | 1841 | promoted (Phase 7A); vector 7 if V set |
| RESET | REAL | 1863 | promoted (Phase 7A); pulses device reset |
| ILLEGAL / A-line / F-line | REAL | 1875 | promoted (Phase 7A); `m68k_illegal_trap`, vectors 4/10/11; also `m68k_is_terminator` |
| (anything else) | `MN_OTHER` | 3786 | illegal/reserved encodings only → `/* unimplemented opcode $XXXX @ $YYYYYY */` + `CGD_MN_OTHER` |

## Non-real markers emitted into generated C (the grep targets)

These are the strings the codegen emits when it *cannot* produce real
semantics. `scan_generated.py` greps for exactly these.

| marker string | meaning | codegen line | routed to codegen_diag? |
|---|---|---|---|
| `/* unimplemented opcode $XXXX @ $YYYYYY */` | `MN_OTHER` (illegal/reserved opcode or data-as-code) | 3790 | yes (`CGD_MN_OTHER`) |
| `/* TODO: dynamic JSR/BSR EA m/r */` | unsupported dynamic call EA (e.g. JSR Dn = illegal) | 1962 | yes |
| `/* TODO: dynamic JMP mode m/r */ return;` | unsupported dynamic jump EA | 2095 | yes |
| `/* BRA with no target */ return;` | branch decoded without target | 2116 | yes |
| `/* Bcc no target */` | branch decoded without target | 2202 | yes |
| `/* cannot store to EA 7/r */` | store to invalid mode-7 EA (imm/PCrel/undefined) | 629 | yes (`CGD_INVALID_STORE_EA`) |
| `/* cannot store to mode m */` | store to invalid EA mode | 639 | yes |
| `0 /* cannot take addr of mode m */` | LEA/PEA/addr-of non-addressable EA (e.g. An) | 528 | **no — comment-only** |
| `0 /* unknown EA addr 7/r */` | addr-of unknown mode-7 sub-form | 523 | **no — comment-only** |
| `0 /* unknown EA 7/r */` | load from invalid mode-7 EA | 442 | **no — comment-only** |
| `0 /* unknown mode m */` | load from invalid EA mode | 447 | **no — comment-only** |
| `0 /* MOVEM unknown EA */` | MOVEM base EA unsupported | 3190 | **no — comment-only** |

**Finding:** five of the EA-fallback markers are *comment-only* and bypass
`codegen_diag` — so they escape the `--fail-on-unsupported` gate and the
end-of-run coverage summary. They are still data-as-code symptoms (illegal EA),
but the project's own visibility tooling does not count them. Closing that gap
(route them through `codegen_diag`) is a clean follow-up.

## EA-legality finding

- A centralized legality validator **exists** (`m68k_validator.c`) and **is
  consulted** — but only by `function_finder.c` (8 call sites) during static
  discovery, to terminate speculative scans on illegal encodings. **Codegen
  does not re-validate**; it emits whatever the (permissive) decoder produced.
- What the validator screens: MOVEA.B (illegal size), MOVE.B→An, MOVE to
  immediate/PC-relative dst, reserved-size ADD/SUB/AND/OR/EOR/CMP, illegal
  store-dst for CLR/NEG/NEGX/NOT/TAS/NBCD/Scc, `MN_OTHER`, and the 68020
  32-bit-displacement branch form (`d8==0xFF`, off by default).
- What it does **not** screen: JSR/JMP with register-direct/illegal EA (→
  `TODO dynamic JSR` markers), bit-op illegal dst, load-side illegal EA (mode
  7/5 etc.), MOVEP/CHK/LEA/PEA EA legality. These reach codegen unchecked.
- The decoder still *mis-classifies* MOVE.B with An destination as `MN_MOVEA`
  unconditionally (`m68k_decoder.c:242`), and tolerates the 68020 32-bit branch
  (`:688-693`). Both are caught downstream by the validator at discovery, but
  the decoder itself has no legality screen — it is permissive by design
  (`m68k_decoder.c:25` / `m68k_validator.h:4-12`).
- Consequence: when a function entry is supplied via disasm seeds / jumptable
  lists / extra-func config / runtime dispatch and points into **data**, the
  validator never saw it, so codegen emits the non-real markers. This is the
  source of the S3 / S3K / S&K exposure (see `scan_generated.py` output) — a
  **function-discovery / data-as-code** problem, not an instruction-semantics
  gap.
