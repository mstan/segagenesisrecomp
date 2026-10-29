# E07 actual-event scheduling

Owner `beads-3z5k.6`, campaign `beads-2wy8.1`. Production source `2079c51`,
parent E06 `61a63c2`, shared 68000 core `f7dd616`, Z80 core `4dfc5fc`.

The previous 384-Sub-clock ceiling was an artificial service interval inside
qualified native execution, including active CPU regions. It was not always
a hardware timer event. Qualified intervals now stop at the earliest actual
timer, CDD transport/packet, CDC decoder pulse, permitted stamp line, DMA word,
host raster or requested execution deadline. Device limiting by a third
master still applies. CPU instructions, faults and bus effects still execute.

Qualification reuses the existing effect metadata/interval-composable sink
contract. Opaque host callbacks without a next-event provider keep the bounded
cadence. Changing that callback or effect metadata invalidates the derived
event cache. All-STOP `run_until` uses the same real-event selector; the public
single-step API and synchronous WAIT budgets retain their original semantics.
`SEGACD_EVENT_DEADLINES=0` is an internal build-time qualification control,
not a shipped user setting or an HLE/LLE switch.

The actual Sonic CD census then identified a larger current cost:98,309,446
device-service calls, including26,117,549 without clock progress. The private
qualified instruction driver now omits redundant **pre-instruction** clock
publication when no time has advanced and no host event is due now. The
post-instruction publication, public step and all WAIT paths retain their old
behavior. Deadline cache invalidation still happens before an omitted call;
writes can change deadlines even without shared-clock progress. Qualification
is derived at its configuration setters rather than reproved in the hot loop.

## Complete selected-family inventory

The audit covers the following engine scheduling mechanisms. These decisions
are about observable execution and demonstrated eligibility, rather than
keeping existing subsystem boundaries as an architectural requirement.

