# Reusable firmware services (E30)

Runtime `f13bf7c`, based on accepted E27 `d4a88d5`, implements a guarded native
protection-table operation used by the standard Sega CD BRMINIT service.
The original whole-family decision is awaiting its independent final gate.
Owns `beads-3z5k.26`; campaign `beads-2wy8.1`. Combined owner play/merge/release
handoff remains pending. Exact builds, inputs, raw paths and samples are in
[the results](engine-firmware-services-results.json).

The qualified firmware is US Model 1 v1.10, 128KiB, CRC C6D10268, full SHA256
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.
HLE declines every other identity. This adds no standalone/catalog support for
other revisions; no claim about unqualified firmware or other CD titles is made.
Cartridge runtimes and title-owned file/SP/render services are unchanged.

## Replacement and complete operation contract

Sub `_BURAM` at 005F16 jumps to 00481E; D0.w selects the firmware branch table
at 004836. BRMINIT requires its reserved 0x640-byte workspace and 12-byte display
buffer. Its shared helper 0050D8..005174 constructs GF(64)/GF(256) power and inverse
tables using a quadratic exchange sort. The engine constructs the power sequence
and inverse permutation directly in O(count*bits), without executing or decoding
the old algorithm inside HLE. Both natural PRG and BIOS-menu Word RAM callers work.

Installation verifies the complete BIOS identity and copies the actual generated
entry metadata (MOVEM.L D6-D7,-(SP), words48E7/0300). Every call rechecks the full
156-byte helper identity in mutable PRG RAM, supervisor/non-trace state, the two
actual D2/polynomial profiles, writable ranges, return/stack validity and all
buffer/coefficient/stack aliases. The existing memory-region resolver admits
ordinary PRG and exclusively Sub-owned linear 2M Word RAM. Protected writes,
ownership waits, MMIO and noncontiguous 1M layouts retain original native execution.
No memory view survives the atomic operation or a bus/device callback.

The adapter reproduces every returned register/CCR bit and the saved D6/D7 stack
residue, including the real BIOS stack below006000. It preserves BRAM and all
other device state. Native completion charges40 cycles, an atomic computation
charge rather than the old sort duration. Normal next-operation IRQ service,
device deadlines, drive delays and host pacing remain intact. The generated
native single-instruction fallback retains its original24-cycle charge.

In the controlled real BRMINIT/FORMAT/BRMINIT oracle, these four helper calls
account for611972/612462 original instructions (99.92%) and6867144 cycles.
The remainder is490 instructions/6324 cycles. This selects the substantial
computation to replace; those counts are not a host CPU or whole-game speedup.

## Finite original-family assessment

Primary source: Sega MEGA-CD BIOS MANUAL call list, scans7?8, plus the owned
firmware dispatchers. The following groups cover all57 named manual services.
Retained means an evaluated decision for this supported engine/profile/workload,
not a claim that a service is HLE-implemented or could never be replaced.

| Services | ABI/state and selected decision |
| --- | --- |
| DRVINIT, DRVOPEN; MSCSTOP, MSCPLAY, MSCPLAY1, MSCPLAYR, MSCPLAYT, MSCSEEK, MSCSEEK1, MSCSEEKT, MSCPAUSEON, MSCPAUSEOFF, MSCSCANFF, MSCSCANFR, MSCSCANOFF; ROMREAD, ROMREADN, ROMREADE, ROMSEEK, ROMPAUSEON, ROMPAUSEOFF | Retain the CDBIOS command path. Entry005F22 ->002E1C distinguishes immediate/queued D0-selected work. Firmware command queues/parameter work areas, tray/TOC state, frame/sector positions, command acceptance and completion remain distinct from host media readiness. Eliminating real seek/sector/IRQ waits would alter that contract; the host-side read/verification work is already handled by E26/E27. Replacing the remaining packet/continuation work would still need to maintain those same guest-visible work areas and transitions, with no comparable redundant computation identified on the qualified boot/game routes. |
| CDBCHK, CDBSTAT, CDBTOCREAD, CDBTOCWRITE, CDBPAUSE; FDRSET, FDRCHG | Retain immediate status/TOC and volume/fade control. The status is firmware queue/cache state as well as drive state; directly returning host-ready would falsely acknowledge pending commands. Fade targets/speeds must use the existing timed fader and its busy/ready behavior. These bounded control/data operations do not contain the selected repeated table construction; a full live-code service guard cannot be justified by removing only their short request/status wrapper. |
| CDCSTART, CDCSTOP, CDCSTAT, CDCREAD, CDCTRN, CDCACK | Retain decoder/data-transfer orchestration, including sector validation, readiness, data pointers/counts, destination/Word RAM ownership, DMA and acknowledge edges. A host-preloaded sector does not establish decoder readiness or permission to expose/acknowledge it. The engine already owns typed sector/DMA execution and event deadlines; an additional BIOS rewrite would preserve that work and its firmware bookkeeping rather than eliminate a demonstrated comparable cost. |
| SCDINIT, SCDSTART, SCDSTOP, SCDSTAT, SCDREAD, SCDPQ, SCDPQL; LEDSET | Retain subcode buffers, availability/PQ sequencing and LED register updates. A cached TOC is not current P/Q/subcode at the emulated drive position. These bounded copy/status/register operations have no selected redundant algorithm; preserve their original firmware-visible buffers and control writes instead of substituting host cache state. |
| CBTINIT, CBTINT, CBTOPENDISC, CBTOPENSTAT, CBTCHKDISC, CBTCHKSTAT | Retain entry005F1C ->003D1A's D0-selected boot/control state machine. Initialization/cancellation, interrupt management, asynchronous tray/check completion and boot continuation cross calls and CPUs. Polling completion alone is not the boot result. E07's real event/IRQ/ownership scheduling is reused, not counted as BIOS HLE. Normal no-disc boot/menu and disc game startup exercise the retained control path. |
| BRMINIT; BRMSTAT, BRMSERCH, BRMREAD, BRMWRITE, BRMDEL, BRMFORMAT, BRMDIR, BRMVERIFY | Select the common table operation above. Retain namespace/directory/capacity, file/header/ECC/checking, read/write/delete/format semantics. Removing those transforms would change native-save contents. The controlled init/format sequence leaves only490 instructions outside the table work, while real menu formatting and game-native save fixtures are byte-identical. No additional BRAM replacement is selected: it would reproduce required storage work for marginal remaining init/format dispatch, rather than remove the demonstrated quadratic cost. |

