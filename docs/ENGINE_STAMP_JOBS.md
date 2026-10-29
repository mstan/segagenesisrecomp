# E14 stamp jobs: retain the existing native kernel

Owner `beads-3z5k.11`, parent `beads-3z5k`, campaign `beads-2wy8.1`.
Base `febff84` has the same compiled runner/tests as accepted E13 `999f392`.
Rejected prototypes: `ec9e653` and `d0bf3d1`. Final code `452f52a`.
**Proposed bounded original-family rejection; final independent audit pending.**
No merge/release/owner-launch approval.

Two complete generic replacements were implemented, qualified and reached by
actual stamp-heavy Sonic CD gameplay. Neither demonstrated a useful gain;
the first repeated its adverse game cost and the second also increased measured
CPU cost. Both are rejected. Final `stamp.c` is byte-identical to the base.
The retained useful package repairs a pre-existing Win32 /WX build failure and
adds a frozen full-state/RAM device regression oracle to the supported suite.
No new runtime optimization or game speedup is claimed.

## Complete bounded opportunity and reassessment

The existing device already executes a whole native asynchronous affine line
job: it does not invoke an emulator or bus callback for every dot. The original
E14 obligation was to choose actual stamp-heavy gameplay and optimize generic
exact jobs/blocks while preserving Word RAM, priority, progress and IRQs.
This work evaluates that whole bounded obligation, not one cache and not a
missing writer/ownership prerequisite deferred to future work.

R1 decoded immutable geometry once per line and used a live destination nibble
cursor. R2 selected all eight hardware geometry/repeat modes once per line and
consumed exact destination-cell spans of up to eight dots. Its compiled kernels
used constant geometry masks/shifts and no runtime division. Both preserved
live map/art/vector reads, each sample-before-single-nibble-write, signed
fractional deltas, 24-bit source and 18-bit destination wrapping, odd/final
partial spans, zero/max dimensions, latched registers, live priority, ownership
pauses, partial clocks, progress, completion IRQ and current-build snapshots.
Neither retained pointers or derived data in state or changed the scheduler.
The unchanged five-clock/dot functional timing profile remains provisional;
matching the baseline is not independent hardware contention qualification.

After two unsuccessful coherent implementations, the architecture was reassessed
and the original native job retained. Unguarded cached content, combined stores
or work crossing an observation boundary can break demonstrated Word RAM
feedback/deadline behavior. Qualified guarded designs remain theoretically
possible; these tests do not rule out all future caches or algorithms. There
is no demonstrated coherent saving here that justifies further machinery or
another tuning/benchmark sweep. No title HLE, asset eligibility or game-effect
redesign was introduced. This is a bounded rejection, not a universal optimum
claim or partial delivery of a new kernel.

## Correctness and portability

For R1, R2 and final restored source, six supported gates pass on each MSVC x64,
actual Win32/x86 and portable GCC Linux: `segacd_stamp`, `segacd_stamp_job`,
`segacd_memory`, `segacd_word_bus`, `segacd_dma_idle_span`, `segacd_native_region`.
The frozen original oracle is byte-exact after symbol-prefix/comment removal.
New checks compare every device byte and all262144 Word RAM bytes across
2048 random jobs,1024 controlled feedback jobs and8 maximum jobs (3080 total).
Existing independently checked manual corners, fixed-point, priority and IRQ
fixtures remain. New tests exercise live RAM/priority/register mutation,
map/art/own-next-vector overlap, split clocks, ownership pause/resume and
current-build copy/restore. The new target is in `segacd_test_suite`.
No existing CD host save-file API or old-state migration is invented or claimed.

The only final runner diff is an explicit size_t conversion of a nonnegative
native-span limit. It has the same C comparison semantics and fixes the original
MSVC Win32 signed/unsigned /WX failure. No warning was suppressed. Its initial
failure log remains. R1 initially printed3081 instead of3080 checked jobs;
that diagnostic was corrected. R2's constant-condition build warning was fixed
with preprocessor mode guards before execution. No failed sample was discarded.

## Actual built-game results

A=accepted E13 Sonic CD PE; B=explicit R1/R2 package. The supported consumer,
qualified native instruction archive/catalog, full emulated audio, BIOS/disc,
isolated fresh BRAM and settings are identical. Dependencies/consumer/UI/source
heads, build configs, PE hashes and raw samples are retained. Each pair uses one
capacity-reserved physical Windows PC, CPU14/below-normal, software renderer
and dummy output. Other owner work continues; this is process cost, not FPS,
physical GPU throughput or a quiet-host claim.

The existing source-gated SS1 fixture enters Stage Select row73, verifies full
STSEL/SPMM, owned SPSS prefix and live Sonic/six-UFO actors, then bypasses only
demo input. No result/completion field is written. After600 settling frames,
1200 fixed turn/jump frames execute400 stamp jobs in every measured arm.
All eight source/actor/checkpoint gates, full final Main/Word/PRG/Z80 RAM,
images/memory/frame hashes and full WAV match exactly. Original SS1 presentation
is preserved; special-stage widescreen is not claimed.

| Package | Order/arm | CPU seconds | Process cycles | Stamp jobs |
| --- | --- | --- | --- | --- |
| rejected_r1_abba | 1 A | 4.750000 | 22,480,666,350 | 400 |
| rejected_r1_abba | 2 B | 4.578125 | 21,662,797,260 | 400 |
| rejected_r1_abba | 3 B | 4.828125 | 21,415,292,910 | 400 |
| rejected_r1_abba | 4 A | 4.421875 | 20,834,413,800 | 400 |
| rejected_r1_followup | 1 A | 4.359375 | 20,262,796,040 | 400 |
| rejected_r1_followup | 2 B | 4.593750 | 21,116,230,970 | 400 |
| rejected_r2_ab | 1 A | 4.609375 | 21,846,690,400 | 400 |
| rejected_r2_ab | 2 B | 4.984375 | 22,163,146,570 | 400 |

R1 initial paired reductions were +3.62/-9.19% CPU and +3.64/-2.79% cycles,
with about7% baseline drift. The one focused follow-up repeated adverse cost:
-5.38% CPU and -4.21% cycles. R1 is rejected; its first positive pair is retained.
R2's pair was -8.14% CPU/-1.45% cycles. Its cycle difference alone is within
prior variation; it demonstrates no gain and supplies no reason to take the
additional code with adverse observed CPU cost. R2 is rejected too.
No averaging, package mixing or other game/feature offsets a negative sample.

R2 startup CPU/wall costs and working/private memory remain in its raw pair.
Its rejected PE grew6144 bytes; that code is absent from final production.
Final public device layout, allocations, state/netplay/backend identity and
portable execution are unchanged. No new ordinary-route or cartridge gameplay
was needed after restoring the original kernel and retaining only a
semantics-identical comparison cast; final source/platform proof is retained.
The original ordinary route had no stamp jobs and cannot select a stamp kernel.
RKA/Sonic1 do not link these CD-only files and received no optimization package
or new measured gain. No owner-facing or previously cleared playtest was repeated.

See [machine-readable results](engine-stamp-jobs-results.json). Evidence roots:
`C:/Temp/Codex/genesis-engine-stamp-jobs-20261029` and
`G:/Temp/Codex/genesis-engine-stamp-jobs-20261029`. Both source prototypes,
full patches/disassembly, frozen PEs, raw WAV/RAM/images/commands, all platform
logs/configs and capacity telemetry remain. R2's inherited r1 initial build-time
field was relabeled explicitly; original JSON and executed producer remain,
and no PE identity, runtime sample or correctness artifact changed.
Final independent review, PR/Beads record and audited build/TEMP cleanup follow.
