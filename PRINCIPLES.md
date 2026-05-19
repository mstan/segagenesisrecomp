# PRINCIPLES.md
Genesis Static Recompiler — Debugging & Reverse-Engineering Principles

This document is the SINGLE source of truth for all rules.
All other files reference this. Nothing overrides this.

The first 15 principles are the platform-agnostic core, ported verbatim
from `recomp-template/NES/PRINCIPLES.md`. Principles 16+ are
Genesis-specific addenda that encode lessons from this project's history.

---

# 1. CORE PHILOSOPHY

We do not guess.
We do not explore blindly.
We do not fix symptoms.

We identify:
1. The exact point of divergence
2. The exact state difference
3. The exact instruction or function responsible

Then we fix that — and only that.

---

# 2. STATE OVER THEORY

If two systems behave differently, then their state differs.

All debugging reduces to:
- Capturing state
- Comparing state
- Finding the first difference

Do not theorize causes without state evidence.

---

# 3. FIRST DIVERGENCE (CRITICAL)

Never debug the final symptom.

Always find:
> The FIRST moment where expected ≠ actual

If you are not identifying the first divergence, you are doing it wrong.

---

# 4. TEMPORAL DEBUGGING

Bugs are about WHEN, not WHAT.

Reason in time:
- What happened before this?
- What changed?

---

# 5. WRITE VS READ BUGS

Determine the class:

Write-time:
- Wrong data written

Read-time:
- Correct data, wrong usage

Do not mix these.

---

# 6. TRACE THE WRITER

When state differs, find:
- WHO wrote it
- WHEN it was written
- WHY it differs

---

# 7. FIX THE SOURCE

Invalid:
- Clamping
- Skipping logic
- Hardcoding values

Valid:
- Fixing the producing logic
- Fixing execution order
- Reproducing missing state

---

# 8. MINIMAL FIXES

The correct fix:
- Smallest possible change
- Matches original system behavior

---

# 9. STRUCTURED DATA ONLY

Use:
- Ring buffers
- Frame snapshots
- Timeseries

Avoid:
- printf spam
- unstructured logs

---

# 10. BUILD TOOLS, NOT GUESSWORK

If you cannot answer something:
→ build a tool to answer it

---

# 11. NEVER DEBUG BLIND

If you say:
- "maybe"
- "likely"

You are missing data.

Stop and gather it.

---

# 12. STUBS (ABSOLUTE RULE)

NO STUBS — EVER

If execution reaches unknown code:
1. STOP
2. Identify target
3. Fix discovery/codegen

Never simulate behavior.

---

# 13. FUNCTION DISCOVERY

A dispatch miss is a graph failure.

Fix:
- function finder
- codegen

Never patch output.

---

# 13a. DISPATCH MISS LOOP (MANDATORY)

Dispatch misses are SILENT GAME-BREAKING BUGS.
A miss means the game jumped to an address with no generated function.
That code never executes. The game skips entire subroutines.

**After EVERY game run (manual, scripted, or test):**

1. Check `dispatch_misses.log` next to the executable
2. If it contains entries: add them to `game.toml` `[functions].extra`
   (or supply via the disasm-driven `gen_disasm_*` pipeline; never hand-add
   from runtime feedback alone — see Principle 16)
3. Regenerate (`GenesisRecomp.exe <rom> --game game.toml`)
4. Rebuild and re-run
5. Repeat until `dispatch_misses.log` is empty

This is not optional. This is not a "later" task.
A game with dispatch misses is FUNDAMENTALLY BROKEN.

---

# 14. SUCCESS DEFINITION

A bug is fixed only when:
1. Root cause identified
2. Divergence explained
3. Fix addresses cause
4. Behavior matches reference

---

# 15. DISTRUST TOOLING

At the start of every session, validate that tools are doing what you
think they're doing. Run a known-good query, check the output by hand,
verify file paths resolve where you expect.

