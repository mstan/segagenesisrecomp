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
| 1 | 68000 instruction semantics | **APPROXIMATE** | broad family coverage; known stubs (MOVEP, CHK, mem-ADDX/SUBX, RTR, CMPM, CCR/SR imm), no EA-legality layer; permissive decoder buckets unknowns to `MN_OTHER` | hard-diagnostic counter on `MN_OTHER`/comment-only emits; legality matrix; fill stubs; synthetic opcode-coverage matrix vs BlastEm/clown68000 |
| 2 | Cycle / timing | **APPROXIMATE** | per-instruction cost (clown-probed), paces the frame but not bus-cycle-exact; data-dependent MUL/DIV/shift averaged; no prefetch/bus-contention | wire exact data-dependent costs; BlastEm `current_cycle` Δ-anchor comparator (cyc_watch analog) |
| 3 | Interrupt / event timing | **SCANLINE (gen) / near-FRAME (V-int delivery)** | H-int genuinely per-scanline; V-int handler runs as one atomic lump at the vblank line (audio stamp-rebase compensates) | interleave the 68K V-int handler across chunk boundaries; validate take-point vs BlastEm |
| 4 | Memory map / MMIO | **APPROXIMATE** | per-access functional; HV counter approximate; status FIFO hardcoded empty; phantom-hblank toggle hack on each status read | real per-line H-counter + FIFO state; MMIO trace diff vs BlastEm |
| 5 | Peripherals / devices (VDP video, **FM**, **PSG**, Z80, DMA, IO) | **MIXED — weakest axis (audio focus)** | video scanline-accurate (no mid-line split); DMA transfer instantaneous (68k→VDP freeze timed in aggregate; fill/copy charge nothing); **residual jump-SFX boop in the generated FM/PSG stream** | per-chip sample-stream diff vs Nuked-OPN2/BlastEm; mid-line raster split; spread DMA over charged scanlines |
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

Status: **APPROXIMATE** — broad coverage, oracle-gated on selected funcs, but known stubs
and no opcode-legality enforcement.

- [x] Most MC68000 families have non-stub codegen — moves, integer arith/logical, bit
  ops, shifts/rotates, MUL/DIV, control flow, Scc (`COVERAGE.md:31-43`).
- [x] Flags computed with explicit per-size formulas; ADD widens to 64-bit for carry
  (`code_generator.c:679-692`), SUB/CMP carry via size-masked compare (`:695-732`),
  MOVE-like sets N/Z, clears V/C, preserves X (`:660-671`). A real ADD-flag bug here was
  caught by the L3 oracle on `Hud_TimeRingBonus` (`code_generator.c:676-678`).
- [x] Tier-3 interpreter mirrors codegen semantics by construction and is differentially
  gated against clown68000 (`runner/tests/m68k_interp_diff.c:1-20,36-56`).
- [ ] **Stubs / unimplemented**: MOVEP, ABCD/SBCD/NBCD (partial), CHK, memory-form
  ADDX/SUBX, TRAP (ignored), STOP→`return;` (`COVERAGE.md:51-56`); collapsed to
  `MN_OTHER`: RESET, TRAPV, RTR, CMPM, ORI/ANDI/EORI #imm→CCR/SR, ILLEGAL/A-line/F-line
  (`:60-67`); `MOVE CCR,<ea>` mis-emitted as load-to-CCR (`:69`). — *Ref: M68000 PRM;
  cross-check decode vs BlastEm m68k core. Validate: clown68000 per-insn diff + a
  synthetic opcode-sweep coverage matrix.*
- [ ] **No EA-legality matrix**: invalid encodings in data can decode as real
  instructions; `MOVE.B` to An can mis-decode as MOVEA (`COVERAGE.md:71-83`). — *Validate:
  fuzz random encodings, diff decoder vs BlastEm/clown68000 legality.*
- [ ] **No synthetic instruction-coverage measurement** (PSX has
  `build_instruction_coverage.py`). Tests are Sonic-ROM-centered (`l1_decoder_test`,
  `l3_oracle_test`, `COVERAGE.md:104-108`). — *Lever: build the Genesis analog —
  per-opcode bucket implemented/missing/deferred with source line ranges.*

