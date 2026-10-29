# E04: Generated native arithmetic and state

Owner: `beads-3z5k.3`; shared core: `beads-rxq.5`; campaign: `beads-2wy8.1`.
Independent validator: `validate_e04`. **Compiler and built-game gates pass; final independent PR/record review is pending. Runtime is neutral/inconclusive for all three tested games.**

The package starts from E01 and the independently accepted E03 branch. It does
not contain E02's rejected page-version/code-identity runtime. Cartridge and CD
frontends share the arithmetic/EA emitter in `m68k-recomp-core`.

## Replacement and observable exits

The common emitter forms fixed-cycle, straight-line register spans of two to
eight instructions. D/A registers and CCR live in host locals; a backward
read/write analysis removes only overwritten flag work. X survives logical
operations, and X/sticky-Z consumers keep their inputs. Unknown effects are
exits. Adjacent LEA/address additions share local address expressions rather
than repeatedly publishing/reloading their intermediate global registers.

The cartridge fast path uses the runtime's actual read-only scheduling budget.
It ends strictly before a possible yield, watchdog or counter wrap; scaled-clock
normalization and a stale audio baseline retain ordinary retirement. Counts and
audio clocks are grouped, all architectural registers are materialized, then the
ordinary final check publishes private budget/seen/stall state. Older clients
without that contract retain the original VBlank-threshold guard. Original instruction paths and entry
labels remain for threshold crossings, aliases, game instruction hooks,
stack-unwind bookkeeping, co-simulation and reverse debugging. Memory/MMIO,
WAIT, faults, variable-charge operations and control transfers remain exact
instruction boundaries. This is not memory-RMW fusion across bus observers.

CD companions use the same emitter. The coordinator admits a span only before
the other CPU is due, the next device event and the caller/third-master ceiling.
Main-clock conversion keeps the exact rational remainder. Every current opcode
and extension byte is checked on each attempt; no code identity is cached.
Retirement PC/opcode, final charge, instruction counts and inactive-Sub clock
publication match ordinary retirement. Existing single-step APIs stay ordinary.

Companion generation verifies the parent catalog and canonical-body map hashes,
then compares exact ordinary emission against each original operation body.
Register-only effects alone do not authorize replacing HLE arithmetic from its
guard opcode. Noncanonical bodies split runs and retain their original callback.
Runtime catalog/effect/priority checks additionally filter spans shadowed by HLE.
Registry/effect/span replacement revokes derived discovery; immutable lookup
caching never replaces live byte guards. Qualification uses a bounded 8192-bit
table per CPU, with ordinary fallback for unsupported bundles.

Build `M68kSpanRecomp`, then run `tools/native_register_spans.py --source <compact>
--output <separate-output> --emitter <M68kSpanRecomp>`. The resulting catalog
sources include newly compiled companion objects; ordinary operation objects
remain a separate, explicitly identified dependency.

## Findings and evidence

The scheduler oracle caught incorrect inactive-Sub clock publication at a grouped
Main exit. The corrected path publishes the exact start clock of the final Main
instruction, including its rational conversion. Failed-before evidence is retained.

GCC also exposed existing undefined signed overflow in CMPA: after
`SUBI.L #$FFFFFFFF,D1`, with initial D1=$7FFFFFFF and A0=$0000FFFF,
`CMPA.L D1,A0` must produce N/V/C and preserve X. The old signed subtraction
could drop N when optimized. The shared emitter now uses defined unsigned
modulo-32 subtraction. A focused expected-flags regression supplements the
ordinary/span differential check. Fresh generation receives the fix; an older
borrowed operation archive does not. Nonidentical archived bodies remain
ordinary-path boundaries rather than being falsely declared equivalent.

The synthetic suite covers the closed effect grammar over all opcode words and
three extension patterns, compiled sequence equivalence, all CCR combinations,
current live guards, priority/effect replacement, IRQ, DMA and third-master
boundaries. A real cartridge-generated harness compares threshold observations
against per-instruction co-simulation and verifies reverse-debug fallback.

Private reproducibility roots (not delivery folders):

- Source/tools/logs: `C:/Temp/Codex/genesis-engine-native-state-20261028`.
- Built products, catalog provenance and raw game samples:
  `G:/Temp/Codex/genesis-engine-native-state-20261028`.
- Portable test scratch: `/tmp/genesis-engine-native-state-20261028-tests`.

Final source/binary identities, all per-game samples, capacity receipts and costs
are retained in [machine-readable results](engine-native-state-results.json).
No new batch has merged or released; owner combined visual clearance is pending.

The first built-game package passed exact state/pixels/full-WAV/rollback checks
for Sonic CD, RKA and Sonic 1. Sonic 1 showed a bounded improvement; CD timing
was mixed. RKA's workstation pairs were consistently slower, while a reversed
pair order on SERVER was neutral. That package was rejected; no aggregate improvement overrides an individual-game
regression. The subsequent out-of-line budget revision was also rejected because
Sonic 1 cycles rose4.67-5.53% against0.34% A/A drift. After those two experiments,
the final revision replaced per-span calls with one actual shared inline budget
representation rather than adding title-specific thresholds.

The RKA investigation exposed a real interface mismatch: its cycle accumulator
remains beyond the old VBlank threshold, although the own runtime's next
observer is its remaining scheduler/watchdog budget. The first guard retired
only one span in the 6000-frame route. The revised contract checks the actual
budget and materializes the same ordinary bookkeeping at the exit. A standalone
state oracle and real generated-code comparisons cover both admitted intervals
and exact exits, including private scheduler state.

