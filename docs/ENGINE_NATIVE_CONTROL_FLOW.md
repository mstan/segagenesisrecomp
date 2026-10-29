# Native calls and control-flow regions (E05)

Owners: `beads-3z5k.4` / `beads-rxq.6`; campaign `beads-2wy8.1`.
Source/test/game gates pass. Final independent PR/record review remains pending;
this is not owner approval to merge or release the new engine batch.
The [machine-readable results](engine-native-control-flow-results.json) retain
source/binary identities, package labels, raw per-game samples and host receipts.

## Implemented contract

Proven cartridge JSR/BSR edges now put the complete resumable frame in the
caller and expose a direct C call. Proven split native tails carry a derived
function continuation in that frame. This removes an out-of-line generic
wrapper, indirect entry and unconditional drain. Dynamic transfers clear stale
native pointers and retain the original resolver, RAM stub and overrides.
The frame's pending/address/previous prefix and address-free walk identity
remain intact; full frames live on the game fiber stack. Nested skips, mixed
stack deltas, reentry and current-build suspension/restore remain supported.
Old generated clients/headers and an explicit diagnostic fallback retain the
original frame path. Static function-table membership alone is insufficient:
direct edges require a proven immutable ROM instruction range. A validator-found
RAM promotion defect was reproduced and fixed for both JSR and JMP.

The common emitter also groups complete canonical bodies and their alias
wrappers by proven native callgraph edges, then balances bounded groups across
the existing32 translation units. This exposes more callees to ordinary compiler
inlining without copying guest instruction fragments, changing hooks or forcing
inlining. Cluster size is bounded at1.2 times average emitted body bytes; a
pre-existing larger function remains indivisible. Allocation failure retains
the old address-order placement. RKA2947 and Sonic1 2081 actual generated
function definitions, plus dispatcher/declarations/cycle/layout files, are
byte-identical before and after placement.

CD companions extend fixed arithmetic regions through one canonical bus-free
BRA/Bcc/DBcc/eligible JMP terminal. Prefix locals/CCR materialize before its
exact ordinary control body. Metadata admits the maximum total charge; runtime
retires actual prefix plus terminal charge and exact terminal PC/diagnostics.
Full current opcode/extension guards and original descriptor/effect/priority
qualification remain mandatory. The original fixed-span alternative remains
available when the larger maximum does not fit. CPU chooser order, rational
Main-clock remainder, IRQ/device/third-master ceilings and inactive-Sub clock
publication remain exact. Modules/effects/registry replacement revoke derived
qualification. Trace, fault, stop, reentry, HLE, WAIT and unsupported work retain
ordinary boundaries. Register-dependent JMP targets are evaluated from current
D/A state; mutable code is guarded, never assumed immutable.

New companions prove147882 ordinary archived bodies, exclude193 noncanonical
symbols and add1908 Main/622 Sub flow spans. Original descriptor architecture,
effects and old callback replacements remain byte-identical, as do42989 existing
companion functions. The qualified ordinary archive remains unchanged at
`1ebae703e46fe0c50e10b77c7687e022515ab2fd11320bfe94383732f29d9249`;
only new companion objects are compiled. No new generator behavior is attributed
to those old ordinary objects.

## Complete candidate disposition