Lever: add a codegen hard-diagnostic counter on every `MN_OTHER`/comment-only emit so
coverage gaps surface as a number, not silence; add the legality layer; fill the stubs;
stand up the opcode-coverage matrix.

---

## Axis 2 — Cycle / timing

Status: **APPROXIMATE** — per-instruction cost accounting, not bus-cycle-exact.

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
  a mid-range estimate (`code_generator.c:1409-1411`). — *Ref: M68000 PRM cycle tables /
  BlastEm. Validate: BlastEm `current_cycle` Δ between anchors on real call sites.*
- [ ] **No prefetch / bus-arbitration / sub-line contention model.** — *Validate: cyc_watch
  Δ-anchor comparator vs BlastEm (the cycle_compare.py analog).*

Lever: build the **BlastEm Δ-anchor cycle comparator** (offset-independent — absolute
cycle compare through boot is meaningless; per-hit deltas between consecutive same-anchor
hits cancel the offset, exactly the PSX `cycle_compare.py` method). Then wire exact
data-dependent MUL/DIV/shift costs the probe can already measure.

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
- [ ] **Residual ~8–13% post-alignment waveform difference** (recomp ymfm vs Nuked) — the
  genuine ymfm-vs-die-accurate gap (DAC-ladder / operator rounding); NOT bit-exact, as
  expected for Genesis FM across cores. This is the real remaining FM lever. — *Validate:
  per-channel `--fm-channel N` Nuked diff to attribute the residual to a specific channel /
  the DAC path.*
- [ ] **Jump-SFX boop** still to be reproduced under this rig — the S3 slice captured title
  music; next is an SFX-window capture (jump → the boop SFX) and the same Nuked diff at the
  sweep windows. The earlier "every stage equal" S1 finding is consistent with the residual
  living in the cross-core waveform delta, now measurable. — *Ref: Nuked-OPN2. Validate:
  per-chip sample diff at the SFX sweep window.*

### 5d. PSG (SN76489) — **APPROXIMATE**
- [x] Clean-room own core `runner/audio/sn76489.c` (replaced AGPL clownmdemu PSG); 3
  square + 16-bit LFSR noise, log volume table (`:60-63`); rate master/240 ≈ **223,721 Hz**
  NTSC (`:22,189-192`); own LPF matching clownmdemu coeffs (`:48-52,124-132`).
- [ ] **Sub-sample leftover discarded per frame** to match clownmdemu's per-Iterate reset
  (`sn76489.c:194-197`) — flagged possible long-run boop. — *Ref: SN76489 datasheet /
  BlastEm `psg_run` (Nuked-PSG is the WRONG chip — YM7101). Validate: PSG sample diff.*

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

**BlastEm (cycles + state + audio reference)** — GPLv3 C, builds on MSYS2 MINGW64
(`mingw-w64-x86_64-{gcc,make,SDL2}` + GLEW, `make`):
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

1. **(audio, P0)** Stand up the audio oracle slice on S3K — extend `synth_replay` with a
   Nuked-OPN2 path + the three drift-tolerant metrics; first real recomp-vs-Nuked FM diff
   at the boop sweep windows. *(Deliverable ii — this pass is doc/recon first.)*
2. **(ring, P0)** Promote `[CHIP-TRACE]` (or a lean variant) to always-on in Release so the
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

## Changelog

- 2026-06-28 — Initial 7-axis recon + scorecard. Four parallel recon agents: (a) PSX
  template map, (b) per-axis posture (cited), (c) audio deep-dive (cited), (d) emulator
  landscape (web-verified). Oracle decision locked: BlastEm source-hooked + Nuked-OPN2
  chip truth. First audio slice target: Sonic 3 / S3K.
- 2026-06-28 — First audio slice landed (Nuked-OPN2 die-accurate FM reference + drift-
  tolerant metric). S3 FM: recomp ymfm vs Nuked corr 0.999, pitch 1.13 cents, onset 98%.
  ymfm validated as faithful YM2612; ~8–13% residual is the next FM lever. Axis 5c updated.