Owned-memory effective-address consolidation is evaluated separately from local
LEA folding. E03's ordinary read/write views can safely permit latching an EA
within a complete dual-owned range. Arbitrary callbacks can change the base or
index; missing callbacks, permission asymmetry, split ranges, WAIT and faults
require the original body. A bounded common-emitter prototype passed4,224 correctness cases
and ABBA measurements with those boundaries and full per-operation view lifetime. Most component cycle
pairs improved about 12-24%; one long-displacement case was inconclusive and the
short CPU-time samples were coarse. These are not built-game gains.

The shared API/emitter now handles a closed single-memory-RMW grammar. It latches
an EA only after complete independent read/write grants in a managed tracked
operation; pre/post updates remain exactly once in the selected original body.
Missing callbacks, partial reads/writes, alignment, WAIT, trace and permissions
retain the ordinary path. The catalog substitutes only exact body-proved callback
pointers, without another runtime lookup. Descriptor bytes, cycles, effects and
priority remain unchanged. Noncanonical/HLE callbacks retain their original body.

Both compiler gates pass 546560 architectural/bus/WAIT/fault/trace/permission
comparisons over 1220 compiled classes; the opcode/extension audit admits 20418
cases. This includes 2219 partial-write progress observations. A producer-local
name collision initially clobbered the span-registry declarations when an owned
callback existed. A failed-before fixture and real generated-header compilation
now cover that case. The validator independently cleared this source/compiler/
producer finding after reviewing failed-before and passing evidence.

## Final built-game gate

Frozen engine `132b84d27a61acb3657ec12737446ca9625e7bb1`, core
`4479e7e24abe3e4478d943f1eebd41cc264e0140`, package
`E04-common-native-state-r4-inline-budget-owned-rmw`. The baseline is E03
engine `246ef0b`/core `ee81e00`; cartridge binaries use that shared engine,
without claiming CD-only ownership changes apply to cartridges. Consumers,
ROM/disc/input identities, dependencies, settings and binary hashes are recorded.

Final MSVC targeted3/3, full GCC18/18 and the final GCC generated-registry
compilation pass. Earlier unchanged register and scheduler suites remain valid.
The actual generated cartridge budget oracle compares2,592 cases with844 admitted
spans; the independent pure model covers517,440 states. The CD scheduler gate
covers1,320 cases, including IRQ stack, DMA, priorities, mutable variants and
third-master ceilings. These are correctness evidence, not performance gains.

All four Sonic CD3,000-frame arms exactly match11 RAM/device/image checkpoints,
complete memory/pixel artifacts and full WAV. CPU retirements and audio ordering
remain unchanged with zero floors/audio/queue faults. Candidate arms retire
385,521 owned-RMW grants and97,160 register spans. RKA6,000-frame and Sonic 1
2,400-frame16:9 BAAB plus original-aspect AB arms exactly match state/frame
hashes, screenshots, full WAV, audio totals and actual current-build rollback
probes. RKA retires about1.25m spans, Sonic 1 about1.44m. Core current-build
state/memory restore reacquires fresh views; the CD game QA interface has no
save/load command, so no actual CD game save/load claim is made.

| Workload | Paired process CPU reductions | Paired process-cycle reductions | Baseline A/A drift, CPU/cycles | Classification |
| --- | ---: | ---: | ---: | --- |
| Sonic CD, frames1800-3000 | -10.16% / +5.84% | -2.70% / +5.07% | -1.82% / -6.48% | Neutral/inconclusive |
| RKA,6,000frames16:9 | +2.01% / +2.39% | +3.34% / +1.44% | +15.41% / +14.20% | Neutral/inconclusive |
| Sonic 1,2,400frames16:9 | -7.06% / +0.63% | -5.82% / +0.49% | -6.47% / -7.64% | Neutral/inconclusive |

RKA's original-aspect single AB pair was24.44% CPU/22.49% cycles slower.
This concrete concern triggered only a targeted reverse ABBA confirmation on the
same host/configuration. Its cycle differences were0.08%/0.84% worse, within
4.53% baseline variation; the large regression did not reproduce. Both sets stay
in the results. Sonic 1's original-aspect single pair was2.0% CPU/1.86% cycles
slower amid its larger measured variation. No final game speedup is claimed.

All matched pairs used this PC, logical14 affinity, below-normal priority and
capacity reservations, with isolated profiles and dummy outputs while preserving
full faithful rendering/audio. Ambient variation remains; this is not a quiet-host,
physical-GPU or speaker result. CD first candidate startup was4.885s versus
0.996s warm candidate and1.021/0.907s baselines; CPU startup0.563s versus
0.469-0.500s. Different cold/warm conditions do not establish a startup win or
regression; the raw costs are retained. EXE sizes increase by3,949,056 bytes
(CD),387,584(RKA),278,528(Sonic 1). Larger generated code is accepted only with
the qualified built behavior; no memory-copy or page-identity cache was added.

This delivers the original common-emitter local-register/CCR/grouped-cycle and
qualified combined-EA contract. The cartridge bus currently exposes per-access
cycle/ring/scheduler effects and has no complete dual-owned provider; ordinary
bus exits preserve those effects. Local LEA folding alone is not credited as
memory-EA consolidation there. Future cross-instruction memory/control-flow
designs are not claimed exhausted. E02 remains open independently.
