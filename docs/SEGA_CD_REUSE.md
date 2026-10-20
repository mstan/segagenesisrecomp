# Sega CD implementation reuse

The owner requested selective permissive implementation reuse to expedite the
general backend. Existing qualified memory/media/CPU work remains; further
device work can adapt suitable licensed implementations. The recompiled main
and sub programs still execute through this project's explicit CPU contexts
and coordinator. Device reuse does not replace those programs with another
emulator's CPU or game-specific high-level calls.

## ares graphics device

`runner/cd/stamp.c` adapts graphics sampling, map alignment, coordinate
transforms, priority and the five-clock-per-dot line profile from
[`ares/md/mcd/gpu.cpp`](https://github.com/ares-emulator/ares/blob/9408cb43d4948fc3ea6e152a307a34348df3fe04/ares/md/mcd/gpu.cpp),
pinned commit `9408cb43d4948fc3ea6e152a307a34348df3fe04`.
Register mapping was cross-checked against that revision's `io-internal.cpp`
and Sega's Hardware/Software Development manuals. The latter resolves the
swapped large-map captions in Hardware p36. The ares implementation is under
ISC; its complete applicable notice is retained in the source and
`runner/cd/ARES-LICENSE.txt` and must accompany binaries using the device.

The port uses C11, explicit state, bounded integers and big-endian physical
Word RAM. It imports no ares CPU, thread/coroutine, UI, VFS, Node audio or nall
type implementation. Host/media pointers remain outside deterministic device
state. Unrelated licenses collected in ares's root LICENSE do not replace
the applicable ares ISC notice.

The initial port schedules a complete line after five clocks per output dot,
as ares does, and raises IRQ1 after the final line. This is a functional
profile, not measured hardware arbitration/refresh accuracy. Register writes
during active work, zero-size operations and ownership loss during work still
need qualification. Normal execution is asynchronous in emulated time, with
busy and remaining-line registers; completion is not injected by a test.

Private source downloads, hashes and licensing audit are under
`build/reference-permissive`. Independent comparisons remain QA tools, never
release dependencies. Agreement with a reference for stated cases does not
establish hardware identity.

## CDC DMA and transfer integration

`runner/cd/dma.c` adapts destination and address stepping from ares's
`ares/md/mcd/cdc-transfer.cpp` and its six-sub-clock word cadence from `mcd.cpp`
at the same pinned revision. Its ISC notice is retained in that source and the
shared license file. The port consumes the existing LC8951 FIFO only after a
successful mapped destination write. It retains the full internal address
between transfers, including low bits absent from the programmed A18..A3
register. PCM advances four internal address units per word, PRG/Word RAM two.
DMA uses the current RF5C164 bank or the sub CPU's mapped PRG/Word RAM view;
denied Word RAM ownership waits without consuming source bytes.

The clock splitter interleaves DMA boundaries with PCM, stamp and media events.
At a coincident PCM-sample/DMA-write boundary it samples before applying the
write; exact hardware phase/contention is unqualified. Odd-byte DMA requests
are explicitly unsupported rather than padded or silently truncated. Destination
write protection and ownership use this backend's bus contract; the reference
profile alone does not establish physical arbitration or CPU contention costs.

The host VDP callback additionally models Word RAM's one-access output latch,
documented by Sega Hardware p61 and corroborated by ares `bus-external.cpp`.
Normal CPU reads are unaffected. Initial CPU-prefetch latch content remains
unqualified. The existing Genesis VDP DMA freeze estimate is now charged to
main-CPU retirement and elapsed time instead of only counted in diagnostics.

The original CDD implementation's report fields were cross-checked against
ares: time reports identify data tracks and track-start reports return decimal
track units.

The motor/TOC progression additionally adapts `cdd.cpp` and `mcd.hpp` from
that revision: motor stop is status zero, status nine means reading the TOC,
and the 75-Hz device clock advances a started motor through TOC reading to pause. Stop
clears the prior report fields; time information is populated by a subsequent
request. The ISC notice is retained in `runner/cd/cdd.c`. The two-step profile
does not establish physical motor spin-up or lead-in scan latency. Initial
reset packet behavior remains separately covered by the original BIOS probe.
Initialized status remains zero until a drive operation; the TOCT request
starts TOC acquisition. This follows the later controller-source analysis by
[Eke](https://gendev.spritesmind.net/forum/viewtopic.php?p=35482), rather than
the earlier notes' incorrect description of status nine as stopped. The
synthetic reset-packet override has been removed; status zero and cleared
report fields naturally produce the checksum-valid initial response. The
immediate TOC metadata and transition cadence remain functional modeling,
not measurements of a physical lead-in scan.

Seek-play and seek-pause also begin three sectors before the requested address,
clamped to the first represented pregap, following that pinned ares revision.
Negative-LBA MODE1 sectors now reach the decoder with their actual raw headers
and integrity result. This is a provisional stream-acquisition profile. An
independent Genesis Plus GX trace instead begins at the requested LBA and
issues invalid decoder callbacks during seek. The Sanyo manual describes
block/interrupt progression from received bytes; it does not establish decoder
interrupts with no arriving input. No synthetic zero-header decoder ticks have
been added to imitate that reference. Physical stream acquisition and decoder
pipeline timing remain unqualified; neither software profile proves hardware
identity.

## Other candidates

MAME's `src/mame/sega/megacd.cpp` and header identify BSD-3-Clause.
Musashi's portable 68000 implementation has an MIT grant. These are available
for focused implementation/reference work; neither has been imported here.
Any later import must pin the actual files, preserve their notices and
validate its integration contract.

Genesis Plus GX remains a private independent QA reference. Its general
license restricts commercial use and distribution of modified binaries;
that implementation is not copied into production.
