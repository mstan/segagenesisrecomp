# E04: Generated native arithmetic and state

Owner: `beads-3z5k.3`; shared core: `beads-rxq.5`; campaign: `beads-2wy8.1`.
Independent validator: `validate_e04`. **Qualification in progress; no adopted performance claim or family closure.**

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

Final compiler gates, source/binary identities, per-game observations, runtime
variation and PR links will be recorded here after the independent gate.

The first built-game package passed exact state/pixels/full-WAV/rollback checks
for Sonic CD, RKA and Sonic 1. Sonic 1 showed a bounded improvement; CD timing
was mixed. RKA's workstation pairs were consistently slower, while a reversed
pair order on SERVER was neutral. This remains an acceptance concern until the
revised package is qualified; no aggregate improvement overrides it.

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
require the original body. A bounded common-emitter prototype is being measured
with those boundaries and full per-operation view lifetime. Its disposition is
pending; this document does not count memory-EA consolidation as delivered.
