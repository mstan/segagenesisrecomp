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
explicit rejection of control/privilege/exception instructions. MSVC, MinGW and
Linux ASan/UBSan pass. The legacy BIOS generation remains byte-identical in all
35 C/header artifacts. This seam is not yet the complete step frontend: CPU
control, privileged instructions, exceptions, cycle accounting, code guards,
registration and the dual-CPU scheduler still need implementation/qualification.