| Candidate | Final disposition and evidence |
| --- | --- |
| Proven static JSR/BSR | Implemented inline complete-frame direct edges for arbitrary proven native callees, not only tiny pure leaves. Actual first-package calls RKA583514 / Sonic1 196753 on the wide routes. |
| Split native JMP/BRA/Bcc/DBcc/fallthrough tails | Implemented qualified native continuations with bounded host-stack drain and original dynamic fallback. Actual tails RKA350044 / Sonic1 1411162. |
| Same-function branches and computed tables | Existing native gotos/switches retained. Function-body bijection and actual interior-alias fixture verify that placement does not replace or lose these paths. No existing capability counted as a new gain. |
| Larger same-TU/callgraph inlining | Implemented bounded common-generator placement after a byte-identical RKA repartition experiment; both actual consumers regenerated and qualified. Experiment and final implementation are separate measurement packages. |
| Tiny whole-callee fragment copying | No separate guest-fragment copier adopted. Cartridge arbitrary direct edges and same-TU placement let the host compiler inline complete existing callees, including memory/hooks/return bookkeeping. Static pure-leaf counts alone were not used to close the goal. |
| CD arithmetic/control regions | Implemented prefix plus exact pure terminal, including variable-charge branches and register-dependent JMP; maximum-budget admission with old fixed fallback. Actual Main339/Sub28353 flow runs retire832/75325 instructions on the route. |
| CD stack-bearing call/leaf/return folding | Evaluated actual live stack ownership and optimistic exact scheduling budgets. Among849238 calls, tiny-leaf sites hit1145 Main/1841 Sub times with owned stack, but only0 Main/19 Sub could fit even the upper budget before extra caller-deadline/body/return guards. No useful whole-leaf folding opportunity demonstrated on this route. Larger bus-bearing callees still require their real internal observers; no new stack-fold machinery adopted. This is a measured rejection, not a postponed architecture prerequisite or an all-title exhaustion claim. |
| Dynamic/RAM targets | CD pure indirect JMP included using current registers and full live bytes. Cartridge mutable/RAM trampolines keep the actual resolver/stub/override route; promoted-RAM JSR/JMP reproducer passes after qualification fix. Unknown addresses are not made static by table membership. Broader identity/version-cache work remains E02; no unchanged rejected cache experiment repeated. |
| Nested/recursive calls and guest stack skips | Actual generated native/fallback configurations cover nested0/1/2 slot skips, mixed stack widths and split tails. Full resumable-frame representation retained. |
| IRQ/reentry/yield/current-build restore | Tested pending static/dynamic tails, nested frame head restoration and address-free walk; real cartridge save/load and rollback pass. Old-state compatibility is unnecessary. No full CD game save/load is claimed where no interface exists. |
| Hooks/aliases/debug/widescreen | Actual native interior-alias call and six sparse-hook execution cases pass per compiler. Existing COSIM/reverse-debug instruction boundaries and dispatchable entries remain in identical function bodies; wide/off built-game routes match exact visuals/state/audio. |
| SR-unmask/TRAP correctness PRs | Frozen corePR4/5 and enginePR33/34 inspected; not adopted or counted as optimization. SR delivery timing and nested TRAP/RTE/skip questions remain with those independently owned correctness changes. These patches are not prerequisites for this new frame/region contract. |
| LTO/PGO/compiler-wide alternatives | E36 remains its own scope. E05 delivers generic body placement and direct edges now, without making compiler-wide changes a dependency. |

The diagnostic CD census is task-only, with retained source/header hashes and
binary identity. Its final inspection matches the qualified candidate. Its
timing is never performance evidence; budget fits are upper bounds, not full
canonical/live/return qualification.

## Validation and measured limits

MSVC/GCC each pass24 real-generated native/fallback frame configurations plus
RAM-edge cases. CD gates pass223488 ordinary/flow/independent PC-cycle-state
comparisons,12090 atomic refusals and5280 scheduler cases covering IRQ, DMA,
third masters, live variants, HLE priority and variable final charges. They
exercise260 flow instructions and446 tight-budget fixed fallbacks. Actual
canonical producer output compiles/links/runs; modified prefix/terminal HLE and
stale parents are refused. Existing arithmetic/RMW regressions remain covered.
MSVC, GCC and GCC through Unix CMake execute alias/hook/layout contracts; the
validator-found CMake executable-path portability defect is fixed.

RKA6000-frame and Sonic1 2400-frame wide BAAB and off A/B routes match state,
frame hashes/PNG, full WAV and actual rollback. Both final builds save/load
their own current-build files, restore exact saved RAM, continue progress and
match qualified baseline artifacts. The earlier package also passes seven
Sonic1 invalid-state atomicity cases. CD four3000-frame runs match every
checkpoint, artifact and full WAV, with zero floor/audio faults.

| Incremental package | Game | Process-cycle reductions, paired | A/A cycle drift | Interpretation |
| --- | --- | --- | --- | --- |
| Inline native frame vs E04 | RKA | +0.72%, +1.83% | +7.84% | Neutral/inconclusive |
| Inline native frame vs E04 | Sonic1 | +0.74%, +0.61% | -3.56% | Neutral/inconclusive |
| Final common callgraph placement vs inline frame | RKA | +1.87%, +1.50% | +1.50% | Small favorable pattern; neutral/inconclusive at this variation |
| Final common callgraph placement vs inline frame | Sonic1 | +2.17%, -0.54% | -2.55% | Neutral/inconclusive |
| CD pure control vs E04 | SonicCD | -4.21%, +19.52% | +36.63% | Neutral/inconclusive |

Positive reductions mean fewer process cycles; negative means more. No proven
new game speedup, universal FPS improvement or summed package gain is claimed.
CPU, off-view samples, startup/build/binary costs, sources/settings and capacity
telemetry are retained in the machine-readable report and raw receipts. Tests
were muted and isolated on the capacity-qualified workstation. No unrelated
owner work was stopped, delivery archive replaced or owner game relaunched.

First failed fixtures/builds and the reproduced RAM defect remain explicit in
the results. They are not hidden as candidate performance samples. Successful
automation does not authorize merging/releasing this new batch; preserve these
builds for the eventual combined owner handoff.
