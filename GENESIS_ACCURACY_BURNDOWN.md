# GENESIS_ACCURACY_BURNDOWN.md

Living accuracy scorecard for **segagenesisrecomp** — the static MC68000→C recompiler
plus the clean-room "own backend" runner (VDP / bus / Z80 / YM2612 / SN76489).

Modeled axis-for-axis on the psxrecomp tomba2 worktree's `ACCURACY_BURNDOWN.md`
(`F:\Projects\psxrecomp\_wt-tomba2\psxrecomp`). Same 7 axes, same GREEN gate, same
always-on-ring philosophy, remapped from PSX hardware to Genesis/Mega Drive hardware.

- Branch: `accuracy/genesis-audio-oracle` (worktree `F:\Projects\segagenesisrecomp\_wt-accuracy`)
- Engine repo HEAD basis: `feat/rka-oracle-parity` @ f1e1082
- Status legend: **GREEN** (both legs of the gate pass) · STRONG · MODERATE · PARTIAL ·
  APPROXIMATE · SOFT-SPOT · NOT-MODELED. Only **GREEN** is a finished state.

---

## Method (non-negotiable)

Every item gets: a **status**, the **external comparative(s)** to cross-reference it
against, and a **validation method**. "Looks good" is NOT a status.

**The GREEN gate (two independent legs — both required):**

1. **Cross-referenced against a REFERENCE** — a hardware/spec source or independent
   emulator source, NOT our own code: the 68000 Programmer's Reference Manual, the
   Genesis Software/Hardware notes, BlastEm source, Genesis Plus GX source, a hardware
   test ROM. Cite the section/file per item.
2. **Runtime-validated against an ACCURATE ORACLE** — first-divergence on the relevant
   *state surface* (VRAM for VDP, **audio samples for FM/PSG**, cycle counts for timing,
   WRAM for control flow). The accurate oracle is **BlastEm** (cycle-accurate), with
   GPGX/clownmdemu as cheaper cross-checks and **Nuked-OPN2** as the chip-level audio
   ground truth.

> Self-agreement (recompiled == our own Tier-3 interpreter) proves **backend
> equivalence**, NOT correctness — both can be identically wrong because they *share the
> same decoder and semantics*. Backend equivalence is necessary, never sufficient.
> (Mirrors PSX `ACCURACY_BURNDOWN.md:30-35` / CLAUDE.md §15.)

