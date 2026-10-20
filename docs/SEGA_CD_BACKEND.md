# Sega CD backend

The owner requested a general Sega CD backend after committing the adapted CD
campaign. Work is isolated on `feature/segacd-backend`. The S3K campaign was
delivered on its own branch in `731461a`; it is a C adaptation and does not boot
original CD programs. Neither branch is merged into master by this work.

Current original-program evidence includes a complete 50-second strict native
Z80 run: 16,292,291 generated instructions with no miss or interpreter fallback.
Main/Sub deliberately used the diagnostic floor for that isolated check.
`build/qa-bios-z80-native-floor-gameplay-4/parent-canonical-run-manifest.json`
records inputs, executable and captures. The CDD counter `drive.frames` is a
75 Hz drive counter; it must not be reported as the video frame count.

The earlier hybrid Main/Sub run retires 79,562,450 native and 350,776 floor
operations. All 112 shared RAM, video and audio artifacts are byte-identical
to the CDDA-integrated floor run; only the execution log differs. Eight further
guarded catalogs now cover its remaining 62 Main and 146 Sub trace PCs, with
independent full-byte source/table/CRC/root checks. Transient MDINIT source
mapping and capture-derived BIOS extension identity limits remain recorded in
`build/qa-bios-native-cdda-movement-audit/parent-adversarial-review.json`.

These are separate qualification results. Combined strict native execution,
host audio, owner playtest, original-program saves/mechanics and final
independent visual comparisons must still pass. Earlier milestones below
retain their original limitations and are superseded only by specific later
evidence, not by a percentage of operations from one run.

The standalone now builds with MSVC. Its first three combined native runs
reach 124, 602 and 604 video frames, respectively, with nonzero Main/Sub/Z80
instruction counts, zero floor operations and zero audio faults. They stop
explicitly on uncovered runtime JMP variants; they have not yet established
playable original-game coverage. Exact captured instruction bytes generate
additional guarded variants without replacing guest state or relaxing guards.
Private run manifests and captures are under
`G:/Temp/Codex/segagenesisrecomp/sonic-cd-native/combined-native-{1,2,3}`.
The final captured RAM proves each instruction variant; its precise writer is
not established by a write trace. These are runtime observations, not a claim
that every original stage is covered.

All 26 synthetic tests pass MSVC, including the shared 68000 test. The GCC
Sega CD suite also passes. A separate integration harness using real ymfm and
SN76489 implementations verifies three frame sample boundaries, two flushed
intervals and timestamped PCM/CDDA silence without opening an audio device.
The new Linux sanitizer pass remains unverified: available WSL distributions
are backed by C:, and the installed MinGW lacks ASan/UBSan libraries. Earlier
Linux results apply only to their recorded revisions.

The native consumer uses `runner/cd/machine.c` with guarded Main/Sub catalogs
and `z80_native.c`; it never attaches the diagnostic floor or links the legacy
Z80 interpreter adapter. The shared Z80 core pin sequences word reads. Z80
time retains the rational `187500000/53693175` Sub-clock phase, and a third
coordinator clock ceiling prevents devices passing the running Z80 boundary.
Banked Z80 bytes use the CD Main map. An ownership WAIT runs the actual Sub
owner and retries only the denied byte; completed earlier effects stay
committed. A fault is terminal and cannot replay a partial instruction.

`audio_native.c` queues FM/PSG writes on the same absolute clock, preserves
chip phases and mixes timed RF5C164/CDDA at the exact rational PSG positions.
One video frame of host delivery latency supplies interpolation lookahead.
No SDL callback advances guest devices. Queue, clock and underflow errors are
sticky; window preflight prevents an incomplete mix reaching host playback.
Stopping ends delivery at the counted complete intervals rather than running
an extra guest frame to invent future source samples.

`backup.c` validates a complete 8192-byte file before replacing guest RAM.
Writes use an exclusive same-directory temporary file, flush/commit and atomic
replacement; a Windows replacement-denied test preserves the prior save.
This is host persistence, not proof of original BIOS-menu formatting or game
save progression. Those still require original-program tests.

The opt-in CMake `SEGACD_BUILD_NATIVE_RUNNER` target `SonicCD` accepts owned
`--bios`, `--disc` and optional `--backup` paths. It requires the qualified US
BIOS and disc identities. Window titles use `Sonic CD`, presentation starts
windowed, and owner audio defaults to volume 100. Arrows/Z/X/C/Enter and a
standard controller supply the original buttons; P pauses and Escape closes.
`--headless --frames N --mute`, repeated `--pad frame:duration:mask`, `--dump`
and `--wav` support reproducible private QA. Generated catalogs and inputs
remain private. Building the target alone is not gameplay acceptance.
An explicit backup path creates no AppData directory; the default save lives
beside the executable. Optional `--debug-port N` binds a loopback QA service;
`--start-paused` permits a client to attach before execution. Its logged memory
writes, held controller input, frame stepping and PPM captures run only between
frames. Neither the service nor its port changes the gameplay window caption.
Z80 I/O-space instructions stop explicitly as unsupported; they never alias
Genesis memory or silently perform an unrelated memory access.

