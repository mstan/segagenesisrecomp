# RKA development log

Investigation notes for the Rocket Knight Adventures (Konami, 1993) bring-up.
Running ROM is USA/NTSC (`rka.bin`, MD5 `ce0fde21d2c418b24c3d904e57df466e`,
1 MB, no SRAM, entry PC `$000208`). The EU dump is present but deferred.

## 2026-06-21 — discovery-miss triage, over-discovery regression, oracle gap

### Symptom

Baseline RKA "renders but no gameplay": backgrounds and palette draw, but
object/sprite logic is dead. Computed dispatches land on addresses that were
never registered as function entries, so the recompiled dispatcher silently
no-ops instead of running the object code.

### Authoritative baseline measurement

The miss log (`runner/glue.c` opens it in append mode, `fopen(...,"a")`) spans
builds — stale pre-detector entries pollute any triage that reads it without
clearing first. **Always delete `dispatch_misses.log` and
`interior_label_misses.log` before a measurement run.** A fresh 4000-frame
headless turbo run on the 934-function baseline yields **7 distinct miss
addresses over 11,745 fires**:

| Miss addr | Producer / shape |
| --- | --- |
| `$005F2E`, `$005F42` | `jsr $5f12(pc,d0.w)` @ `$005F08` — JSR word-offset call table at `$005F12` (4 entries `$005F1A/2E/42/5E`, each a clean `rts` subroutine) |
| `$00C7A6` | `jsr` @ `$00C6F0`, JSR word-offset table base `$00C74C` |
| `$02FE20` (idx 83), `$012F8A` (idx 101) | entries 64–390 of the 391-entry long-pointer master object table at `$0023D8` (`lea $23D8,a0; movea.l (a0,d0.w),a0; jmp (a0)`) — truncated by `JT_LONG_MAX_ENTRIES=64` |
| `$008992` | 1-entry long pointer at `$00898E` |
| `$FFB1F2` | RAM-resident computed-jump target (WRAM) |

`triage_misses.py` is a capstone literal-oracle triage: for each interior-label
miss it walks the ROM, finds the producing dispatch site, and classifies its
shape (PC-indexed word table, Duff's-device run, or two-step A-register
indirect). Use it to attribute misses to producers rather than guessing.

### Two candidate recompiler fixes (saved, NOT applied)

`rka_discovery_attempt.patch` (against `recompiler/src/function_finder.c`):

1. **`jt_enumerate_jsr_word_table()`** — enumerate `jsr (d8,PC,Xn.W)`
   self-relative word-offset call tables. The existing `MN_JMP` path seeded
   PC-indexed tables; the `MN_JSR` path never did, so the Konami object-dispatch
   idiom (`move.w tbl(pc,Dn),Dn; jsr tbl(pc,Dn)`) left its case bodies
   undiscovered. Targets are gated by a tight self-relative window
   (`JT_PCRELW_WINDOW = 0x800`) + even + in-ROM + legal-decode.
2. **Raise `JT_LONG_MAX_ENTRIES` 64 → 1024** — the 391-entry master object
   table at `$0023D8` was truncated at 64. The per-entry gate (non-null, even,
   in-ROM, legal-decoding pointer) self-terminates at the true table end, so the
   cap is only a runaway backstop and must merely exceed any real table.

Result of applying both: functions 934 → 4545, misses 7 → 0.

### Why "0 misses" was a FALSE summit (the regression)

Applying the patch produced a **user-confirmed visual regression**: the
previously-working title screen became garbled. Root cause is the missing code
oracle. RKA has no disasm, so `code_addrs_file` is unset, so
`game_config_is_known_code()` (`recompiler/src/function_finder.c`, the
`add_function` admission gate) returns `true` unconditionally — every
speculative target is admitted. Aggressive discovery then promotes **data bytes
and mid-instruction addresses** to function entries, corrupting codegen.

**Lesson: dispatch-miss count is not a success metric.** Over-discovery can
drive misses to zero while breaking rendering. Visual/behavioral correctness is
the real metric, and the user is the visual oracle.

### Conclusion — the oracle gap

Heuristic tuning measured against RKA's miss count is structurally doomed: with
no ground-truth code-address set, false positives are unmeasurable, so any
change that lowers misses cannot be distinguished from one that corrupts
codegen. The patch is therefore parked.

The validated path forward:

1. Build a discovery-precision harness against games that **do** have a disasm
   oracle (Sonic 1/2/3/S&K). Generate the full code-address set from the
   disasm `.lst` (`tests/tools/gen_code_addrs.py`), run the finder ungated, and
   report per game **False Positives = discovered − oracle** (drive to 0) and
   **Recall = |discovered ∩ oracle| / |oracle|** (push up).
2. Improve the finder heuristics (the two ideas above + structural FP-proofing:
   no mid-instruction targets, no function overlap, self-terminating table
   gates). Accept a change only if FP stays 0 across all Sonic oracles and
   recall does not drop.
3. Re-apply the proven heuristics to RKA (now safe) and re-measure both misses
   AND visual correctness.

This work lives on `feat/discovery-heuristics` (branched from
`feat/tier3-interp-floor`, which carries the two-step detectors in commit
`72a5fd5` that `master` lacks).

### Artifacts in this directory

- `triage_misses.py` — capstone literal-oracle miss→producer triage.
- `rka_discovery_attempt.patch` — the two-detector change (reverted from
  `function_finder.c`; kept here for re-application once proven FP-free).
- `interior_label_misses.log` — raw miss log (untracked; append-mode, clear
  before each measurement).