| Candidate | Decision and concrete evidence |
| --- | --- |
| Qualified Main/Sub native regions | Implemented real event deadlines. The old synthetic384 cap was independent of actual device state. Effect-qualified instruction/register/flow spans continue through their existing safe ownership window. Real instructions, owner eligibility and time conversion remain intact. |
| Main/Sub hardware STOP | Implemented real event wakeups in qualified `run_until`. Masked timer activity, accepted IRQs, stable level7 pins, exact device/RAM/CPU state and simultaneous events are tested. No guest polling loop is converted into STOP. |
| Hardware event/device clock ownership | Consolidated the selector for native and STOP intervals. Timer reload/origin, CDD command/seek/skip/fader/frame deadlines, finite CDC DECI, stamp access permission, six-clock DMA words and host raster deadlines remain live inputs. PCM/CDDA sinks retain the already-required interval composition contract. |
| Redundant qualified clock publication | Implemented omission of unchanged pre-instruction publication with no due host event. The current workload has26.1million no-progress calls. Public and opaque-observer service remains exact; a tracked-bus fixture checks real RAM effects and a host event already due at clock0. Final built counts/timing are recorded below. |
| Ordinary coordinator step and opaque observers | Retained the published384-clock maximum idle advance and every old callback. An opaque observer is an actual observation boundary. The configuration-transition test first warms an unbounded native cache, installs an opaque observer, then checks the actual callback occurs within the old bound plus instruction overshoot. |
| Main/Sub synchronous ownership WAIT | The actual owner executes on the blocked caller's stack, preserving earlier bus effects; only the blocked bus access retries. `wait_limit` bounds owner/scheduler advances. Beyond rejecting unlimited jumps, evaluated a bounded virtual-attempt span: stop at the earlier real event or the remaining budget times384, then account each original attempt and execute the real waking owner. It requires a stopped owner and complete observer deadlines. The actual3000-frame census finds zero ownership WAIT and zero stopped-WAIT attempts; this mechanism removes no work in the current CD consumer. No unused virtual-budget API was added. Existing actual timer/IRQ/owner-grant fixtures retain the real bounded wake behavior. |
| External Z80/third-master Main-byte WAIT | Existing immediate probe, reset eligibility, exact owner instructions, real grant retry, fault and stall rejection remain. A zero budget permits only the immediate byte. STOP wake uses actual event sources within bounded attempts. Fabricating a grant, skipping the owner, or weakening fault propagation is rejected. The external-bus tests exercise these paths. |
| Cartridge scanline/fiber coalescing | `machine_run_frame_ex` begins raster state, resumes the 68000, services latched IRQs, runs Z80 and then delivers line IRQs before row/status/output work. Z80 banked accesses can touch Main RAM and sound mailboxes. Besides rejecting blind488-cycle enlargement, evaluated servicing those SAME boundaries on the game fiber: a scheduler continuation would perform existing raster/Z80/IRQ/sink work there and switch out for frame completion, snapshots/debug/explicit yields. This is architecturally possible; subsystem ownership is not a prohibition. Actual transition instrumentation finds only0.05643seconds across1,736,083switches in RKA6000frames (0.3986percent of process CPU), and0.01110seconds across375,259switches in Sonic1 2400frames (0.4583percent). These estimates include timestamp/preemption overhead and are not exact isolated cost. Existing assembly minicoro switches are already very cheap. The complete switch-removal opportunity is below0.5percent on both representative consumers, before continuation/callback/snapshot-gate costs. This candidate is ruled unproductive for this run rather than landing a new coupled scheduler with no measurable runtime case. E04/E05 native spans do not subsume these switches and are not counted as resolving this candidate. |
| Cartridge VBlank/interrupt/mailbox waits | The engine already parks recognized VBlank waits, drains real IRQ cycle debt and yields mailbox/IRQ polls so the other hardware can execute. Arbitrary repeated reads may be volatile, acknowledge status or observe external writers. Address/PC signatures and fabricated values are rejected; title polling/HLE replacements belong in the separate title backlog. Existing title-specific shortcuts were not extended or used as proof of generic safety. |
| Continuous Z80 execution | Existing E06 intervals already avoid adapter reentry between instructions. Guarded operations, per-instruction IRQ sampling and exact BUSREQ/reset prefixes remain necessary. The actual machine tests assert BUSREQ/reset after1/2/7/31 instructions during an interval; batching past those writes would be a regression. |
| Z80 HALT refresh batching | Evaluated both the opaque callback contract and the known machine reset/BUSREQ predicate. With running Main/Sub CPUs, settling their real instructions can change reset/BUSREQ at a precise completed Z80 prefix; bulk refresh requires materializing that prefix at each such observer. When both68000s are stopped, a qualified event horizon could batch four-cycle refresh ticks while retaining R/64-bit cycles/rational remainder/retired count/EI/IRQ. The actual CD census records zero HALT ticks among16,241,534operations, including zero both-STOP eligibility. It removes no work here, so no unused HALT interval contract was added. Existing ordinary HALT/EI/IRQ and in-interval machine BUSREQ/reset fixtures remain qualified. No semantic sound-driver replacement is implied. |
| Reset/BUSREQ-held Z80 and parked cartridge CPU | Already consume held/parked time without guest instructions. `segacd_z80_hold_until` resets fractional phase by its existing contract; the cartridge helper returns for parked/VBlank state and pays actual IRQ debt. There is no per-cycle held-CPU instruction loop to remove. |

This inventory does not claim that every future game or new ownership contract
can never expose more batching. It resolves the current engine paths and
qualifies the useful replacement available under their supported contracts.

## Qualification

`segacd_event_wait_test` compares full device blobs, both CPU architectures,
RAM, clocks/remainders and actual observer events against the bounded path.
It creates a real invalid-sector decoder pulse (explicit invalid EDC), submits
a real CDD status command through HOCK, and aligns decoder expiry, packet
completion and timer activity at22500. DMA/stamp jobs, pending/masked IRQs,
opaque callbacks, third-master rejection, faults and public-step cadence are
also checked. Idle service calls fall33 to3; a host-event case falls35 to9.
These are component work counts, not whole-game speed claims.

