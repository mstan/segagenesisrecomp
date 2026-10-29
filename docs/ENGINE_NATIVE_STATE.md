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

The cartridge fast path runs only when the entire span finishes strictly before
the existing timing threshold. Counts and audio clocks are grouped, then all
architectural registers are materialized. Original instruction paths and entry
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