**A research-claimed discrepancy is a HYPOTHESIS, not a bug.** Validate the *output*
(diff our result vs the oracle's on the same input) BEFORE changing code. The user's
ears / the oracle's output override source-reading. "Sounded right before, wrong after"
= revert, always. (Mirrors PSX `ACCURACY_BURNDOWN.md:14-18`.)

**Layered-parity caveat (this project's own hard-won lesson — `feedback_no_1to1_…`):**
raw full-state byte equality between a static recomp and an emulator oracle is a *doomed*
test — timing/sound/RNG scratch diverges early while the program is perfectly correct
(proof: RKA WRAM-hash diverged at frame 76 with correct visuals). So:
- **Exact CPU/state compare** is used ONLY same-backend (recompiled vs all-interpreted
  Tier-3 floor — the same deterministic program).
- **Semantic state-surface invariants at sync points** are used for recomp-vs-emulator
  (VRAM hash at vblank, FM/PSG sample stream, executed-PC set, cycle Δ between anchors).
  Never expect raw sample/RAM equality across binaries.

**Always-on ring buffers, never arm-then-capture** (CLAUDE.md global rule + PSX
`tools/divergence/README.md:16-17`). Probes QUERY an always-on ring for the window of
interest; they never `arm; run; dump`. If data is missing, EXTEND the ring, then query.

---

## Reference shelf

| Reference | Role | Notes |
|---|---|---|
| MC68000 Programmer's Reference Manual | instruction semantics + cycle counts | per-instruction cost truth |
| Sega Genesis Software/Hardware Manual + community VDP notes | VDP/DMA/IO/timing | nocash-equivalent for MD |
| **BlastEm** source (GPLv3, C) | **accurate oracle** (cycles + state + audio) | de-facto cycle-accurate reference; passes Nemesis VDP-FIFO test, CRAM dots, Overdrive 2 |
| Genesis Plus GX source (via **mdref**) | broad-compat state oracle | 100% commercial-game compat; per-frame WRAM diff already wired |
| clownmdemu source (`_oracle` build) | 2nd-opinion state + per-chip audio | deliberately high-level/approximate; no guest cycle counter |
| **Nuked-OPN2** (LGPL-2.1) | **chip-level FM audio ground truth** | die-accurate YM2612; fed the recomp's own register-write ring |
| clown68000 | per-instruction semantics oracle | already wired into the Tier-3 diff harness |

All oracles are **dev-only**, never shipped (same posture as the existing AGPL clownmdemu
`_oracle` build; preserves the PolyForm-Noncommercial release).

---

## Always-on ring inventory (what we can already query)

| Ring | Location (file:line) | Cap | Always-on | Captures |
|---|---|---|---|---|
| bus_ring | `glue.c:182` | 64 | yes | last 64 M68K bus accesses |
| frame_record (Tier-2) | `frame_record.h:177`, `cmd_server.c:288` | 600 frames | yes | full per-frame M68K+Z80+VDP+FM+PSG+WRAM + 256B game tail |
| reverse_debug Tier-1 (rdb) | `reverse_debug.c:44` | 1,048,576 | yes | every WRAM write (frame, vint#, addr, val, func, caller) |
| rdb fire ring | `reverse_debug.c:457` | 65,536 | yes | VBlank-fire events + reason class |
| oracle Tier-3 (t3) | `oracle_trace.c:31` | 1,048,576 | oracle build only | per-instruction pc+D0-7+A0-7+SR |
| crash block ring | `crash_report.c:30` | 64 | yes | recent function entries |
| **audio event queue** | `audio/event_queue.c:9-15` | 262,144 | yes | cycle-stamped FM/PSG writes |
| **audio delivery rings** | `audio.c:38-76` | 4096 + 256 | yes (Release too) | SDL queue depth + drop/underrun events |
| **[CHIP-TRACE]** | `chip_trace.c` | 262,144 | **GEN_DEV_TRACE only** | FM/PSG register-write stream w/ writer attribution — the shared native↔oracle tap |
| [SND-TRACE] | `genesis_machine.c:51` | 65,536 | GEN_DEV_TRACE only | sound-command lifecycle |

Dev traces ([CHIP-TRACE]/[SND-TRACE]/[DMA-STALL]/[IRQ-DEBT]/[VINT-MASK]) gate behind the
`GEN_DEV_TRACE` CMake option (default OFF; prod stubs report "not compiled in").
**Gap vs the global rule:** `[CHIP-TRACE]` — the single tap shared by both native and
`_oracle` builds and the basis for cross-build audio diffing — is NOT always-on in
Release. Promoting it (or a lean variant) to always-on is the first ring-extension lever.

---

## 7-axis summary table

| # | Axis | Verdict | Gap (one line) | Lever |
|---|---|---|---|---|
| 1 | 68000 instruction semantics | **STRONG — measured complete** | 83/83 mnemonics real codegen (0 stub/comment-only/mis-emit); synthetic 65,536-opcode sweep: 0 legal opcodes → MN_OTHER; real games: 0 unimplemented-instruction sites (707 flagged are all data-as-code → discovery problem). Old "stubs" verdict was a stale COVERAGE.md. Minor: EA-legality checked at discovery not codegen; 5 fallback markers bypass codegen_diag | route 5 markers through codegen_diag; refresh COVERAGE.md; prune data-as-code via executed-PC ring (Axis-6 follow-up) |
| 2 | Cycle / timing | **APPROXIMATE — MEASURED within ~1.3% vs die-accurate BlastEm (Z80 path)** | comparator BUILT (`tools/cycle_compare/`, 99.9% align, Δ ratio 0.9866); found Z80 DAC-loop constant(285)-vs-alternating(294/252) cost-averaging; 68K per-insn path not yet exercised (S1 boot+title is all Z80-origin); data-dependent MUL/DIV/shift still averaged | run comparator on a 68K-driven-audio window; fix Z80 DAC period; wire exact data-dependent costs |
| 3 | Interrupt / event timing | **SCANLINE (gen) / near-FRAME (V-int delivery)** | H-int genuinely per-scanline; V-int handler runs as one atomic lump at the vblank line (audio stamp-rebase compensates) | interleave the 68K V-int handler across chunk boundaries; validate take-point vs BlastEm |
| 4 | Memory map / MMIO | **APPROXIMATE** | per-access functional; HV counter approximate; status FIFO hardcoded empty; phantom-hblank toggle hack on each status read | real per-line H-counter + FIFO state; MMIO trace diff vs BlastEm |
| 5 | Peripherals / devices (VDP video, **FM**, **PSG**, Z80, DMA, IO) | **MIXED — weakest axis (audio focus)** | video scanline-accurate (no mid-line split); DMA transfer instantaneous (fill/copy charge nothing); **FM faithful (residual DAC-path, sub-audible); jump-SFX boop = `sn76489.c` noise channel missing the ÷2 output flip-flop — FIXED + measurement-validated (ours-vs-BlastEm now == clown-vs-BlastEm), pending user ear-test** | user ear-test S3/S1/S2 jumps; mid-line raster split; spread DMA over charged scanlines |
| 6 | Static-vs-dynamic recompiler fidelity | **STRONG (floor 0-div vs clown68000, default OFF)** | diff harness shares the decoder with the thing it tests (can't catch a decoder-level bug common to both); framed capsule can't run RAM-resident code | ship the planned free-running native-vs-oracle `oracle_block_diff.py`; RAM-code execution in the floor if needed |
| 7 | Determinism | **STRONG (deterministic; good headless trace)** | none material; cross-binary native-vs-oracle is non-bit-equal *by design* (layered-parity) | keep as invariant; `--hash-frames`/WRAM-FNV gate already exists |

Overall: **frame/scanline-accurate by design** with **per-instruction cycle accounting**
good enough to pace the frame, an **oracle-validated correctness floor** for recompiler
dispatch gaps, and a **deliberately approximated peripheral layer** (atomic V-int handler,
phantom H-blank, instantaneous-but-charged DMA) — the documented pragmatic trades that
keep the shipped Sonic titles correct without full mid-line raster modeling. **Axis 5
(peripherals/audio) is the declared weakest and the focus of the first oracle slice.**

---

## Axis 1 — 68000 instruction semantics

Status: **STRONG — MEASURED COMPLETE for legal opcodes (2026-06-28).** (Upgraded from
APPROXIMATE; that verdict was based on a stale `COVERAGE.md`.)

- [x] **Opcode-coverage matrix BUILT + synthetic sweep DONE** (`tools/opcode_coverage/`:
  `coverage_matrix.md`, `synthetic_sweep.c`, `scan_generated.py`). **83/83 mnemonics have a REAL
  codegen path — 0 stubs, 0 comment-only, 0 known mis-emits.** Synthetic sweep of all 65,536 base
  opcodes through the real decoder+validator: **0 legal opcodes decode to `MN_OTHER`** (legal MC68000
  opcode space fully covered); all 83 mnemonics reachable; `MN_OTHER` is reached ONLY by genuinely
  illegal/reserved encodings (reserved size ss==3, undefined group-4, bit-8 MOVEQ, illegal imm-to-CCR/SR).
- [x] **`COVERAGE.md` (repo root) is STALE** — predates Phase 4 + 7A/B/C. Everything it lists as a
  stub/`MN_OTHER`/mis-emit is now REAL codegen (retirements documented in `codegen_diag.h:16-24`):
  MOVEP, CHK, ABCD/SBCD/NBCD, mem-form ADDX/SUBX, RESET, TRAPV, RTR, CMPM, imm-to-CCR/SR are real;
  `MOVE CCR,<ea>` mis-emit is FIXED (`code_generator.c:3290-3305`, both directions). — *Follow-up: refresh
  COVERAGE.md (recompiler-repo doc, out of accuracy scope).*
- [x] Flags computed with explicit per-size formulas; ADD widens to 64-bit for carry
  (`code_generator.c:679-692`); a real ADD-flag bug here was caught by the L3 oracle on
  `Hud_TimeRingBonus` (`:676-678`). Conservative-but-real (`REAL*`): ABCD/SBCD/NBCD/ADDX/SUBX emit
  deterministic undefined N/V + correct sticky-Z; CHK traps loud; STOP sets SR (no halt-until-IRQ model).
- [x] Tier-3 interpreter mirrors codegen semantics by construction, differentially gated vs clown68000
  (`runner/tests/m68k_interp_diff.c:1-20,36-56`).
- [x] **Real-game exposure measured (`scan_generated.py`): 707 non-real sites total, but NONE is an
  unimplemented valid instruction** — all are data-as-code (MN_OTHER words = printable ASCII, e.g. the
  `SEGA GENESIS` header at $000102; or illegal-EA forms only reachable from data). **S1 = 0 (clean),
  S2 = 7 (one func `func_0087DC`, illegal EA from data); S3/S3K/S&K clusters live in data regions
  ($04xxxx/$1Fxxxx/$24xxxx).** → This is a **function-discovery / data-as-code problem (Axis 6 / the
  discovery pipeline), NOT an instruction-semantics gap.** — *Confirm live-vs-dead via the runtime
  executed-PC ring (RKA-style); prune the data regions from discovery.*
- [ ] **EA-legality screened at DISCOVERY only, not at codegen.** A centralized validator
  (`m68k_validator.c`) is consulted by `function_finder.c` (8 sites) to stop speculative scans on illegal
  encodings, but **codegen never re-validates**, and the decoder unconditionally classifies `MOVE.B`→An
  as `MN_MOVEA` (`m68k_decoder.c:242`) / tolerates 68020 32-bit branches (`:688`) — both caught downstream
  at discovery, not in the decoder. It doesn't screen JSR/JMP/bit-op/MOVEP/CHK/LEA/PEA/load-side illegal
  EAs. — *Minor; only matters for data-as-code, which discovery already gates.*
- [ ] **5 EA-fallback markers bypass `codegen_diag`** (`cannot take addr`, `unknown EA addr 7/r`,
  `unknown EA 7/r`, `unknown mode m`, `MOVEM unknown EA`) — comment-only, so they escape
  `--fail-on-unsupported` + the end-of-run summary. — *Clean follow-up: route them through `codegen_diag`
  (recompiler change, deferred).*

Lever: instruction semantics is essentially DONE. Remaining (all minor/deferred): route the 5 fallback
markers through `codegen_diag`; refresh the stale `COVERAGE.md`; the 707 data-as-code sites are a
discovery/Axis-6 follow-up (confirm dead via executed-PC ring), not semantics.

---

## Axis 2 — Cycle / timing

Status: **APPROXIMATE — now MEASURED within ~1.3% vs die-accurate BlastEm (Z80 path); 68K path
not yet exercised.**

- [x] **BlastEm Δ-anchor cycle comparator BUILT + first slice run (2026-06-28).** Extended the
  BlastEm oracle with an always-on register-WRITE ring tagged with monotonic `abs_cycle` (hooks:
  `psg.c:54` psg_write; `ym2612.c:542/550/682` addr-part1/part2/data — `tools/blastem/oracle_ring.{c,h}`,
  dump `<prefix>.writes.bin`). Comparator `tools/cycle_compare/cycle_delta_compare.py` aligns our
  `chip_ring.txt` writes against BlastEm's writes (LCS, **99.9% matched**, one contiguous block) and
  compares the **Δ-cycle between consecutive writes** (offset-independent). Cycle domains reconciled:
  both 68000 master cycles, **scale = 1.0** (our `mc=` is per-wall-frame `g_audio_cycle_counter*7 +
  g_68k_stamp_rebase` for 68K-origin / `machine_z80_stamp()` for Z80-origin; BlastEm `abs_cycle` folds
  every `sync_components` rebase). RESULT (S1 boot+title): **overall recomp/BlastEm Δ ratio = 0.9866**
  (~1.3% short), median per-interval 0.983, 48,115/54k intervals within ±7%, none beyond +25%; gross
  frame calibration 1.015 (within ~1.5%). Our pacing is faithful to die-accurate within ~1.3%.
- [x] **Localized inaccuracy (the predicted "averaged data-dependent cost" class):** the **Z80 DAC
  playback loop** (FM `$2A` writes) emits a *constant* 285-cycle period in our recomp, but the real
  Z80 loop *alternates* 294 (ratio 0.969) / 252 (ratio 1.131) — a branch-dependent cost flattened to a
  constant. Music FM writes: uniform ~1.7% undercount (ratio 0.983). — *This is in the **Z80** timing /
  `machine_z80_stamp` derivation, not the 68K.*
- [ ] **68K per-instruction cost path STILL UNMEASURED — chip writes can't anchor it (2026-06-28).**
  Both slices' chip writes are 100% Z80-origin: S1 boot+title (pcz $00C5/$00C8) AND the Sega scream
  (the "SEGA" PCM is a normal SMPS DAC sample played by the **Z80**, not a 68K routine — 27000 `$2A`
  writes, all pcz $00C5/$00C8, zero 68K `$FFFF`; scream slice just re-measured the Z80 path at 0.98655
  with the same constant(3300)-vs-bimodal{3318,3360} stamp signature). Root reason: **in SMPS games the
  Z80 does ~all chip I/O; the 68K barely writes the chips** (only sparse `pcz=$FFFF` FM-register writes,
  ~617 in the title window — not a dense loop). (The dev-trace `SND_TRACE_START_FRAME 90u` gate,
  `chip_trace.c:35` / `genesis_machine.c:40`, is GEN_DEV_TRACE-only and did NOT block anything.) So the
  chip-write comparator validates **Z80 timing + `machine_z80_stamp` + frame scheduling** (~1.3%) but
  cannot reach the 68K. — *To measure the 68K cost path, two real options (both a lift, neither needs
  changing shipped behavior): (A) **68K-driven VDP-write Δ-anchor** — the 68K's dense output IS VDP
  writes (VRAM/CRAM/VSRAM/sprite/scroll); add a VDP-write ring (addr/data/cycle/pc) on our side + hook
  BlastEm's VDP write path with abs_cycle, then the same offset-independent Δ diff. (B) **Synthetic
  per-instruction microbench** — our 68K costs are sourced from clown68000 at codegen (`cycle_probe.c`),
  so validate that model directly: loop each instruction (esp. data-dependent MUL/DIV/shift) on BlastEm,
  measure cycles via abs_cycle, compare to the clown68000 cost — the PSX instruction-cost-matrix analog,
  targets exactly the flagged averaging. DEFERRED pending user call (anti-thrash).*
- [x] Generated code emits per-instruction `g_cycle_accumulator += N;
  g_audio_cycle_counter += N; if (… >= g_vblank_threshold) glue_check_vblank();`
  (`code_generator.c:949-956`, emitted `:1780`).
- [x] Cost source = exact per-instruction cost probed from **clown68000 at codegen time**
  (`cycle_probe.c:1-17`, `code_generator.c:1629-1637`), PRM-table fallback
  `estimate_cycles_prm` (`:1463-1620`).
- [x] Budget drains by real elapsed 68K cycles; per-scanline chunk `M68K_PER_LINE=488`
  (`genesis_machine.c:271,362`; `glue.c:602-630`); interrupt-handler cycles become raster
  debt `s_irq_cycle_debt` paid before each chunk (`glue.c:536-595,769-780`); DMA freeze
  charged via `glue_charge_68k_stall` (`glue.c:632-640`).
- [ ] **Data-dependent costs averaged** — MULx/DIVx magnitude, register shift counts use
  a mid-range estimate (`code_generator.c:1409-1411`). — *Now measurable: run the comparator on a
  68K-driven-audio window and look for the alternating-vs-constant Δ signature (as found in the Z80 DAC
  loop). Then wire exact data-dependent MUL/DIV/shift costs the probe can already measure.*
- [ ] **No prefetch / bus-arbitration / sub-line contention model.** — the ~1.3% residual undercount is
  consistent with this (and with the Z80 DAC averaging). — *Validate: comparator on more windows.*

Lever: the **BlastEm Δ-anchor cycle comparator is built** (`tools/cycle_compare/`, offset-independent,
99.9% align, scale 1.0). Next: (1) run it on a 68K-driven-audio window to validate the 68K per-instruction
cost path (this slice was Z80-only); (2) fix the Z80 DAC-loop constant→alternating(294/252) period; (3)
wire exact data-dependent MUL/DIV/shift costs.

---

## Axis 3 — Interrupt / event timing

Status: **SCANLINE-ACCURATE generation; near-FRAME atomic V-int delivery.**

- [x] **H-int genuinely per-scanline**: `gvdp_begin_scanline()` decrements the H-int
  counter per active line, reloads `reg[10]`, fires `GVDP_IRQ_HBLANK` on underflow
  (`genesis_vdp.c:354-379`); delivered at the matching scanline boundary
  (`genesis_machine.c:374-394`).
- [x] **V-int level-trigger** correctly latched while masked (imask≥6) and delivered when
  the mask drops; a masked span across >1 vblank yields exactly one V-int
  (`glue.c:739,842,874-881`).
- [ ] **V-int handler runs atomically** at the vblank line rather than interleaved with
  mid-frame raster (`own_deliver_vint`, `glue.c:756-815`); the audio stamp-rebase
  `g_68k_stamp_rebase` exists precisely to correct for this lumping (`glue.c:766`). —
  *Ref: 68000 IRQ take-point = exact instruction boundary; BlastEm. Validate: compare
  exception-entry timing / first-write-after-vint cycle vs BlastEm.*

Lever: interleave the handler across chunk boundaries for true mid-frame raster (large
change; the rebase machinery exists because atomic delivery was the pragmatic choice).

---

## Axis 4 — Memory map / MMIO

Status: **APPROXIMATE** — per-access functional, not cycle-timed.

- [x] VDP ports individually modeled: data/control/HV decode (`genesis_bus.c:228-296`),
  control-port two-write FSM, register writes, address/code latch, auto-increment
  (`genesis_vdp.c:267-301`), VRAM odd-address byte-swap (`:114-119`), CRAM BGR9 mask
  (`:127-133`).
- [x] FM/PSG/Z80/IO routed (`genesis_bus.c:233-296`); FM/PSG writes pushed to the
  cycle-stamped audio event queue, not applied inline (`:287-296`).
- [ ] **HV counter approximate** — V in high byte, H derived from `in_hblank` flag only
  (`genesis_vdp.c:346-351`). — *Ref: VDP HV-counter tables. Validate vs BlastEm HV reads.*
- [ ] **Phantom H-blank hack** — `gvdp_read_control` toggles `in_hblank ^= 1` on every
  status read (`genesis_vdp.c:342`) so per-whole-line code polling the H-blank flag makes
  progress. Deliberate, not hardware. — *Validate: MMIO status-read trace diff vs BlastEm.*
- [ ] **Status FIFO bit hardcoded "empty"** (`genesis_vdp.c:330`). — *Validate vs BlastEm
  FIFO model (BlastEm passes the Nemesis FIFO test).*

Lever: real per-line H-counter + proper FIFO state removes both the phantom-hblank toggle
and the hardcoded FIFO-empty; build the `mmio_tally.py` analog + MMIO trace diff vs BlastEm.

---

## Axis 5 — Peripherals / devices  ← WEAKEST (audio focus this pass)

Covers VDP rendering, **YM2612 FM**, **SN76489 PSG**, Z80, DMA, controllers. Audio is the
declared focus of the first oracle slice.

### 5a. Video (VDP rendering) — **SCANLINE-ACCURATE**
- [x] Whole-line renderer: planes A/B + window + sprites with full Genesis priority order,
  shadow/highlight, per-line H-scroll, 2-cell V-scroll, interlace mode 2
  (`genesis_vdp.c:610-740`); sprite engine per line (`:462-557`); per-line H-scroll table
  indexing reproduces mid-frame parallax (`:646-651`).
- [ ] **No mid-line raster split** — register changes finer than one line not honored;
  the phantom-hblank toggle (axis 4) is the workaround. — *Validate: per-vblank VRAM/CRAM
  hash + framebuffer diff vs BlastEm; raster-effect test ROMs.*

### 5b. DMA — **APPROXIMATE**
- [x] 68k→VDP freeze timed in aggregate via raster-gated access slots
  (`genesis_vdp.c:180-199` → `gvdp_consume_68k_stall` `:208-214` → `glue.c:632-640`).
- [ ] **Transfer itself instantaneous** (`while(len--)`) for 68k→VDP / VRAM copy / VRAM
  fill (`genesis_vdp.c:168-259`); mid-DMA VRAM never observable; **fill/copy charge no
  stall** (only 68k→VDP does). — *Ref: VDP DMA timing. Validate: DMA-stall cycle Δ vs
  BlastEm; spread transfer across charged scanlines.*

### 5c. FM (YM2612) — **MEASURED vs die-accurate Nuked-OPN2: corr 0.999 / 1.13 cents**
- [x] ymfm (BSD-3) core, `runner/audio/ym2612_ymfm.cpp` behind the stable C API
  (`:4-6,115`); clock = master/7 = 7,670,453 Hz, output = clock/144 = **53,267 Hz**
  (`:39-41,159-164`); HW output low-pass matching clownmdemu coeffs (`:69-94,106-107`).
- [x] **YM_GAIN = 195/256** correction (`ym2612_ymfm.cpp:51-52`, applied `:104`) — fixed
  the +2.3 dB level boop (ymfm x21.005 vs reference x16). Synth_replay envelope
  correlation 1.000 (`:46-50`).
- [x] **GREEN-leg-1 (reference) NOW MET** — Nuked-OPN2 (LGPL, die-accurate YM2612) vendored
  at `tools/nuked-opn2/` (pinned 335747d) and replayed through the shared chip-write ring
  by `tools/synth_replay` (FM path added). On S3 title music (16.8 s, 255k FM writes,
  zero timing drift by construction): **ours(ymfm) vs Nuked-OPN2 envelope corr = 0.999;
  clownmdemu vs Nuked = 1.000** → ymfm is a faithful YM2612, and clownmdemu (the prior
  reference) is corroborated by the die-accurate core. Drift-tolerant metric
  (`tools/audio_drift_diff.py`): per-window xcorr **0.996**, onsets **98% matched** (median
  Δ 0.0 ms), per-note pitch error **median 1.13 cents / p90 4.07** (imperceptible).
- [x] **Residual ~8–13% post-alignment ATTRIBUTED (2026-06-28)** — `synth_replay --fm-channel
  N` / `--dac` (write-stream filtering applied identically to all three cores; valid because
  the YM2612 has no cross-channel modulation, and the silenced-channel DAC-ladder bias cancels
  in the residual since it's present on both sides). On the S3 title ring, the **DAC path
  dominates**: it carries ~the entire FM output energy (envRMS 973 vs full-mix 1034) and ~36%
  of the full-mix residual energy despite a modest 4.9% own residual — cause is ymfm's
  `dac_discontinuity` ladder vs Nuked's die-accurate time-multiplexed DAC. Next contributors
  ch0 (13.5%) / ch1 (7.7%, highest *per-unit* divergence at 8.2%); ch3/4/5 negligible (ch5
  unused — S3 uses ch6 as DAC). **Pitch ≤1.13 cents on every channel → residual is
  timbral/level, not frequency-math.** Outputs: `tools/synth_replay/_run_chan/{full,ch0..5,dac}/`,
  binary `build_acc_chan/`. (Bug fixed in passing: prior `--fm-channel` leaked DAC regs $2A/$2B
  into every per-channel render.) — *Lever: a die-accurate DAC reconstruction in the ymfm
  wrapper is the only remaining FM accuracy gain, and it is sub-audible (4.9% on the dominant
  source, imperceptible pitch). Treat as KNOWN-GOOD; do not chase below audibility.*
- [x] **Jump-SFX boop REPRODUCED + LOCALIZED (2026-06-28) — it is PSG, not FM** (see 5d). An
  in-game S3 capture (AIZ Act-1, real jumps via `--input-script`, ring f1753–2921) run through
  `synth_replay` + `audio_drift_diff.py` at 250 ms windows shows FM stays faithful at every
  jump (ymfm-vs-Nuked xcorr ~0.98, residual 10–23%), while the PSG (ours-vs-clownmdemu) goes
  to ~99% residual / xcorr ≈ 0 at exactly the jump-SFX onsets. The boop is the PSG noise
  channel, NOT the FM path. Artifacts: `tools/synth_replay/_run_s3_sfx/`. — *This re-points the
  long-standing S1/S3 "boop" investigation (was FM-framed) at the PSG noise model.*
  - Secondary: one isolated FM cluster at 16.0–16.5 s (residual up to 76%, xcorr 0.65) pulls
    FM pitch *mean* to 10.3 cents despite the 0.5-cent median — minor vs the PSG issue.

### 5d. PSG (SN76489) — **BUG ROOT-CAUSED: noise channel clocks LFSR 2× too fast (the jump "boop")**
- [x] Clean-room own core `runner/audio/sn76489.c` (replaced AGPL clownmdemu PSG); 3
  square + 16-bit LFSR noise, log volume table (`:60-63`); rate master/240 ≈ **223,721 Hz**
  NTSC (`:22,189-192`); own LPF matching clownmdemu coeffs (`:48-52,124-132`).
- [x] **Jump-SFX boop ROOT-LOCATED to the noise channel (2026-06-28).** In-game S3 capture
  (`_run_s3_sfx/`) per-window diff: every PSG-divergent window contains a channel-3
  **noise-control `$E7` write** (white-noise clocked by ch2's tone period); clean windows lack it.
- [x] **ROOT-CAUSED + ADJUDICATED via BlastEm `psg.c` (die-accurate-ish, 2026-06-28).** Vendored
  BlastEm's SN76489 into `synth_replay` as a 3rd chip-replay core (`tools/synth_replay/blastem_psg.{c,h}`,
  GPLv3 dev-only) and compared ours/clownmdemu/BlastEm on the IDENTICAL `$E7` register timeline
  (Layer A, zero drift). At the 11 jump windows: **ours-vs-BlastEm xcorr 0.233** (goes *negative*,
  −0.28/−0.21, at the worst onsets) vs **clown-vs-BlastEm 0.471** — i.e. BlastEm and clownmdemu
  AGREE and **ours is the systematic outlier** (not a seed difference, which would track clown).
  **BUG: our noise channel is missing the rising-edge ÷2 output flip-flop, so it advances the LFSR
  on EVERY counter expiry → noise an octave too high → the boop.**
  - ours `sn76489.c:114-121` — `if (--noise_counter<=0)` reloads and shifts the LFSR every expiry,
    no `output_state[3]` toggle gating the shift.
  - BlastEm `tools/blastem/psg.c:117-123` — expiry toggles `output_state[3]`; LFSR shifts only when
    true → ÷2. clownmdemu `clownmdemu-core/source/psg.c:212-214` — same ÷2 via `fake_output_bit`.
  - Both independent references implement the documented SN76489 behavior (LFSR clocks on the rising
    edge of the divider, i.e. ÷2); ours does not. Applies to ALL noise modes; for the ch2-clocked
    `$E7` case it doubles the noise rate.
  - Secondary (minor): output-tap phase — ours reads `lfsr&1` AFTER the shift (`:120`), BlastEm reads
    BEFORE the rotate (`psg.c:122`). The feedback tap (bit0^bit3, white) matches BlastEm.
  - **FIX APPLIED + MEASUREMENT-VALIDATED 2026-06-28 (pending user ear-test).** Added a `noise_ff`
    ÷2 flip-flop to the noise channel; the LFSR now shifts only on its rising edge (`sn76489.c`
    struct field + render gate). Rebuilt synth_replay (`build_acc_psg`) against the fixed core and
    re-ran the identical-ring 3-way compare. **The "ours is the outlier" signature is GONE:**
    whole-clip envelope corr ours-vs-BlastEm **0.526→0.954** (== clown-vs-BlastEm 0.953),
    ours-vs-clown **0.840→0.998**; per-`$E7`-window xcorr ours-vs-BlastEm **0.233→0.535** (now ≈
    clown-vs-BlastEm 0.518, was well below it). I.e. our PSG now tracks the die-accurate BlastEm
    reference exactly as well as clownmdemu does. Residual ~0.5/window = irreducible cross-core
    noise-phase + BlastEm unipolar output (common-mode to both cores). Applied to BOTH
    `segagenesisrecomp/runner/audio/sn76489.c` (build's copy via junction) and the worktree copy.
    Output-tap-phase (read-after vs read-before shift) left as-is — second-order, ambiguous, not
    needed. **NEXT: user ear-validates S3 in-game (one game at a time), then re-check S1/S2 jumps.**
- [~] **S1 jump-SFX boop — OPEN (Axis 3/5e); eliminated synth + delivery + register stream (2026-06-28).**
  The user's boop is specifically a SONIC 1 artifact. The S1 jump SFX is a **ch0 TONE sweep** (320→95 ≈
  350→1177 Hz), **zero `$E_` noise writes** → the ÷2 noise fix does not apply. Four-layer elimination:
  1. **Synth math — exonerated.** Offline 3-way replay of the native jump ring (`_run_s1_jump/`):
     ours-vs-clownmdemu envelope corr **0.999**, pitch 0.46 cents, 100% onsets; only a common-mode
     offset vs BlastEm (its selftest RMS ~3× lower = calibration, not a per-core bug).
  2. **Delivery path — exonerated.** Live delivery-ring probe (S1 `--port 4380`, `audio_delivery_dump`)
     during the user's deterministic every-jump boop: **0 new underruns/drops, 0 queue-zero dips,
     queue 47-55KB, dt_us steady ~16.7ms.** No splice/underrun. So NOT a delivery glitch (corrects the
     earlier delivery-path hypothesis).
  3. **Register-stream values + wall-frame timing — exonerated.** native-vs-oracle (clownmdemu backend)
     register diff at the jump: ch0 sweep values **byte-identical**, cadence **1 step/wall-frame in
     both**, no extra/missing/dup writes.
  4. **Residual difference (only one left):** a *constant* ~86%-of-frame intra-frame stamp PHASE —
     native re-stamps V-int SMPS writes to the vblank delivery cursor (end-of-frame) via
     `g_68k_stamp_rebase` (`glue.c:766`, consumed by `STAMP_68K()` `genesis_bus.c:35`); the clownmdemu
     oracle surfaces them at frame start. A *constant* sub-frame shift of a steady sweep is inaudible.
  **Contradiction → premise likely stale.** If registers + synth are identical, native and oracle
  should sound the same; the "native boops / oracle clean" A/B is **17 days old** (pre-declown /
  pre-own-backend-rework) and was NOT re-verified on current builds. — *USER DECISION 2026-06-28: STOP
  chasing; log as the open Axis 3/5e lever. To resume: (a) re-establish ground truth — live ear A/B of
  current oracle vs native GHZ jump; if current oracle also boops, the premise is stale (boop may be
  hardware-faithful / a shared render trait). (b) If still real: the atomic-V-int handler (writes
  bunched at vblank, `own_deliver_vint` glue.c) is the prime Axis-3 suspect — interleave it across
  chunk boundaries for true mid-frame timing; or window-aligned live-WAV spectral diff native vs oracle
  to catch a live-mixer/device-path difference the register replay can't see.* See
  [[project_s1_boop_delivery_rootcause]].
- [ ] **Sub-sample leftover discarded per frame** to match clownmdemu's per-Iterate reset
  (`sn76489.c:194-197`) — secondary to the noise-channel bug. — *Validate: PSG sample diff vs BlastEm.*

### 5e. CPU↔audio sync — **sample-accurate w.r.t. writes, drained per wall-frame**
- [x] Every FM/PSG write pushed as cycle-stamped `AudioEvent` (`event_queue.h:27-34`);
  68K stamp `g_audio_cycle_counter*7 + g_68k_stamp_rebase`, Z80 raster stamp
  (`genesis_bus.c:287-374`).
- [x] `audio_mixer_drain()` once per wall-frame: FM addr/data pair guard, stable sort by
  (stamp, push index), **advance each chip between writes** before applying (the
  architectural boop/squelch fix), tail-advance to frame end (`mixer.c:80-171`).
- [x] Multi-frame burst spreading (Sega-scream ~118k DAC writes / ~129 frames deferred &
  re-queued, `mixer.c:91-121`).

### 5f. Output / resample — **drift-servo SDL push**
- [x] SDL2 push mode at PSG rate (223,721 Hz), 4-frame cushion, dynamic-rate-control servo
  ≤±0.5% (`audio.c:87-258`); always-on delivery rings `s_depth_ring[4096]` / `s_evt_ring[256]`
  (`audio.c:38-76`); WAV taps generated stream pre-servo at ratio 1 (`audio.c:189-198`).

### 5g. Controllers — *(unaudited this pass)* — *Validate: input trace diff vs BlastEm.*

Lever (axis-wide): **per-chip sample-stream diff** is the GREEN lever for audio — see the
audio-first comparison plan below.

---

## Axis 6 — Static-vs-dynamic recompiler fidelity

Status: **STRONG** — Tier-3 floor 0-divergence vs clown68000, default OFF.

- [x] Tier-3 interpreter `m68k_interp.c` reuses the recompiler's decoder, mirrors codegen
  semantics, HALTs loudly on anything unimplementable (precision over recall)
  (`m68k_interp.h:11-37`).
- [x] `genesis_log_dispatch_miss` funnels every computed-dispatch miss into the A7-neutral
  framed capsule `m68k_interp_run_framed` (`glue.c:1958-2040`); a capsule exit ≠ native
  loose-A7 return is UNSAFE_EXIT → `floor_unsafe.log` + decline (`glue.c:2026-2037`).
  Default OFF, opt-in `GENESIS_FLOOR` (`glue.c:1936-1956`).
- [x] Same-backend differential `m68k_interp_diff.c` vs clown68000 (per-insn register
  trace + final-RAM compare) (`:1-20,36-56`).
- [ ] **Diff harness shares the decoder** with the thing it tests — can't catch a
  decoder-level bug common to both. — *Validate: independent reference (BlastEm decode).*
- [ ] **Framed capsule decodes only from the ROM image** — RAM-resident computed targets
  declined unless they trampoline back to ROM (`glue.c:1998-2008`). — *Lever: ship the
  planned free-running native-vs-oracle `oracle_block_diff.py`; add RAM-code execution if a
  target game needs it.*

> Note (gate discipline): recompiled == Tier-3 floor proves backend equivalence only.
> Correctness still requires the BlastEm/clown68000 leg.

---

## Axis 7 — Determinism

Status: **STRONG** — deterministic given same input; strong headless trace.

- [x] No host RNG / wall-clock in the sim path (`time()` only in `crash_report.c:284` log
  stamp); getenv toggles resolved at boot, not per-frame.
- [x] Fixed scheduler constants `LINES_TOTAL=262`, `MASTER_PER_LINE=3420`,
  `M68K_PER_LINE=488`, `Z80_PER_LINE=228` (`genesis_machine.c:269-273`); machine zeroed at
  init (`:220`); save states are raw struct images + pointer re-wire (`:235-249`).
- [x] Headless trace: `--input-script` + `--hash-frames N` `[FBHASH]`; `zone_smoke.py` /
  `boot_smoke.py` FNV1a-64 WRAM hashing (`DEBUG.md:144-251`); audio path engineered
  bit-deterministic across runs (`audio.c:190,234`).
- [ ] Cross-binary native-vs-oracle is non-bit-equal **by design** (layered-parity) — this
  is correct, not a defect.

Lever: none material; keep determinism as a maintained invariant (it is the precondition
that makes the Layer-1 frame-fingerprint comparison valid at all).

---

## Audio-first comparison plan (drift-tolerant)

The residual jump-SFX boop now lives where every *measurable* stage already matches the
reference, so the next move is offline **sample/spectrogram diffing at the SFX sweep
windows** with a metric that tolerates timing drift. Three layers, cheapest first:

**Layer A — chip-replay parity (no system timing; isolates "is my synth math right").**
`synth_replay` already replays the identical `chip_ring.txt` register timeline through OUR
synths (ymfm + sn76489.c) AND clownmdemu's `fm.c`/`psg.c` through the same mixer →
`ours.wav`/`theirs.wav`/`diff.wav` (`tools/synth_replay/synth_replay.cpp:1-49`). **Extend
it to add a Nuked-OPN2 path** (die-accurate FM truth) and a BlastEm-`psg_run` path (PSG
truth). Because the register timeline is identical by construction, there is *zero* timing
drift — any divergence is pure synth-core difference. This is the sharpest probe for the
current boop.

**Layer B — whole-pipeline drift-tolerant diff (end-to-end).**
Native `--wav` vs oracle (`--audio-backend=clownmdemu --wav`, and later BlastEm `--wav`)
at the common 223,721 Hz. Align with the VINT axis (`tools/wav_vint_align_diff.py` already
resamples both onto a common vint axis via `--framelog fcnt=` and reports 2–6 kHz SFX-band
RMS — the closest existing drift-tolerant tool). **Add the three drift-tolerant metrics:**
- **Cross-correlation alignment** — find the lag that maximizes normalized cross-correlation
  per window; report residual after best-lag alignment (neutralizes constant lead/lag, the
  audio analog of the cycle-axis Δ trick).
- **Onset-timing histogram** — detect note/SFX onsets on both streams, histogram the
  onset-time deltas; a tight zero-centered histogram = good timing, a shifted/bimodal one =
  the defect signature.
- **Per-note pitch error** — per detected note, estimate fundamental on both; report cents
  error. Catches LFSR/divider/sweep math bugs that RMS hides.

**Layer C — event-structure diff (drift-tolerant by alignment).**
Ring the FM/PSG **key-on/key-off/envelope** events frame-stamped on both backends; align on
the **first key-on** (`delta = recomp_first_kon.frame - oracle_first_kon.frame`), re-bin
recomp events by `frame - delta`, diff sorted `(kind, voice)` sets per frame, classify
`rec-extra`/`rec-missing`/`divergent` (mirrors PSX `tools/_spu_ring_diff.py:87-126`). This
is the structural counterpart to the sample diff.

**First slice target: Sonic 3 / S3K** (user-chosen). Hold fixed during pitch/level
analysis: `YM_GAIN=195/256` (`ym2612_ymfm.cpp:51-52`), `s_psg_vol_div=8` (`audio.c:13`),
both LPF coefficient pairs (FM 6.910/4.910, PSG 26.044/24.044).

**Metric verdict:** bit-exact is NOT realistic for Genesis FM across binaries (ymfm vs any
other core differs at the LSB even when perceptually identical). The GREEN bar for audio is:
best-lag-aligned per-window residual below an audibility threshold + zero-centered onset
histogram + per-note pitch error within a few cents, cross-referenced against Nuked-OPN2
(FM) / BlastEm (PSG) and runtime-validated on the S3K stream.

---

## Oracle build plan (BlastEm hook + Nuked-OPN2)

Confirmed feasible (recon agent d, web-verified). All dev-only.

**BlastEm (cycles + state + audio reference) — BUILT 2026-06-28**, pinned hg changeset
`7650b0eb0fa838fe34372d7b19296490dcea5c27` (Pavone's canonical `retrodev.com/repos/blastem`,
NOT the libretro GitHub fork; v0.6.2+1118, no tagged release since 2019). Local-only at
`tools/blastem/` (gitignored), GPLv3 dev-only, never shipped/linked. Built on MSYS2 MINGW64
(`mingw-w64-x86_64-{gcc,make,SDL2,glew,pkgconf}`) with a `pcshim/gl.pc` + Windows source
overrides (`make blastem NET=net_win.o TERMINAL=terminal_win.o MEM=mem_win.o FONT=… CHOOSER=…`).
Instrumentation landed (always-on, armed at launch via env, NO arm-at-probe / NO pause):
- m68k cycle anchor — `genesis.c:630` (`oracle_cycle_base_add(deduction)` at the `sync_components`
  rebase) → monotonic `abs_cycle = base + local_cycle` survives BlastEm's periodic rebase
  (verified: 0 non-monotonic steps over 54.5M master clocks).
- FM tap — `ym2612.c:500` end of `ym_output_sample()`; PSG tap — `psg.c:174` in `psg_run()`.
- New `oracle_ring.{h,c}` (fixed-size, pow2, eviction-bounded). Query = env-gated file dump at
  exit (`BLASTEM_RING_DUMP=<prefix>` → `<prefix>.{fm,psg}.bin`, 16-byte entries
  `{u64 abs_cycle; i16 L; i16 R}`, monotonic → bisectable by window). Chose file dump over the
  GDB stub because `qRcmd` only runs while halted (= the forbidden pause-to-observe). Docs:
  `tools/blastem/BLASTEM_ORACLE_README.md`.
Build plan as originally scoped (now satisfied):
1. **Guest-cycle export** — read `m68k_context.current_cycle` (uint32_t) at frame/sync in
   the `genesis.c` step path. Caveat: BlastEm periodically rebases `current_cycle` to avoid
   overflow — read it *relative to a known sync point*, never as a long-run absolute (use
   Δ-between-anchors, same as the PSX cyc_watch method).
2. **Audio sample trace** — tap `ym2612.c::ym_output_sample()` (FM stereo + per-channel) and
   `psg.c::psg_run()` (PSG mono), or one layer down in `render_audio.*`
   (`render_put_stereo_sample`/`render_put_mono_sample`) keyed by `audio_source`. Push into
   an **always-on ring tagged with current_cycle**, queryable by window (NOT arm-then-capture).
3. **Server endpoint** — extend the existing GDB-remote stub (`-D`, TCP :1234) with custom
   `qRcmd` monitor packets, OR add a small dedicated TCP command server mirroring the
   recomp's own `cmd_server.c` `--port` convention (4380/4381…). Latter is cleaner.
4. **Pin a revision** — BlastEm has no stable release since 0.6.2 (2019); 0.6.3 is a live
   nightly. Pin the hooked commit so the hook doesn't rot.

**Nuked-OPN2 (chip-level FM ground truth)** — LGPL-2.1, two files `ym3438.c/.h`. Vendor
dev-only; the replay tool reads the recomp's existing register-write ring, calls
`OPN2_Write` at recorded cycle stamps, `OPN2_Clock` to advance, emits a golden sample
stream to diff against our `ym_output`. **SN76489 caveat:** Nuked-PSG is YM7101, NOT
SN76489 — do not use it for PSG; compare PSG against BlastEm's `psg_run` (or lift SN76489
from the heavier GPL Nuked-MD).

**Three-job oracle assignment:**
| Job | Oracle | Why |
|---|---|---|
| state / divergence | mdref/GPGX (cheap, already wired per-frame WRAM diff) → BlastEm when timing matters; clownmdemu 3rd opinion | broad-compat net + accurate edge cases |
| cycle / timing | **BlastEm** (`current_cycle` Δ-anchor) | only embeddable C emulator exposing a real guest cycle counter |
| audio | **Nuked-OPN2** (FM chip truth, fed our ring) + **BlastEm** (PSG + full-system) | chip replay isolates synth-math from timing; matches the always-on-ring philosophy |

---

## Open levers, prioritized

1. **(audio, P0) — DONE 2026-06-28.** Audio oracle slice stood up: Nuked-OPN2 FM path +
   drift-tolerant metrics + per-channel attribution + in-game SFX capture. FM = faithful
   (residual DAC-path, sub-audible); **boop localized to the PSG noise channel.**
2. **(audio/PSG, P0) — FIXED + measurement-validated 2026-06-28; PENDING USER EAR-TEST.** Added the
   ÷2 `noise_ff` flip-flop to `sn76489.c`; LFSR now shifts on its rising edge only. Re-measured:
   ours-vs-BlastEm 0.526→0.954 (== clown), per-window 0.233→0.535 — outlier gone. Remaining: user
   ear-validates S3 in-game, then S1/S2 jumps; commit (this submodule first, per #20) on user say-so.
3. **(ring, P0)** Promote `[CHIP-TRACE]` (or a lean variant) to always-on in Release so the
   shared native↔oracle audio tap satisfies the global ring rule.
3. **(cycle, P1)** BlastEm Δ-anchor cycle comparator (cyc_watch/cycle_compare analog).
4. **(recompiler, P1)** Ship `oracle_block_diff.py` free-running native-vs-oracle
   first-divergence detector (the one independent-decoder leg axis 6 lacks).
5. **(semantics, P2)** `MN_OTHER` hard-diagnostic counter + synthetic opcode-coverage matrix.
6. **(MMIO, P2)** Real per-line H-counter + FIFO state; retire phantom-hblank + hardcoded
   FIFO-empty.
7. **(DMA, P3)** Spread fill/copy DMA over charged scanlines; charge their stall.

---

## First audio slice — results (S3, 2026-06-28)

Deliverable (ii): the first real recomp-vs-accurate-reference audio comparison. Chip-level
(Layer A), zero timing drift by construction (both synths replay the SAME chip-write ring).

**Setup.** Built Sonic3Recomp with `GEN_DEV_TRACE=ON` (`build-trace/`), captured S3 title
music headless (`--max-frames 1500 --snd-dump-frame 1450 --wav`), giving a real
`chip_ring.txt` (255k FM writes, 16.8 s) from the recompiled runtime. Vendored Nuked-OPN2
(`tools/nuked-opn2/`, die-accurate YM2612, dev-only LGPL) and added an FM reference path to
`tools/synth_replay` so the ring replays through ymfm (ours), clownmdemu (prior reference),
and Nuked-OPN2 (die truth) through the identical mixer. Built the drift-tolerant metric
`tools/audio_drift_diff.py` (xcorr alignment + onset histogram + per-note pitch cents).

**Result (recomp ymfm vs Nuked-OPN2 die-accurate, S3 FM):**
| metric | value | reading |
|---|---|---|
| envelope correlation | **0.999** | content/timing match (clownmdemu vs Nuked = 1.000) |
| per-window xcorr | 0.996 (lag −0.04 ms, drift 0.08 ms) | no timing drift |
| onset match | **98%**, median Δ 0.0 ms, std 2.56 ms | note onsets aligned |
| per-note pitch error | **median 1.13 cents**, p90 4.07 | musically imperceptible |
| post-align residual | 8–13% of RMS | genuine cross-core waveform delta (DAC ladder) — the real FM lever |
| best-fit gain | ×21.7 | per-core level calibration (scale-invariant; not a defect) |

**Verdict:** ymfm is a faithful YM2612 for S3 — GREEN-leg-1 (die-accurate reference) met for
FM content/timing/pitch. Open: attribute the ~8–13% residual per-channel; capture an
SFX-window (jump) for the boop; PSG truth still pending BlastEm `psg_run` (PSG vs clownmdemu
corr 0.955). Whole-pipeline (Layer B) needs window-aligned captures — the native WAV spans
boot→1500 while the ring is the last 262k events, so a direct native-vs-ring WAV diff is
window-misaligned (not a fidelity finding); next is capturing the native WAV over the exact
ring window.

**New tooling (this slice):** `tools/nuked-opn2/{ym3438.c,ym3438.h}` (pinned 335747d),
`tools/synth_replay/synth_replay.cpp` (Nuked FM path + 3-way compare), `gen_fm_tone.py`
(integration self-test), `tools/audio_drift_diff.py` (drift-tolerant metric).

## Deferred backlog — revisit someday (low-value, parked by user 2026-06-28)

The substantive audio work is done (FM faithful; PSG ÷2 noise bug fixed + shipped). These
remaining items are real but low-value — each is sub-audible, cosmetic, or a parked bug with
a stale premise. Logged so they're not lost; **revisit someday**. Each carries enough to resume
cold without re-deriving.

1. **S1 jump-SFX boop — OPEN bug, premise likely stale (Axis 3/5e).** Eliminated as a cause:
   PSG synth (ours≈clown 0.999), delivery path (live rings healthy through every boop), and the
   register stream (native≡oracle values + wall-frame cadence). Only residual = a *constant*
   intra-frame stamp phase (`g_68k_stamp_rebase`, `glue.c:766`), inaudible for a steady sweep.
   The "native boops / oracle clean" A/B is 17 days old (pre-declown/own-backend rework) and was
   NOT re-verified on current builds. **Resume:** (a) live ear A/B of CURRENT oracle vs native GHZ
   jump — if current oracle also boops, the premise is dead (boop may be hardware-faithful);
   (b) else interleave the atomic V-int handler (`own_deliver_vint`) across chunk boundaries, or a
   window-aligned live-WAV spectral diff native-vs-oracle. See full detail in Axis 5d.
2. **Z80 DAC-loop cost-averaging (Axis 2 / 5e).** The Z80 DAC playback loop emits a *constant*
   285-cycle period between FM `$2A` writes; real hardware *alternates* 294/252 (branch-dependent).
   Found by the cycle comparator (`tools/cycle_compare/`). It's a timing-STAMP modeling detail in how
   the runner cycle-stamps interpreted-Z80 writes (`machine_z80_stamp`), NOT an interpretation limit
   (keeping Z80 interp — see the Z80-recompiler-option note in project memory). Sub-1.3% effect.
   **Resume:** make the Z80 write stamping reflect the loop's branch-dependent per-iteration cost.
3. **PSG sub-sample leftover discarded per frame (Axis 5d).** `sn76489.c:194-197` drops the
   sub-sample remainder each frame to match clownmdemu's per-Iterate reset — flagged as a possible
   long-run drift. **Resume:** PSG sample-stream diff vs BlastEm over a long run; carry the remainder
   if it drifts.
4. **FM DAC reconstruction (Axis 5c).** The ~8–13% ymfm-vs-Nuked residual is DAC-path-dominated
   (~36%): ymfm's `dac_discontinuity` ladder vs Nuked's die-accurate time-multiplexed DAC. Pitch is
   perfect (≤1.13 cents); this is timbral and sub-audible — KNOWN-GOOD. **Resume only if** a
   die-accurate DAC reconstruction is wanted in the ymfm wrapper; do not chase below audibility.
5. **PSG common-mode gain vs BlastEm (calibration).** Ours and clownmdemu agree but sit at a uniform
   gain offset from BlastEm (its volume table is /14, selftest RMS ~3× lower). Scale-invariant metrics
   already neutralize it; it's a reference-calibration note, not a defect. **Resume:** only if an
   absolute-level comparison vs BlastEm is ever needed.

## Changelog

- 2026-06-28 — Initial 7-axis recon + scorecard. Four parallel recon agents: (a) PSX
  template map, (b) per-axis posture (cited), (c) audio deep-dive (cited), (d) emulator
  landscape (web-verified). Oracle decision locked: BlastEm source-hooked + Nuked-OPN2
  chip truth. First audio slice target: Sonic 3 / S3K.
- 2026-06-28 — First audio slice landed (Nuked-OPN2 die-accurate FM reference + drift-
  tolerant metric). S3 FM: recomp ymfm vs Nuked corr 0.999, pitch 1.13 cents, onset 98%.
  ymfm validated as faithful YM2612; ~8–13% residual is the next FM lever. Axis 5c updated.
- 2026-06-28 — Three measurement tasks fanned out in parallel (no behavioral changes):
  - **FM residual attributed** (`--fm-channel`/`--dac` added to synth_replay, built into
    `build_acc_chan/`): the 8–13% ymfm-vs-Nuked residual is **DAC-path-dominated** (~36%, ymfm
    `dac_discontinuity` ladder vs Nuked die-accurate DAC), then ch0/ch1; pitch ≤1.13 cents
    everywhere → timbral/sub-audible, KNOWN-GOOD. Axis 5c updated.
  - **Jump-SFX boop reproduced + localized**: in-game S3 capture (`_run_s3_sfx/`) shows the
    boop is the **PSG noise channel** ($E7 ch2-clocked white-noise) vs clownmdemu (~99%
    residual / xcorr ≈ 0 at jump onsets), while FM stays faithful. Re-points the S1/S3 boop
    hunt from FM to the SN76489 noise model. Axis 5d updated → SOFT-SPOT. **Caveat: clownmdemu
    is approximate; await BlastEm `psg_run` before changing `sn76489.c`.**
  - **BlastEm source-hook oracle**: building in background (MSYS2 MINGW64) to provide the
    die-accurate PSG (`psg_run`) + cycle (`current_cycle` Δ-anchor) legs.
- 2026-06-28 — **BlastEm oracle BUILT** (pinned hg `7650b0eb0fa8`, Pavone retrodev.com; always-on
  FM/PSG/cycle rings keyed by monotonic `abs_cycle`, env-gated file dump — verified 0 non-monotonic
  steps / 54.5M clocks). **PSG boop ROOT-CAUSED**: vendored BlastEm `psg.c` into synth_replay as a
  3rd chip-replay core; ours-vs-BlastEm 0.233 vs clown-vs-BlastEm 0.471 at the `$E7` jump windows →
  BlastEm+clown agree, **ours is the outlier**. Bug = `sn76489.c` noise channel missing the
  rising-edge ÷2 output flip-flop (LFSR clocked 2× too fast → noise an octave high). Fix identified,
  NOT applied (behavioral; awaiting user go-ahead + ear validation). Axis 5d → ROOT-CAUSED.
- 2026-06-28 — **PSG ÷2 noise FIX APPLIED + measurement-validated** (user-approved). `sn76489.c`
  noise channel given a `noise_ff` ÷2 flip-flop; LFSR shifts only on its rising edge. Re-measured
  via the BlastEm-PSG chip-replay: ours-vs-BlastEm whole-clip corr 0.526→0.954 (== clown 0.953),
  per-`$E7`-window xcorr 0.233→0.535 (≈ clown 0.518) — outlier signature gone, our PSG now as
  accurate as clownmdemu vs the die-accurate reference. Pending user in-game ear-test. Axis 5d → FIXED.
- 2026-06-28 — **S1 jump boop investigation: eliminated synth + delivery + register-stream; OPEN as
  Axis 3/5e (user said stop & log).** S1 jump = ch0 tone sweep (no noise; ÷2 fix N/A). (1) synth
  exonerated (ours≈clown 0.999); (2) delivery exonerated (live ring probe: 0 underruns/drops at every
  deterministic boop, queue healthy) — corrects the earlier delivery-path hypothesis; (3) register
  values + wall-frame cadence native≡oracle (byte-identical); (4) only residual = constant inaudible
  intra-frame stamp phase (`g_68k_stamp_rebase`, glue.c:766). Contradiction: registers+synth identical
  ⇒ should sound identical, but the "native boops/oracle clean" premise is 17 days stale & unverified
  on current builds. Logged as open Axis-3/5e lever (re-verify premise via live ear A/B; else atomic-
  V-int interleave or live-WAV spectral diff). ÷2 fix is unaffected — already committed + ear-validated.
- 2026-06-28 — **Axis 2 first slice: BlastEm Δ-anchor cycle comparator BUILT + run.** Extended the
  BlastEm oracle with a cycle-stamped register-WRITE ring (psg_write + ym2612 write paths), rebuilt;
  `tools/cycle_compare/cycle_delta_compare.py` aligns our chip_ring writes vs BlastEm's (99.9%) and
  compares offset-independent Δ-cycles. S1 boot+title: recomp/BlastEm pacing ratio **0.9866** (~1.3%
  short), gross frame calib 1.015 — faithful within ~1.3%. Found a localized Z80 DAC-loop cost-averaging
  bug (constant 285 vs real alternating 294/252). CAVEAT: S1 boot+title writes are all Z80-origin, so
  the 68K per-instruction cost path is NOT yet exercised — next slice needs a 68K-driven-audio window
  (Sega scream / S2-S3). Comparator is game-agnostic. Axis 2 status → measured (Z80 path).
- 2026-06-28 — **Axis 2 68K-window slice attempted via Sega scream → NEGATIVE: scream is Z80-driven too.**
  The "SEGA" PCM is a normal SMPS DAC sample played by the Z80 (27000 `$2A` writes, all pcz $00C5/$00C8,
  zero 68K `$FFFF`), so it re-measured the Z80 path (0.98655, same constant-vs-bimodal stamp). Learned:
  in SMPS games the Z80 does ~all chip I/O, so chip writes can't anchor a 68K-cost measurement; the 68K's
  dense output is VDP writes. 68K cost path remains UNMEASURED — needs either a 68K-driven VDP-write
  Δ-anchor (new VDP rings both sides) or a synthetic per-instruction microbench vs BlastEm (validate the
  clown68000 cost model directly). Deferred pending user call. Frame-90 dev-trace gate confirmed harmless.
- 2026-06-28 — **Axis 1 first slice: opcode-coverage matrix + real-game exposure (STRONG result).** Built
  `tools/opcode_coverage/` (coverage_matrix.md, synthetic_sweep.c all-65536-opcode classifier,
  scan_generated.py). Result: **83/83 mnemonics real codegen, 0 stub/comment-only/mis-emit; 0 legal
  opcodes → MN_OTHER; 0 unimplemented-instruction sites in real games** (707 flagged sites all data-as-
  code: ASCII headers / illegal EA from data; S1 clean, S2 7, S3/S&K clusters in data regions). The old
  APPROXIMATE verdict was a stale COVERAGE.md — all listed stubs/MN_OTHER/mis-emits are now real
  (codegen_diag.h:16-24; MOVE CCR fixed code_generator.c:3290-3305). Axis 1 → STRONG. Minor follow-ups:
  EA-legality screened at discovery not codegen; 5 fallback markers bypass codegen_diag; refresh
  COVERAGE.md; confirm the data-as-code sites dead via executed-PC ring (Axis-6/discovery, not semantics).
