# RKA oracle-parity plan (feat/rka-oracle-parity)

Strategy adopted 2026-06-21 after conferring with ChatGPT. Supersedes the
"keep the screenshot green" framing for RKA *gameplay* (see the project memory
`project_rka_oracle_parity`).

## Premise

RKA gameplay has never worked: the 934-function baseline is title-correct but
gameplay-garbled (object code no-ops). So there is no working-gameplay state to
protect — we were stuck in a title-screen local minimum. The garble is
diagnostic signal. We march **native↔clownmdemu oracle parity** instead of
chasing visual output, with the **tier-3 interpreter floor as the fallback** for
any computed-dispatch target not provably safe to native-compile.

## Policy

- **Protect** known-good subsystems (title render, boot, VDP/sound init, input,
  frame timing, the Sonic FP=0 detector gate). Title smoke test stays as a GUARD.
- **Allow controlled breakage** in the never-working subsystem (RKA gameplay).
- **Primary acceptance test is oracle-based**, not screenshot-based: did native
  match clownmdemu for *longer* after entering gameplay than the previous build?

## The classifier (fixes the Steps 3-4 regression)

`runtime-observed != FunctionEntry`. Runtime-observed proves an address is real
code, NOT that it is a callable ABI boundary. Route each runtime-observed
computed target:

| Target | Action |
| --- | --- |
| RAM (`$FFB1F2`) | tier-3 trampoline only — never a static native function |
| Interior to an existing function | **tier-3 first**, NOT split-native (graduate to a native split landing pad only after proving split ABI) |
| True known function-start | native FunctionEntry |
| Unknown / unproven | tier-3 / defer (do NOT reopen broad speculative discovery) |

Split-ABI safety: a split is safe only if the tail reconstructs ALL live guest
state from canonical CPU state (D0-7/A0-7/SR/PC/memory/cycle), never from host C
locals in the head. Audit: reject/suspend a split whose tail branches back into
the head's pre-split region; route that target to tier-3 until supported.

## First pass (ChatGPT's exact sequence)

1. Resolve all 7 baseline dispatch misses by **runtime** evidence (the Step 1
   oracle already confirmed all 7 are real code).
2. RAM `$FFB1F2` → tier-3 / trampoline.
3. **All interior ROM targets → tier-3 first** (not split native).
4. Only true known function-starts → native FunctionEntry.
5. Run native vs clownmdemu with the same input script.
6. Stop at the **first divergence after gameplay entry**; capture it.
7. Fix **only** that divergence; repeat.

Bridge rule: *runtime target → native only if ABI-safe, otherwise tier-3.*
Later: replace tier-3 interiors with native split landing pads one at a time.

## March order

Native matches oracle through: one object dispatch → one object update loop →
one gameplay frame → N gameplay frames. **Smallest first target** = the first
runtime-observed computed target after gameplay entry where native diverges
(smallest, ROM, single source, few instructions, stable) — NOT the player object.

When native diverges, capture and classify: first divergent PC, native vs oracle
instruction, register/SR/memory/VDP diffs, last computed edge before divergence,
current object routine. Classes: wrong entry classification / bad split ABI /
incorrect 68k translation / bad flags-SR / bad memory-bus mapping / bad VDP
side-effect / RAM trampoline / scheduler-timing. **Chase the first CPU/memory
divergence, never the screen garbage (it is downstream).**

## Progress metrics (instead of screenshots)

dispatch misses · runtime targets resolved · tier-3 entries · first-divergence
frame · first-divergence PC · instructions matched after gameplay start ·
instructions matched after first object dispatch · VDP writes matched before
divergence. A visually uglier build is progress if first-divergence moves later
and more object dispatches complete.

## Build state on this branch

- Starting point: the Steps 1/2/5 work (oracle, audit, smoke) is committed on
  `feat/discovery-heuristics`; this branch inherits it.
- The Steps 3-4 recompiler changes (runtime_exec gate + jt_enumerate_jsr_word_table)
  are in the working tree and need REWORK: route interior targets to tier-3
  instead of `add_function`. `rka/generated` is reverted to the known-good 934.
- Next concrete step: implement the classifier (interior → tier-3, function-start
  → native, RAM → trampoline) + turn the tier-3 floor ON for RKA, then stand up
  the native↔oracle first-divergence harness.
