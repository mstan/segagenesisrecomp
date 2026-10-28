# Sega CD raw media

The general backend uses owned BINARY BIN/CUE images. Conversion preserves raw
2352-byte sectors; it does not extract game files or adapt game content. Owned
images, BIOS files, generated code and captures belong outside source control.

```text
python tools/segacd/disc_image.py "owned-disc.cue" "private/disc.sgcd"
segacd_disc_probe "private/disc.sgcd"
```

The converter accepts consecutive tracks 01–99 with MODE1/2352 or AUDIO, quoted
BINARY files, INDEX 00/01 and PREGAP/POSTGAP. Shared and separate BIN files
are supported. Unsupported formats, flags or indexes fail explicitly. All
stored sectors, including INDEX 00, remain byte-identical. Virtual audio gaps
become zero samples. Virtual MODE1 gaps contain zero user data, actual BCD
MSF headers, sync, EDC and P/Q parity encoded according to ECMA-130 section 14
and annex A. Track 1 INDEX 01 is LBA 0; its represented first pregap starts
at LBA -150. These synthesized sectors describe unstored gap data, not captured
physical lead-in or subchannel information. No subchannel or pre-emphasis
implementation is claimed.

Publication uses a temporary file on the output volume, flush/fsync and atomic
create-if-absent. Existing output requires explicit `--overwrite`; failed
conversion preserves it. Output cannot replace an input CUE/BIN path.

SGCDIMG1 uses explicit little-endian fields, never a dumped host C structure.
The file consists of a 96-byte header, one 32-byte entry per track, then raw
sectors from LBA -150 up to but excluding leadout.

| Header offset | Type | Value |
| --- | --- | --- |
| 0 | 8 bytes | `SGCDIMG` followed by zero |
| 8 | u32 | Version 1 |
| 12 | u32 | Header plus track-table size |
| 16 | u32 | Sector size 2352 |
| 20 | u32 | Track count 1–99 |
| 24 | i32 | First LBA, -150 |
| 28 | i32 | Leadout LBA, 1–449849 |
| 32 | u64 | Payload offset, identical to offset 12's value |
| 40 | u64 | Payload bytes, `(leadout + 150) * 2352` |
| 48 | 32 bytes | SHA-256 of the complete raw payload |
| 80 | 16 bytes | Reserved, zero |

Each track entry holds u32 track number, u32 kind (0 audio, 1 data), signed
32-bit INDEX 00, INDEX 01 and exclusive end LBAs, then 12 reserved zero bytes.
The first entry begins at INDEX 00 -150 and INDEX 01 0. Each following INDEX 00
equals the preceding exclusive end. INDEX 01 precedes the end; the last end
equals leadout. Leadout plus 150 must fit two-digit MSF, through 99:59:74.

`segacd_image_open` verifies format, exact file length, all bounds/reserved
fields, track sequence, TOC and whole-payload SHA-256. It owns its file handle
outside simulation state. Sector reads reject out-of-range LBAs and preserve
the caller's buffer on I/O failure. Mounted files must remain immutable; a
payload hash checked at open is not protection against later external writes.

Full-payload validation uses Windows CNG, or optional OpenSSL 1.1+ EVP on other
hosts when CMake finds `OpenSSL::Crypto`. The provider handles its own CPU
feature selection. `-DSEGACD_IMAGE_OPENSSL_SHA=OFF` disables that optional
dependency; absence of OpenSSL also selects the universal C implementation.
Provider initialization, update or finalization failure restarts a complete
portable pass, including resident-sector population. Every launch still hashes
all payload bytes. Linux distributions/packages enabling OpenSSL must carry
the linked crypto library and its license with their other runtime dependencies.
CTest covers native, forced-portable and unavailable-provider builds, plus
injected OpenSSL initialization, mid-stream and finalization failures.

The bounded data-track residency uses a read-only file mapping where available.
Every published mapped byte is compared with the validation stream and touched
during that same pass. Mapping failure or
`-DSEGACD_IMAGE_MAP_RESIDENT=OFF` retains the copied-memory implementation;
resident pointers keep the same lifetime and exact raw-sector contents.
Sequential nonresident reads retain the standard stream's read-ahead buffer
instead of seeking back to its current position for each sector. Device delivery
clocks, cancellation, raw-sector errors and the mounted-file immutability
contract are unchanged. Host statistics distinguish mapping size, private
resident bytes, raw reads and issued seek calls; these exclude validation I/O.

The pure sector validator checks MODE1 sync, mode and EDC using
[ECMA-130 sections 14.1–14.3](https://ecma-international.org/wp-content/uploads/ECMA-130_2nd_edition_june_1996.pdf).
It does not validate or repair ECC, compare header MSF with an LBA, or claim
support for Mode 2 data. The controller receives a real integrity result.
Bad EDC remains an error status; unsupported data modes and media-read failures
produce explicit diagnostics. Timed raw CD-DA sector events are a host mixing
seam; resampling, fader/de-emphasis and actual audio output remain pending.

The standalone synthetic CTest suite includes malformed headers, arithmetic
bounds, integrity failures, independent known SHA/EDC fixtures, track boundaries,
buffer preservation and converter publication failures. `segacd_disc_probe`
additionally reads every sector of a caller-supplied image and checks all
program-data MODE1 EDCs. It requires no game-specific filenames or adapters.

The canonical gap encoder was independently checked against libchdr's
BSD-3-Clause parity verifier at commit
`47d794151b2b64f6aba60304e6480f4f5a2bc24b`. All 155 selected LBAs pass full
parity and separate bitwise EDC checks; deliberate header/data/EDC/reserved/P/Q
mutations are rejected. That verifier remains a private QA dependency.
The regenerated owned 35-track image preserves every one of its 255,826 stored
sectors (601,702,752 bytes); only 150 implicit first-pregap sectors differ from
the earlier zero-placeholder image. Evidence is retained in
`build/qa-mode1-gap-proposal`, including the independent comparison manifest.
