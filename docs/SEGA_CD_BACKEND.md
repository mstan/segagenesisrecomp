# Sega CD backend

The owner requested a general Sega CD backend after committing the adapted CD
campaign. Work is isolated on `feature/segacd-backend`. The S3K campaign was
delivered on its own branch in `731461a`; it is a C adaptation and does not boot
original CD programs. Neither branch is merged into master by this work.

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

Original US v1.10 BIOS execution established a neutral post-reset CDD handshake:
the initial status command receives ten nibbles `000000000F` until the first
valid nonzero command. Returning mounted-disc STOP/MSF immediately caused the
original sub program to reset repeatedly. The repaired sequence now reaches
TOC/seek processing and graphics initialization without changing owned bytes.

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
