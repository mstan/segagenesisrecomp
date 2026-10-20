# Sega CD backend

The owner requested a general Sega CD backend after committing the adapted CD
campaign. Work is isolated on `feature/segacd-backend`. The S3K campaign was
delivered on its own branch in `731461a`; it is a C adaptation and does not boot
original CD programs. Neither branch is merged into master by this work.

The new `runner/cd` device library is game-independent portable C. It takes an
owned BIOS; BIOS, disc images, generated program C and captures stay private.
The original-program runner and AOT module registration will use these devices.
No external emulator implementation is linked/copied into production.

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
That establishes code generation only; no original BIOS/disc execution is yet
claimed. The qualified BIOS SHA256 is
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
actual RAM/PRG/VRAM images and a PPM frame when stopping. The first run stopped
on missing CDD `$FF8037`; after the initial CDD implementation it reaches missing
CDC `$FF8005` after 865,419 main instructions and 13 sub instructions
(`build/qa-bios-native-dual-2`). These are explicit missing-device faults, not
successful boot. The probe still lacks disc/CDC/ASIC and Z80/audio rendering.
Its DMA stall sink currently counts stalls without applying their timing.

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
coordinator; they are not silent successes. Media streams, CDC delivery and
CD-DA are not connected yet. The register/protocol specification tests pass;
that does not establish complete disc support.

CDD sources: Sega Hardware Manual pp31-33 and the original authors'
[CDD_inf v2.0 reverse-engineering notes](https://gendev.spritesmind.net/forum/viewtopic.php?t=2061),
including the later correction that no-disc status is `$B`. Those protocol facts
are used without copying/linking external emulator implementations. CDC work
uses Sanyo's [LC8950/LC8951 Design Manual](https://bitsavers.org/pdf/sony/cdrom/CDU535-88_SLCD/LC8950_LC8951_Design_Manual.pdf),
including appendix E's original-LC8951 differences. Its private source SHA256 is
`afb95208d748c2ed9552b5ded3a86e57b831801ade2b25d32220173716383d0e`.
