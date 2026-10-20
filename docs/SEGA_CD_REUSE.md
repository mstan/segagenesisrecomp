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

## Other candidates

MAME's `src/mame/sega/megacd.cpp` and header identify BSD-3-Clause.
Musashi's portable 68000 implementation has an MIT grant. These are available
for focused implementation/reference work; neither has been imported here.
Any later import must pin the actual files, preserve their notices and
validate its integration contract.

Genesis Plus GX remains a private independent QA reference. Its general
license restricts commercial use and distribution of modified binaries;
that implementation is not copied into production.