The new `runner/cd` device library is game-independent portable C. It takes an
owned BIOS; BIOS, disc images, generated program C and captures stay private.
The explicit-PC dual-CPU coordinator and guarded AOT modules use these devices.
The stamp device adapts ares's ISC-licensed GPU algorithm; its provenance,
license and integration limits are recorded in [implementation reuse](SEGA_CD_REUSE.md).
The recompiled original CPU programs remain on this project's execution path.

The initial memory/control implementation covers BIOS, four PRG windows and
write protection, Word RAM ownership, 2M/1M mapping, cell/dot images and pixel
priority, odd-lane backup RAM, communication ownership, the stopwatch and
periodic/sub-CPU software interrupts, font expansion and the H-int vector.
Denied memory accesses return `SEGACD_WAIT`, for the dual-CPU scheduler to
resolve by running the owner. They never pretend to read/write shared memory.
Unsupported device ports return `SEGACD_UNMAPPED`; passing memory tests does
not establish CDC/CDD/audio/ASIC implementation or BIOS/disc boot.

Device timing uses the 12.5 MHz sub-CPU domain. Timers count in 384-cycle units;
the interrupt timer reload period is `(n+1)*384` with zero disabled. Main CPU,
sub CPU and media clocks must derive from emulated time, with rational clock
conversion retaining its remainder. No host-time disc shortcuts or loading
enhancements belong in game releases.