E07 preserves real timer/raster/CDD/CDC/DMA/stamp/IRQ and ownership wake boundaries;
E22 owns PCM/CDDA publication; E26/E27 own media reads/cancellation/verified storage.
These underlying contracts support retained BIOS behavior. They are not claims of
whole-service HLE. In particular, E06's Z80 work does not dispose of Sub BIOS waits.

## Actual qualification and measured behavior

- Independent private oracle compares4 real original calls,12 SR/CCR/IRQ profiles,
  12 Word/mixed layouts and8 low-stack cases against original firmware. Every
  architecture field and the complete1,014,536-byte device image match.88 stack/
  argument and36 ownership/view declines make no changes;4 original fallback
  operations match their native24-cycle result. Private firmware bytes are not
  committed; public mathematical GF and standard SHA vectors are committed.
- Normal no-disc BIOS -> CD player -> memory information -> menu -> FORMAT ->
  YES -> FORMAT COMPLETE -> menu uses controller input only, starting with erased
  disposable RAM. Baseline/candidate match at8 meaningful picture checkpoints,
  remain native-only (floor0/0), and produce the same8192-byte formatted image
  SHA4390aad972163d9bbc8a8cd7d32e86b216e5a17a0d795c7d230942155e6a895b.
  Candidate naturally completes2 table calls with0 declines.
- Rebooting that persisted image shows0 saved items/125 free blocks; rebooting
  an actual QA Sonic CD native save shows1 item/114 free blocks. Both builds match
  and preserve every input byte. The game-created native-save SHA is
  94b1f2daef69a321f88fe634c8fb75604ad9d4769e34a268660451357d325a75.
- Sonic CD's existing3000-frame route completes8 table calls/0 declines with
  floor0/0, audioFaults0 and no queue underflow/overflow/backwards. Four gameplay
  images at candidate2066/2366/2666/2966 match baseline2100/2400/2700/3000 exactly:
  gameplay is34 frames (about0.57s) earlier on this route. The final source replays
  the qualified candidate's11 checkpoints,21 captured artifacts and full WAV
  bit-identically. Equal-frame baseline WAV/image differences are retained as
  loading-timing differences, not silently counted as equality.
- Final matched SERVER pair: frame0 startup CPU6.09375 ->6.078125s and wall
  8.349944 ->8.323623s; paced1800..3000 CPU14.09375 ->13.640625s, cycle count
  45,952,757,846 ->46,050,715,136, wall20.566360 ->20.562117s. Whole process CPU
  through3000 is34.59375 ->33.625s. Classify host CPU/startup as neutral/inconclusive;
  the mixed small cycle/CPU observations do not prove throughput improvement.
  The earlier PRG-only package's less favorable startup sample is also retained
  under its own package label. The proven user-visible gain is earlier gameplay.
- Windows MSVC19.41 and Linux GCC13.3 build the actual runtimes. Each passes the
  table/SHA vectors and media default/portable-provider tests (3/3). Linux also
  runs the real BIOS menu/native-save cases above. No new assembly/platform floor.

The atomic helper adds no pending semantic state. Four explicit completed-boundary
CPU/device snapshot/restore/replay checks reproduce every byte and charge; restored
completed snapshots have no latent operation. Diagnostic use counters are excluded
from semantic identity. Sonic CD's baseline standalone exposes no whole-game
save/load/rewind/netplay rollback API, so no end-to-end feature is claimed. Existing
cartridge snapshot code is unchanged; native-game-save preservation is qualified.

Earlier installer mask, accidental IPL7/PRG-only/stack restrictions and nonqualifying
runs are retained in raw records. They are corrected; no unused path counts as a
performance result. No benchmark host was required to be idle. Unattended outputs
were silent and no owner play window was opened for these tests.