Never trust:
- That a previous session's tool still works the same way
- That generated output matches what you asked for
- That a grep/awk pipeline found all matches

When you build a new tool or instrument, verify its FIRST output manually
before relying on it for analysis.

---

# 16. DISASM IS GROUND TRUTH

Function discovery and jump-table inputs come from the disassembly source
(`s1disasm/`, `s2disasm/`, `skdisasm/`), not from runtime feedback.

Use the `gen_disasm_*` Python tools under `tests/tools/`:
- `gen_disasm_labels.py` — function labels
- `gen_disasm_jumptables.py` — JMP/JSR jump tables
- `gen_disasm_seeds.py` — entry-point seeds
- `gen_disasm_subs.py` — subroutine boundaries

Adding `[[extra_func]]` entries to `game.toml` based on
`dispatch_misses.log` alone is a FALLBACK — every entry must be cross-
checked against the disasm source. Runtime feedback can identify *that*
a function exists; only the disasm tells you *where it begins* and which
labels are interior vs standalone.

---

# 17. ALWAYS-ON RINGS, NEVER ARM-AND-RECORD

The runner has Tier-1 always-on ring buffers (bus_ring, frame_record,
reverse_debug, oracle_trace, crash_report — see `DEBUG.md`). They start
recording at boot and never stop.

Probes operate as: **connect → query backward window → analyze.**

Do NOT design probes that:
1. Connect to the runner.
2. Arm a trace filter (`rdb_add_range`, etc.).
3. Run a workload.
4. Dump the trace.

By the time the LLM finishes setting up step 2, the workload has often
already executed step 3 unobserved. The "I observed no events" conclusion
is a lie of omission.

If the data you need isn't in an existing ring, EXTEND THE RING (add the
new event class to the always-on capture path), then query. Do not work
around it with arm-and-attach.

**Pause/step is the same anti-pattern in disguise.** If you find yourself
writing "let me pause both, step them in lockstep, then read state" —
STOP. Use the rings.

---

# 18. NO PRINTF TELEMETRY IN HOT PATHS

stderr is reserved for:
- One-shot loud-abort messages (`[ILLEGAL]`, watchdog, `assert`-style aborts).
- Startup banner / TCP-port announcement.
- The `[NOTE]` channel for one-time configuration warnings.

stderr is FORBIDDEN in:
- M68K bus accessors (`m68k_read*`, `m68k_write*`).
- VBlank / HBlank handlers.
- Any per-instruction or per-block hook.
- Any per-frame loop body.

If you need to observe a value: add it to `g_pace_snap`, the frame-record
`game_data` tail, the bus_ring entry shape, or a Tier-1 entry. TCP
queries pull the structured data; printfs spam the terminal.

(The dead `runner/src/runtime.c` skeleton that violated this rule has
been removed; production runner code lives flat under `runner/`.)

---

# 19. NEVER EDIT GENERATED C

Files under `*/generated/` (e.g., `sonicthehedgehog/generated/sonic_full.c`,
`sonicthehedgehog2/generated/sonic2_dispatch.c`,
`sonicthehedgehog2/generated/sonic2_layout.c`) are RECOMPILER OUTPUT.

If something in generated code is wrong:
1. Fix the recompiler (`recompiler/src/`).
2. Regenerate (`GenesisRecomp.exe <rom> --game game.toml`).
3. Rebuild.

Hand-editing generated code is INVALID even as a temporary measure.

**One narrow exception** — local boot-smoke sanity testing (Wave 0B):
mutating one constant in a generated file to confirm the smoke flags the
divergence is acceptable for one run, but the change MUST be reverted
immediately and MUST NOT be committed or pushed. Prefer toggling a
runner env var when an equivalent toggle exists.

---

# 20. SUBMODULE COMMIT ORDER

There are three repos:
- `segagenesisrecomp` (submodule — shared engine + recompiler)
- `SonicTheHedgehogRecomp` (Sonic 1 release; consumes segagenesisrecomp as a submodule)
- `SonicTheHedgehog2Recomp` (Sonic 2 release; also consumes segagenesisrecomp)

