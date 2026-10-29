# Independent E14 final validation

Reviewer: `validate_e14`. Reviewed source and ready PR #46 head
`8029d30212dff734bc52b4b0e30b544f03eff3ad`; production disposition
`452f52a42f24491fa7cc79ef176cf09858c5aab6`.

**ACCEPTED: the original bounded E14/N18 opportunity is decisively evaluated
and rejected. The retained test/build-fix package is ready for the combined
owner handoff. No runtime optimization or game speedup is accepted or claimed.**
There are no unresolved objective findings in this review.

## Scope and decision

The original closure required an actual stamp-heavy built consumer and an exact
generic block/span implementation, preserving Word RAM, priority, partial
progress and IRQs. Both complete implementations met that scope: R1 hoisted
immutable geometry into a native line job with a live destination cursor; R2
selected all eight fixed hardware modes and processed exact destination-cell
spans. These were implemented, compiled, correctness-qualified and exercised by
real Sonic CD SS1, rather than proposed or deferred. Every measured arm executed
400 stamp jobs in the same 1,200-frame window.

The first implementation repeated adverse process CPU/cycle cost on its focused
follow-up after mixed initial observations. R2 demonstrated no gain and had
adverse observed CPU cost; its smaller cycle difference remains within earlier
variation. Rejecting both and retaining the best existing native job is the
correct per-game decision. A third tuning sweep is not required to satisfy this
bounded obligation. Neither test activity nor removing divisions establishes a
user-visible gain.

The reassessment correctly distinguishes unsafe unguarded cached content,
combined stores and work crossing observation boundaries from hypothetical
future guarded designs. It does not claim every future stamp algorithm is
exhausted or that baseline-equivalent timing proves hardware contention
accuracy. This is a completed bounded rejection, not partial delivery or a
deferred prerequisite.

## Independent checks

- Final `stamp.c` is byte-identical to `febff84`. The frozen oracle is also
  byte-identical after only exported symbol-prefix and explanatory-comment
  changes. The only final runner diff is the explicit `size_t` comparison cast;
  both limits are nonnegative and its C comparison semantics are unchanged.
- Audited R1 and R2 sampling, transforms, coordinate wrapping, destination
  geometry and live sample-before-single-nibble-write ordering. Neither changes
  scheduler observation boundaries, serialized device layout or public state.
- The 3,080-job full-state/262,144-byte Word RAM oracle meaningfully covers
  randomized geometry, directed map/art/vector feedback, offsets, partial and
  maximum dimensions, live register/priority/RAM changes, split progress,
  ownership pauses and current-build restore. Existing independent corner,
  timing/IRQ, memory, DMA and native-region gates remain. The new test is a
  supported suite dependency; initial compile/reporting failures are retained.
- Reviewed all nine R1/R2/final platform result sets: six tests pass without
  failures/skips on each MSVC x64, actual Win32/x86 and GCC Linux run. Independently
  checked final test PEs have machine types `0x8664` and `0x14c` respectively.
- Independently hashed 53 retained raw receipts, 213 distinct receipt/artifact
  files and all three frozen game binaries. Source hashes in consumer manifests
  match their recorded Git commits. All eight game arms have identical source
  gates, complete inspect checkpoints, full final Main/Word/PRG/Z80 RAM,
  capture artifacts and full WAV. All five compacted checkpoint hashes per arm
  reproduce the complete retained raw inspect payloads.
- Verified actual capacity receipts use the same physical Windows host,
  compatible measured headroom, realistic CPU/RAM budgets, CPU14, below-normal
  execution and isolated muted software-rendered output. All negative samples
  remain attributed to their respective packages; no aggregate offsets them.
- Ready/open PR #46 stacks PR #45 and its remote head matches reviewed source.
  Its title/body accurately describe the useful Win32 build repair and oracle,
  and the rejected runtime experiments. Durable final investigation comments
  exist on owning `beads-3z5k.11`, parent `beads-3z5k` and campaign
  `beads-2wy8.1` (IDs `01a18947-b0a2-7a2e-8dc5-df0e42280790`,
  `01a18947-c1fd-7dda-8d68-f8016bf5b75e`,
  `01a18947-c3d1-78d1-8366-ed7c6ec08fe5`). Owning issue is claimed and
  correctly labeled in the central database.

Reproduction of this offline evidence audit is retained in
`validator_e14_audit.py`; its machine-readable result is
`VALIDATOR_E14_AUDIT.json`. No new gameplay, benchmark or source implementation
was performed by the reviewer.

No ordinary-route or cartridge rerun is required for the final retained package:
the original stamp kernel is restored exactly, the comparison cast is
semantics-identical, final supported platform gates pass, and cartridge consumers
do not link these CD-only files. No new cartridge gain is claimed. No owner
playtest is repeated by this audit.

## Explicit next-task instruction

Root: record E14 as `bounded_rejected`, update the checklist and machine-readable
audit from 21 to **20 original families open**, and attach this verdict to
PR #46 and the owning/parent/campaign Beads records. Complete the planned config
retention and audited non-forced cleanup of owned build/TEMP scratch, preserving
the frozen PEs, source prototypes, raw evidence and review records; finish the
central sync. Then **move to original E15, reuse unchanged hardware render
results, in a new isolated worktree with a fresh validation-only subagent**.
Do not resume E14 tuning or skip E15 because historical narrow caches were
rejected. No merge, release, delivery or owner-facing launch is authorized by
this engineering verdict; the combined owner handoff remains pending.