Primary specification: Sega **Mega-CD Hardware Manual**, version 1.0,
1991-10-14. [Original manual index](https://www.megadrive.org/elbarto/megacd/Official%20Sega%20CD%20Manual/segacd_toc.html).
Memory maps: pp14-18; reset/register/memory ownership: pp19-25 and 56-59;
communication/timers/interrupts: pp28-30 and 60; font expansion: p34.
The source scans are external references, not distribution inputs. Individual
pages use `MegaCD/Hardware/Cdh-NN.gif` relative to the index directory.

`tests/segacd` configures independently of the games and proprietary inputs:

```text
cmake -S tests/segacd -B build/segacd-tests
cmake --build build/segacd-tests --config Release
ctest --test-dir build/segacd-tests -C Release --output-on-failure
```

Tests use synthetic data and the manual's independent cell-map examples.
Independent hardware/original-BIOS observations must still qualify the timing,
mapping and edge behavior. CPU state, media and device state must participate
in deterministic snapshots/compatibility identity before any netplay claim.

Remaining backend acceptance includes the disc reader, CDC/CDD and CD-DA,
RF5C164, rotation/scaling, both original 68000 programs with AOT dispatch and
overlay identity/invalidation, scheduler/interrupt/reset integration, owned-BIOS
disc boot, actual original Sonic CD gameplay/audio/visual validation, persistence
and a standalone game consumer/build. The current device library is the first
implementation step, not a completed Sega CD backend.

Initial validation: the device test passes MSVC, MinGW GCC and Linux GCC with
ASan/UBSan, including every 1M cell-view byte, five cell bands, ownership denials,
protected writes, reset-pulse latching, backup lanes, read-only communication,
IRQ/timer and font contracts. These are specification tests, not independent
hardware observations.

The existing original 68000 recompiler also accepts the owned US v1.10 BIOS
without relaxing cartridge/header validation. Its private generation probe
finds 436 functions and reports 17 dispatch sites with zero unsupported sites.
That older function-generation check establishes generation only. The later
instruction-module probes below execute original BIOS programs. The BIOS SHA256 is
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.

The next execution seam uses explicit guest PCs and instruction-sized AOT
operations. Both CPU contexts and writable overlays need a safe scheduling and
invalidation boundary. A compiled operation must match the instruction bytes
in the currently mapped memory before running; a PC alone cannot identify code
that the disc has replaced. Unsupported/mismatched code must report a miss and
use the qualified correctness floor, never execute an old program or no-op.

Shared core branch `feature/segacd-flat-step` exposes the established native C
arithmetic/EA emitter with consumer-owned CPU/memory bindings. Synthetic real
opcodes compile into executable C and pass overflow flags, independent contexts,
postincrement RMW, long index, big-endian memory, real stack adjustment and
explicit rejection of control/privilege/exception instructions by the semantics
API. The operation API adds real guest BSR/RTS, JSR/JMP, Bcc/DBcc, SR/USP,
STOP/interrupt/RTE, traps, divide and CHK. Generated operations execute through
the shared portable CPU context, with current instruction-byte guards and
independent stacks/memory. Tests cover divide overflow at quotient 65536,
signed division/remainder, divide by zero entering vector five and CHK bounds.
The existing interpreter floor has the same unsigned-overflow fix; it reports
zero division loudly because its function ABI cannot construct that frame.

MSVC and MinGW pass the device/generated-operation/legacy-floor/CPU-context
tests. Linux ASan/UBSan also runs these synthetic tests. The first
semantics API preserved all 35 legacy BIOS generation artifacts byte for byte;
the later DIV fix intentionally changes generated division. No original BIOS
execution is inferred from these tests. Group-zero exception frames, dynamic
cycle timing and the general-system correctness floor still need
implementation/qualification.

The original-program [module frontend](M68K_STEP_MODULES.md) now emits native
steps, sorted descriptors and byte guards for a caller-selected load base and
CFG roots, or all aligned words. Eight synthetic tests pass MSVC, MinGW and Linux
ASan/UBSan. Independent loop expectations cover registers, stacks and exact
cycles; replacement/extension mismatches leave CPU state untouched. This does
not supply a correctness floor for code absent from the registered modules.

`segacd_coordinator` now runs independent context-bound main/sub programs. It
uses the NTSC master/7 and 12.5 MHz sub clock with a retained rational remainder;
elapsed timelines are separate from retired instruction cycle counts. Word RAM
WAIT runs the actual owner, preserving the waiting operation's existing bus
effects. STOP advances emulated timers and host event sources, including while
the other CPU waits. Reset pulses reload SUB vectors without erasing PRG.
The host-clock callbacks are the integration seam for Genesis VDP/Z80 events.

Generated synthetic main/sub programs qualify actual Word RAM grant/write/
return/read, independent registers, timer wake during a pending memory WAIT,
autovector frames/RTE, reset latching, host IRQ wake and precise all-STOP host
deadlines. An absent module during WAIT halts the waiting CPU and retains the
owner CPU's miss/PC in diagnostics; it cannot restart a partly executed
instruction. The coordinator/device/core-context targets use MSVC `/W4 /WX`;
the portable suite also passes MinGW and Linux ASan/UBSan. PAL clocks,
instruction/bus-cycle phase accuracy, reset during
an in-flight wait, full machine serialization/netplay and original dual-program
boot remain unqualified. This coordinator is not complete disc/device support.

The private `bios_probe` is an optional main-only bring-up driver, not the game
runner or a release target. It uses the actual Genesis bus/VDP and CD memory,
but has no sub-CPU/VDP clock scheduling, Z80 execution or audio rendering. The
owned BIOS CFG contains 558 native operations and zero unsupported operations.
Its first execution probe retired 2,000,000 original BIOS instructions and
21,048,508 nominal cycles, initialized devices/released the sub CPU, then polled
at `$000A1A/$000A1E` for VBlank (`$FFFE26`). It neither establishes complete boot
nor qualifies the absent devices/audio. Private logs are under
`build/bios-step-probe-{build,run}.log`; original-program C stays under build.

The optional `bios_dual_probe` now connects both native original BIOS programs
and main RAM handlers to the coordinator and real VDP raster events. It emits
actual RAM/PRG/VRAM images and a PPM frame when stopping. Its latest no-disc run
(`build/qa-bios-native-dual-8`) reaches 60,000,006 sub clocks, retiring 2,939,153
main instructions and 3,718,427 sub instructions without a missing operation or
device fault. The actual frame shows the US v1.10 BIOS Earth/copyright screen.
Registered catalogs cover all aligned words in the 128 KiB BIOS, the initial
64 KiB resident sub program, main RAM jump handlers, and the handlers the sub
BIOS writes to RAM. Catalog bytes are never injected into runtime memory;
the original programs write memory and every operation guards its current bytes.
Scanning data as candidate opcodes does not establish executable-code coverage.
Disc boot, Z80/audio output and ASIC work remain incomplete. Its DMA stall sink
counts stalls without applying timing; Word RAM VDP DMA's documented delayed
read also remains pending. This is a diagnostic, not a release consumer.

The upstream `rf5c164` chip takes explicit contexts and emits one stereo sample
per 384 sub clocks. It covers channel/wave banks, odd-lane CPU mapping, wave
RAM, active-low channel control, fractional addresses, loop markers, position
reads, stereo envelope/panning, full 16-bit sum/limiter, and reset preserving
external wave RAM. The sample kernel was extracted from the qualified campaign
chip, without its game driver. Audio sinks are optional and host-owned; muted
validation still advances chip state. Primary specification: Sega PCM manual
sections 4.1-4.4 and 4.8-4.9, linked from the original manual index above.

Independent qualification `build/qa-upstream-pcm-original/results.json` runs the
owned original sound driver against the new upstream chip and an external
reference chip (Genesis Plus GX `49c584764893b0505ac7f768a754f97330fa4392`, QA only).
All nine music banks pass at 180 seconds each, plus the 28-second voice/stop
sequence: zero driver-state differences, correlation at least 0.9999999528 and
at most seven LSB of sample error. The permitted eight-LSB bound accounts for
the documented per-channel multiply/truncation order. The script, input/source/
DLL hashes and raw streams are retained privately. This qualifies sample/chip
behavior under atomic 512-sample driver service, not hardware bus phase,
analogue filtering, the future host resampler or complete original-game audio.

The provisional `cdd` library implements gated host control, 10-nibble packets
and complement checksums, bounded TOC metadata/queries, drive position,
seek/read/pause/resume/scan/tray state, 75 Hz status/sector cadence and IRQ4.
Fader register data-transfer busy is modeled; its audio gain/envelope is not.
Mechanical/packet/fader delays and scan behavior remain provisional and need
independent qualification. Unsupported commands are recorded and halt the
coordinator; they are not silent successes. Raw media now feeds the CDC and a
timed CD-DA sector callback; the audio resampler/fader/output is not implemented.
The register/protocol specification tests do not establish complete disc support.

CDD sources: Sega Hardware Manual pp31-33 and the original authors'
[CDD_inf v2.0 reverse-engineering notes](https://gendev.spritesmind.net/forum/viewtopic.php?t=2061),
including the later correction that no-disc status is `$B`. Those protocol facts
were implemented from their specification. CDC work
uses Sanyo's [LC8950/LC8951 Design Manual](https://bitsavers.org/pdf/sony/cdrom/CDU535-88_SLCD/LC8950_LC8951_Design_Manual.pdf),
including appendix E's original-LC8951 differences. Its private source SHA256 is
`afb95208d748c2ed9552b5ded3a86e57b831801ade2b25d32220173716383d0e`.

Further default CPU fixes preserve A7's two-byte increment/decrement for byte
EAs and execute CMPM's source read/increment before its destination read when
the registers alias. Generated native steps and the existing interpreter pass
the same architectural expectations; a separate Unicorn original-MC68000
probe agrees for all three byte-stack/alias cases
(`build/qa-stack-byte-unicorn.json`). Broader cartridge/default-path regression
qualification remains necessary before any merge.

The `cdc` layer implements original LC8951 address-register sequencing,
active-low interrupt flags/masks, a 16 KiB sector ring, header/subheader/status
reads, a two-byte host FIFO and CPU-specific gate-array host reads. DAC advances
on FIFO loads; DBC counts actual host byte reads and DTEI occurs at final
consumption. Gate-array EDT clears on destination selection. DMA destinations
still reject transfer starts explicitly; their arbitration/timing is not guessed.
Decoder IRQ pulses use appendix E's approximate 2750/1800/1500 us profiles,
converted to sub clocks. Exact serial input, decoder pipeline delays and ECC
correction are unqualified. A bad EDC is reported, never silently repaired.

The [media format and converter](SEGA_CD_MEDIA.md) preserve raw program/audio
sectors and stored/virtual audio pregaps. The portable reader validates exact
bounds, TOC and the entire payload SHA-256 before mounting. The owned 35-track
image was independently compared against all source files and all 255,976
payload sectors, including 5,126 stored INDEX 00 sectors and 150 lead-in sectors.
The C reader separately read every sector; all 59,261 MODE1 program sectors
passed ECMA-130 EDC. These are media identity/integrity checks, not gameplay.
`segacd_advance_io` delivers sectors on the CDD's emulated event boundaries.
Synthetic tests qualify long/split advance equivalence, CDC data/EDC delivery,
timed raw audio events, finite decoder pulses and read-failure diagnostics.
Missing/truncated media halts the coordinator instead of supplying zero sectors.

The current-byte execution floor now uses the same explicit CPU context,
exceptions, WAIT and trace boundary as guarded AOT operations. It decodes current
writable RAM, so a module miss cannot execute stale generated code. The probe
always attaches this floor; there is no correctness toggle in a launcher.
Synthetic tests compare complete CPU/memory/cycle state against native loops,
exercise partial-instruction WAIT, stack-byte/CMPM aliasing, privilege/traps,
and verify CLR's actual discarded read before its zero write. Register-count
ROX and count-zero carry fixes pass 4,992 independent Unicorn seeds across all
24 register-shift forms. Group-zero exception frames, data-dependent arithmetic
cycles and broader cartridge regression remain pending before any merge.

Original US v1.10 BIOS execution requires an initialized status-zero packet,
`000000000F`. The first bring-up masked the incorrect internal STOP=9 state
behind a synthetic reset-handshake response. That gate has been removed:
STOP is now zero, nine is TOC reading, and a normal complement checksum gives
the initial packet. Status polls leave the initialized motor stopped; a TOCT
request starts TOC acquisition. Stop clears stale report fields. Subsequent
motor/TOC progression uses the explicitly provisional 75-Hz profile described
in [the reuse notes](SEGA_CD_REUSE.md), rather than holding status nine forever.
This is device bring-up evidence, not proof that the original game boots.

The corrected drive sequence initially delivered 64 MODE1 sectors without
enabling decoder buffering. Canonical virtual MODE1 pregaps, actual negative-LBA
delivery and the pinned ares three-sector stream-acquisition profile now let the
BIOS complete 52 real CPU-port transfers. Run `build/qa-bios-floor-disc-9`
reaches 166,217,096 sub clocks, delivers 153 media sectors (152 decoder sectors),
and shows the BIOS Start prompt followed by the original game's SEGA licensing
screen. It then halts on unsupported CDD command A, packet `A000020B08` (radial
track skip). This is startup progress, not a title-screen/gameplay or final
visual acceptance claim. That run uses the execution floor; private catalogs
for the original disc-loaded boot programs are being added separately.

The current 16 synthetic tests pass MSVC, MinGW and Linux ASan/UBSan. The
earlier 64-sector native-catalog run matches its floor-only run in all nine
retained RAM/VRAM/frame artifacts after approximately 19.3 million operations;
it does not establish native execution of the newly loaded game programs.
The gap encoder's independent integrity checks and unchanged stored disc bytes
are documented in [the media notes](SEGA_CD_MEDIA.md). The provisional seek
profile and reference discrepancy are documented in [the reuse notes](SEGA_CD_REUSE.md).

Command A now performs radial movement with explicit Tracking/Pause states.
Its count is hexadecimal spiral turns; either nonzero direction nibble selects
inward movement. A first-party fixed-point functional model uses nominal 25-mm
program origin, 1.6-micrometre pitch and 1.30-m/s scan velocity from the ranges
in ECMA-130 sections 8.6 and 11. Movement now takes 1,250 sub clocks per turn
(10,000 turns/second, nominal 16 mm/s); status/IRQ frames remain at 75 Hz.
This is a deterministic functional timing profile, not measured original-drive
calibration. The former one-turn-per-75-Hz profile made an observed 12,929-turn
request last 172 seconds, exceeding the original BIOS's roughly ten-second
wait. The revised request takes 1.2929 seconds while preserving intermediate
positions, interruption and retargeting. Tests exercise both coincident and
off-phase command/movement events and verify that movement does not add IRQs.
Positions clamp to the represented first pregap and final stored sector.
For long accepted images the nominal geometry extrapolates beyond the standard
user-zone radius; the TOC remains the addressing bound. A replacement seek
uses the position already reached. Invalid seeks preserve ongoing movement,
and a zero count preserves the exact logical position. The permissive ares
TrackSkip handler is a TODO/no-op and was not used as an implementation.
Synthetic tests cover interruption, bounds, direction and divided time advance.
An independent SI-unit floating-point calculation checks 180 position/direction/
count/disc-size cases against the fixed-point model, with maximum discrepancy
1.522 sectors within its analytically bounded radius/sector quantization.
These checks qualify the selected geometry, not actual Sega motor timing.

The next original-program run (`build/qa-bios-native-disc-10`) passes the former
TrackSkip halt, completes 75 CPU-port transfers and reaches 375 million sub
clocks (30 seconds), retiring 31,159,569 guarded native operations plus
14,289,796 floor operations. Optional private boot catalogs cover the original
system-area Main image at `FF0000` and SP image at `006000`; the running BIOS
loads the bytes, and candidate operations still guard current memory.
Later overlays and BIOS-written handlers remain incompletely cataloged.
`SEGACD_PROBE_REQUIRE_NATIVE=1` disables the diagnostic floor and exposes the
first catalog miss at Sub `005F28`; native-only startup is not yet accepted.

The floor-only comparison initially stopped at valid `MOVEP.L` in the BIOS
backup-RAM reader. The floor now performs the instruction's ordered alternating
byte transfers, signed displacement, preserved flags and word-register upper
half, using 16/24 nominal clocks. Tests cover all four forms, odd/even addresses
and a WAIT after the first completed byte. The architectural contract is in
Motorola's [Programmer's Reference Manual, MOVEP pp4-131..133](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).
Its focused test passes MSVC, MinGW and Linux ASan/UBSan, following the full
16-test passes on all three toolchains for the drive/media changes.

With that fix, `build/qa-bios-floor-disc-11` and the native run have byte-identical
contents in all 18 shared RAM/CDC/VRAM/frame artifacts and identical final CPU,
cycle and device observations after 45,455,995 coordinator steps. Evidence is
`build/qa-bios-native-disc-10/native-floor-comparison.json`. Both end on a black
frame: Main loops at `FF215A`, while Sub polls Word RAM's DMNA at `0101BA`.
The diagnostic now retains the first unconditional self-loop's predecessor PCs,
registers and RAM snapshot. The path into that loop remains under investigation.
No original-game title screen, playable stage, complete audio, final independent
visual acceptance or native-only gameplay is claimed by that milestone.

The error path is now identified from original instructions and memory writes:
BRAMINIT requests BIOS BRMINIT, which rejects the initially erased internal
backup RAM as unformatted. D1=1 and carry identify that result; D0=2 is capacity
information, not error code two. An independent erased-RAM reference renders
the original formatting warning. With the corrected movement rate, a fresh
erased-RAM run (`build/qa-bios-erased-fast-floor-1`) also renders that warning.
Its 17 colors have a one-to-one pixel-position mapping to the independent
frame-1800 capture: geometry matches, but DAC/presentation colors differ
(mean RGB absolute error 1.4078). Neither a raw pixel match nor final visual
acceptance is claimed. The previous black result is retained as earlier evidence.

A private synthetic caller executes the original BIOS's BRMINIT service 0,
BRMFORMAT service 6, then BRMINIT again. It executes 612,462 instructions and
produces an 8-KiB persisted image with SHA256
`4390aad972163d9bbc8a8cd7d32e86b216e5a17a0d795c7d230942155e6a895b`.
All 70 backup-byte writes originate in the original BIOS. Parent review replays
the logged writes from erased RAM and reconstructs the exact image. This is a
legitimate persisted input for further bring-up; formatting through the normal
BIOS menu remains bypassed and unverified.

Using that input and the corrected movement rate,
`build/qa-bios-preformatted-fast-floor-1` reaches 625 million sub clocks
(50 seconds), completes 543 transfers and reads 2,144 media sectors, including
721 decoder and 1,422 CD-audio sector events, without a CPU/device fault.
The original opening movie now renders. Main RAM contains the original
`OPEN_M.MMD` body at `FF2000`, and Sub PRG contains `OPEN_S.BIN` at `030000`.
This run uses the diagnostic floor; its image has not passed an independent
visual comparison and audio sectors are not audible output. A matched reference
run with only the early Start pulse remains at the title at frame 3000; it
requires a later real Start press to enter the menu/opening sequence. That input/
startup-flow difference remains unqualified. Full stage/mechanics acceptance,
native-only execution, complete sound and release integration remain
unaccepted. The 16 synthetic tests pass MSVC, MinGW and Linux ASan/UBSan with
the revised drive timing.

The corresponding guarded-native run,
`build/qa-bios-preformatted-fast-native-1`, retires 33,218,750 native and
37,385,999 diagnostic-floor operations. All 22 RAM/VDP/frame artifacts are
byte-identical to the floor-only run, with identical final CPU/device
observations after 70,616,284 coordinator steps. The comparison manifest is
`native-floor-comparison.json` in that directory. This qualifies agreement
between these execution paths for the run, not native-only coverage or
independent original-game fidelity.

Ordinary Start presses at frames 1500, 1650 and 1800 reach Palmtree Panic in
`build/qa-bios-scripted-floor-1`. The follow-up
`build/qa-bios-scripted-floor-movement-1` holds Right at 2350..2899 and A at
2450..2459: captures show Sonic moving through terrain, camera scrolling,
the loop traversal and 13 collected rings. It reaches 625 million clocks
without a fault. These are initial diagnostic-floor gameplay observations;
full collision/mechanics/audio coverage, independent matching input runs and
all 70 original stage/time variants remain work. No progress/success flags
were injected. The existing campaign's coverage does not qualify this backend.

The private dual probe accepts `SEGACD_PROBE_BRAM_IMAGE` as an exact 8,192-byte
persisted input; missing/short/long/unreadable input fails rather than formatting
or repairing it. Final dumps include backup RAM, VDP registers, CRAM and VSRAM
as well as the existing RAM/frame evidence. `SEGACD_PROBE_WATCH_WORD_BYTE` and
`SEGACD_PROBE_WATCH_AFTER` optionally log changes to a physical Word RAM byte,
with the neighboring bytes and CPU observations; they never change guest state.
`SEGACD_PROBE_PAD_EVENTS` supplies up to 64 real P1 button events as
`frame:duration:mask` entries separated by commas (decimal frame/duration,
decimal or `0x` 12-bit mask). Overlapping intervals OR their buttons. The
existing CLI Start pulse remains unless explicitly suppressed with
`SEGACD_PROBE_PAD_EVENTS_OVERRIDE_START=1` alongside an event list.
`SEGACD_PROBE_SNAPSHOT_FRAMES` replaces the default capture schedule with up to
64 unique decimal frame numbers; custom captures include Main/Sub RAM. Invalid
or overflowing input fails before boot. `SEGACD_PROBE_INPUT_SELFTEST=1` checks
and prints event boundaries before opening BIOS/media, for private QA only.

The custom snapshots now also retain physical Word RAM, VDP registers,
CRAM/VSRAM (explicitly host-u16), Z80 RAM, memory mode and fader state.
`SEGACD_PROBE_FLOOR_PC_LIMIT` accepts 1..4096, defaults to 16 and bounds printed
unique PCs per CPU per custom-snapshot epoch. A normalized even-24-bit-PC bitmap
avoids quadratic searches, while total unique counts remain visible beyond the
print limit. Only unambiguous single-retirement steps are counted; a PC/opcode
is not program identity, and an operation spanning the host snapshot edge may
be assigned to an adjacent epoch.

`SEGACD_PROBE_CAPTURE_AUDIO=1` records raw RF5C164 signed-16 LE stereo and raw
2352-byte CDDA sectors with separate index/time CSVs, without speaker output or
a fabricated final mix. Fader word/deadline transitions are observed after each
coordinator step, including same-word low-byte deadline refreshes. PCM sample
indices run at 12.5 MHz / 384; a reset starts a new index epoch whose origin is
estimated at the observed coordinator boundary, not an exact reset bus cycle.
The capture also includes post-fader CDDA signed-16 LE frames and their actual
device subclocks. Its CSV labels the fader value **after** that frame's ramp
step; the frame itself used the preceding coefficient. Capture open/write/flush
failures fail the run. These controls are diagnostic only and do not implement
Z80 execution or host mixing.

`SEGACD_PROBE_CAPTURE_MAIN_PC` optionally captures one even 24-bit Main PC,
after an unambiguous successful retirement and before another CPU executes.
It retains RAM/VDP/Z80 state without changing execution. Capturing `FF1000`
at clock 218704055 exposes a transient routine and an earlier resident page
that later frame captures had already lost. Its guarded runtime catalogs
remain identified as captured original-program RAM; one matching interior
IPX block does not establish the enclosing routine's full file provenance.

`build/qa-bios-audio-observation-floor-movement-1` completes the same 50-second
input schedule with all 46 baseline artifacts unchanged. Parent review verifies
1,627,603 PCM frames against every CSV sample and all 1,649 captured CDDA
sectors against the owned image at their recorded LBAs. It observes 29 fader
writes (58 word/deadline transitions): initial mute from Sub PC `0002B6`, then
the original BIOS service at `002D9C`, including unity `4000`, normal `3100`,
the game's intermediate fade targets and mute. `parent-adversarial-review.json`
retains the checks. This establishes source delivery and program requests,
not complete audio or physical timing correctness.

The portable `fader.c` unit implements the encoded attenuation curve from
[Sanyo LC7883K/KM EN3884B, p9](https://www.tautec-electronics.de/Datenblaetter/Schaltkreise/LC7883KM.pdf),
cross-checked against Sega's Hardware Manual p31: unity at `4000`, 1024 field
steps to mute, one field step per DAC stereo frame. Both channels use the same
coefficient before stepping. Nonzero upper coefficients use the upper field
divided by 256; the last three codes use 1/1024, 2/1024 and 3/1024. This preserves
the documented gain plateaus instead of treating the whole field as linear.
Fixed-point output rounds nearest/ties away from zero and saturates explicitly;
that rounding policy and above-unity behavior have not been measured on hardware.
The unit now drives the device's CDDA sample callbacks. EFDT remains the
existing separate CDD transfer status/deadline. `cdda.c` queues two raw sectors,
decodes 588 little-endian stereo frames per sector, and schedules samples with
an integer 12.5 MHz / 44.1 kHz remainder. A first audio sector establishes the
source phase, with its first frame 284 subclocks later. This startup phase is
a functional profile, not measured DAC phase. Integer sector intervals can
contain 587, 588 or 589 samples; the FIFO preserves continuous source order
and the cumulative 44.1 kHz rate. The stream advances inclusively to each CDD
event before exposing the sector delivered at that event.

Mapped fader writes latch the target on the low byte. Actual CDD command
changes flush pending audio on stop/pause/seek; resume waits for a newly
delivered sector. Data-track delivery clears the audio stream, using the
delivered LBA rather than prematurely discarding the final audio sector when
the drive's next-sector LBA crosses into data. Read errors, FIFO under/overflow
and unimplemented emphasis/double-speed modes are explicit device faults;
they do not silently generate successful audio. Emphasis, double speed and
the owner-facing host mix/Z80 path remain unfinished.

The 20-test suite passes MSVC, MinGW and Linux ASan/UBSan, including real
mapped packets/fader writes through the media-to-DAC path, transport flushes,
the complete final audio sector and reset/error handling. The original-program
run `build/qa-bios-cdda-integrated-floor-movement-1` reaches 625000002 subclocks
and emits 2,204,999 captured CDDA frames (including inactive silence), with
zero source underflow or device error. This is still diagnostic-floor gameplay
and device output, not full Z80/host audio or an owner-facing release.
All 46 baseline RAM/VDP/frame artifacts and the earlier raw PCM/CDDA streams
remain byte-identical. Independent arithmetic checks match 968,875 source
frames to their exact sector samples and preceding fader coefficient. The
462-subclock gap in inactive output is explained by the original Sub reset:
its peripheral-ready deadline gives reset clock 13962764, and restarting the
sample phase places the first new tick at 13963048 after the old 13962586 tick.
`parent-reset-phase-review.json` retains that calculation. This confirms the
implemented reset profile, not the physical DAC's reset phase.

The instruction generator accepts `--entries-file PATH` with whitespace-separated
validated entry addresses, combined with positional entries or other files.
This avoids Windows command-line limits while keeping one deduplicated CFG
catalog. Synthetic tests require byte-identical output to positional roots,
10,000 duplicate entries without a long command line, and invalid/missing input
failure before output files are created. The private Palmtree Panic catalog
now covers 7,795 native entries from 4,917 source-matching roots in one module;
its guards match all eight captured resident Word RAM images. Compilation and
native-only runtime acceptance are separate checks.

Additional private native overlays use the CMake cache lists
`SEGACD_BIOS_PROBE_EXTRA_MAIN_MODULES` and
`SEGACD_BIOS_PROBE_EXTRA_SUB_MODULES`, with entries
`absolute-generated-directory|C_identifier_prefix`. CMake validates the module,
header and 32 shards and generates a registry header. Existing current-byte
guards remain mandatory. Extra directories are not target-wide include paths:
adding an overlay must not invalidate every previously compiled catalog.
The first strict native miss at Sub `005F28` is a relocated BIOS jump stub;
the registered earlier image targets `020C`, while actual bytes target `602A`.
Rejecting that entry is correct; the relocated variant requires its own guarded
catalog rather than weakening the guard. Registering the exact captured four
relocated jump stubs gets past that miss; the next strict run stops at Sub
`018014`, with 1,053,757 native operations and zero floor operations. Broader
native coverage remains work.

The [ISC ares GPU port](SEGA_CD_REUSE.md) implements trace-vector stamp drawing,
2M Word RAM map sizes, 16/32-dot stamp cells, rotations/flips, repeat/transparent
bounds, fixed-point sampling, image-buffer offsets/vertical stride, pixel
priority, asynchronous busy/remaining lines and IRQ1. CPU priority now applies
to the 1M dot-image aperture; direct RAM writes remain ordinary memory. The
15-test suite passes MSVC, MinGW and Linux ASan/UBSan. Twenty independent
Genesis Plus GX reference cases match the entire output Word RAM, including
both stamp sizes and all transforms. The initial worker fixture set used an
incorrect STS bit and address assumptions; it was rejected and preserved under
`build/qa-stamp-reference/invalid-initial-fixtures`. Only the corrected manifest
and `build/qa-stamp-synthetic/reference-comparison.json` support these claims.

The next private original-BIOS run, `build/qa-bios-floor-disc-2`, reaches
90,000,003 sub clocks (7.2 seconds), retiring 5,102,454 main and 4,170,003 sub
instructions without a CPU/device fault. It starts 614 real ASIC operations
and writes 7,462,288 pixels. Its actual captured BIOS logo still differs from
the independent reference, and no sectors have been read. This is not a
validated disc boot. Word RAM-to-VDP DMA's documented one-word delay, actual
DMA stall charging, CDC DMA, full audio/runner integration and original-game
qualification remain work. These captures do not satisfy final visual acceptance.

The following CDC DMA milestone adds destinations 4/5/7 using the
[permissive reference profile](SEGA_CD_REUSE.md), with emulated six-clock word
boundaries, full internal address retention, PCM/PRG/Word mapping, ownership
WAIT without source consumption, actual final-byte EDT/DTEI and IRQ5. Odd-byte
requests still stop explicitly. Device errors now also halt all-STOP execution;
they cannot disappear behind an idle deadline. A real port-driven PRG test
programs IFCTRL/DBC/DAC/DTTRG, crosses the six-clock edge, checks transferred
bytes and actual completion/ack. Unit cases separately cover bank/address
boundaries, write protection, ownership resume and split advances. All 16
current tests pass fresh MSVC, MinGW and Linux ASan/UBSan. This does not qualify
physical DMA contention or all hardware transfer edge cases.

The VDP Word RAM output latch and main CPU freeze charging repair the visibly
scrambled BIOS logo. Run `build/qa-bios-floor-disc-3` reaches 190,000,002 sub
clocks (15.2 seconds), retires 10,132,645 main/9,202,149 sub instructions and
starts 1,572 ASIC operations with no fault. Its SEGA/SEGA CD logo shapes now
render, but the Start prompt remains missing and no sectors load after real
controller Start at frames 600-609. The frame-360 diagnostic comparison against
the inserted-disc reference still differs (mean RGB absolute error 4.0016),
and is not final visual acceptance. A later protocol correction fixes decimal
track units for tracks 10-99 and missing data flags in time reports; tests
cover all 99 track queries. The next owned-BIOS run is pending.