When a change touches the submodule:
1. Commit `segagenesisrecomp` FIRST.
2. Bump the submodule pointer in the consuming release repo SECOND.
3. The two release repos can be bumped independently.

Never commit a release repo with a submodule pointer that doesn't exist
upstream.

---

# 21. PER-GAME DATA THROUGH SPEC + LAYOUT

Shared runner code reads:
- `g_game_spec.*` (function-pointer hooks — entry points, IRQ handlers,
  periodic callbacks, lifecycle hooks, debug commands, dispatch override,
  hybrid table). Defined in `runner/game_spec.h`.
- `g_game_layout.*` (per-game WRAM addresses — game_mode, vint_runcount,
  player_object, stacks, etc.). Defined in `runner/game_layout.h` and
  populated from `[ram_layout]` in `game.toml`.

Shared runner code MUST NOT contain:
- Literal Genesis WRAM addresses (`0xFF[0-9A-Fa-f]{4}` or `$FF...`).
- Per-game function names (`func_NNNNNN` referring to a specific game's
  ROM offset).
- Per-game compression / data-format magic ranges (NemDec, Kosinski,
  Enigma — these belong in `[memory_regions]` or per-game spec hooks).

When a new per-game knob is needed:
- A function-pointer hook → extend `GameSpec`, populate in each
  `*_spec.c`.
- A WRAM address → extend `[ram_layout]` in `game.toml` + `GameRamLayoutCfg`
  in the recompiler + `GameRamLayout` in the runner.
- A WRAM range → use `[[memory_regions]]` (Wave 1+) or pass through a
  spec hook.

The audit script `tools/audit_runner_purity.py` flags shared-runner files
that violate this principle.

---

# 22. FREE-RUNNING DIFFERENCE OVER PAUSE/STEP

Cross-binary divergence detection (native vs oracle) uses always-on
rings, not pause/step lockstep.

The canonical workflow (post-Wave 3):
1. Launch native + oracle on different TCP ports, both free-running.
2. Probe queries each binary's Tier-2 block-trace ring.
3. Find the first PC where (pc, D0-D7, A0-A7, SR) differ.
4. Report block index, frame, divergence.

The pause/step `divergence_diff.py` workflow is a FALLBACK for cases
where rings are too short to find a common anchor. It is not the default.

---

# 23. BOOT SMOKE BASELINE PROTOCOL

The `boot_smoke.py` tooling captures a deterministic snapshot of a known
configuration. Baselines live alongside the script
(`boot_smoke_baseline_sonic1.json`, `boot_smoke_baseline_sonic2.json`).

A baseline change MUST be reviewed in the same commit as the code change
that justifies it.

NEVER:
- Update the baseline alone (without a code change).
- Update the baseline retroactively to mask a regression.
- Treat a baseline-update commit as a rubber stamp.

There is no GitHub Actions / cloud CI for this project (the build needs a
ROM that can't be checked in upstream). Discipline here is local: run
`boot_smoke.py` before committing, and if you must update the baseline,
do it in the same commit as the code change that justifies it. The commit
message must explain *which* code change drives the new baseline.

---

# 24. ONE RUNTIME INSTANCE AT A TIME

Use `taskkill //F //IM <exe>` before relaunching. Stale background
processes silently bind TCP ports and produce stale ring data.

Verify with `tasklist | grep -i sonic` (or equivalent) before each
launch.

---

# 25. USER VERIFIES END-TO-END

A change is not "done" because:
- The recompiler builds.
- The runner builds.
- A test passes.

A change is "done" only when the user has confirmed end-to-end behavior
matches the reference (visual + runtime + audio where relevant). Do not
claim "no regression" or "fix worked" without their confirmation.

This applies to silent fixes too — even a "trivial" docs edit is not
verified until the user has pulled it.