The tracked-bus publication fixture reduces callback count63 to33 with exact
CPU/device/RAM/event observations. Its public-step comparison remains62 to62,
and opaque intervals remain63 to63. It checks an actual event due at clock0
before the first operation, not merely eventual final state. Full stdout is
retained in `events-msvc-full.log` and `events-gcc-full.log`; CTest's default
JUnit output truncates passing stdout after1024bytes.

All six affected test targets pass on MSVC19.44 and GCC13.3: event waits,
native regions (including1308 retained comparisons), coordinator, external
bus, Z80 native and machine. Raw logs/XML and capacity receipts are retained
in `C:/Temp/Codex/genesis-engine-event-scheduling-20261029`.

## Built consumers and measured costs

The final package uses supported frozen Sonic CD consumer94f5779/UIb9ef2f5,
unchanged E05 catalog manifest283505cd and qualified instruction archive
1ebae703. Clean reference and candidate are both built from2079c51, changing
only the internal `SEGACD_EVENT_DEADLINES` definition0/1. Final binary SHAs
are da64562917c60505aec64746d09fb33a22d3e8eee6592cbdc3031f668f561da2
(reference) and749493d3a5d12e46cc6dc26f844847d36d00f6b32e6daa73ea6b304b21b6c5ed
(candidate),115635200bytes each. The actual build logs retain compiler flags,
and source/binary identities and commands are in the machine-readable results.

All four final3000-frame BAAB runs match accepted architecture/device/memory
checkpoints, pixels and full WAV. Actual native work and region counts match;
execution floor, audio faults, underflow/overflow and backwards audio counts
remain zero. Process cost is measured over frames1800-3000 on one capacity-
reserved Windows host, logical14/below-normal, dummy SDL/software presentation
and faithful audio. This is not physical GPU performance or a netplay
cross-build compatibility claim.

Runtime is **neutral/inconclusive**: paired CPU reductions+7.1429/-1.8561percent,
process-cycle reductions+3.9633/-1.8772percent; reference A/A CPU drift-0.6912
and cycle drift+3.1020percent. Candidate CPU samples span6.296875 to6.859375
seconds (~8.93percent), so the small mixed second pair is within observed
variation. No new FPS, load-time or whole-game CPU speedup is claimed.

The first actual-event-only package2478108 also matches four exact runs but
does not reduce region-service counts on this route (897820both arms). Its
CPU reductions-2.05/-4.66percent and mixed cycles are retained, not promoted
as a win. The later actual work census selected redundant clock publication
as the current useful service target instead of continuing timer-cap tuning.

Cartridge production code is unchanged from E06: the runner diff contains
only CD files. Reuse accepted E06 RKA/Sonic1 gameplay, current-build save/load,
rollback, pixel and full-WAV evidence with exact source/binary identities.
The separate task-only fiber census measures candidate cost; it is not a new
gameplay acceptance or an A/B performance package. Owner-facing game windows,
delivery archives, saves and settings were not changed.

The final task-only census verifies98,309,446 to80,262,842full service calls:
**18,046,604omitted (18.3569percent)**. Actual clock-progress advances remain
72,191,897and Z80 operations16,241,534. HALT/WAIT/all-STOP counts remain zero.
Thus a real repeated service cost was removed without eliminating meaningful
clock advances or CPU work; this does not turn mixed timing into a proven
whole-game win. The diagnostic binary was retained separately from both clean
performance binaries. See [machine-readable results](engine-event-scheduling-results.json).

Independent complete gate is pending. Do not mark E07 accepted or move to E08
before that gate. PR41 is stacked on E06 PR40; combined owner visual approval,
merge and release remain pending. Task build trees/logs/private fixtures are
retained outside delivery folders for this campaign; census binaries are
preserved under `binaries/census` for review before disposable build cleanup.
