# Sonic trilogy campaign experiment

Tracking: central Beads `beads-tdq.3.2`. Work solo. Engine and consumer branch:
`feature/sonic-trilogy-campaign`, in `_wt-sonic-trilogy-engine` and
`_wt-sonic-trilogy-game`. Engine baseline `35620e2`, consumer `924fa3b`.

## Accepted direction

PC Sonic3KRecomp mod; independently optional Sonic 1 and Sonic 2 donor packs;
campaign order Sonic 1, Sonic 2, Sonic 3 & Knuckles. Sonic 3 physics/abilities
and standard Sonic, Sonic & Tails, Tails and Knuckles roster throughout.
First experiment: Green Hill 1–3 and its boss, Emerald Hill 1, then native
S3K. Blue Spheres, shared Chaos Emeralds, Super Emeralds beginning in S&K.
Eight slots plus No Save; completed slots unlock zone selection.

Owner steering during implementation supersedes the original separate-file
save design: **one existing S3K SRAM file, native prefix followed by optional
extensions**. Plain files and any combination of extension presence must work.
Missing ROMs must not erase their records. The owner confirmed that an
unavailable current chapter retains its checkpoint while play resumes in the
next available chapter. Restoring its donor returns it to the normal campaign
order. Unfinished enrolled chapters still count toward campaign completion.

## SRAM format and compatibility

The current runner persists a **16,384-byte raw-bus SRAM image**, including
unused byte lanes. This is the existing file representation, not a claim that
the original cartridge physically needs 16 KiB. Keep those bytes at offset zero
unchanged in format. Only that prefix enters cartridge SRAM.

An unextended file has exactly that size. An extended file appends:

| Offset in tail | Size | Meaning |
| --- | ---: | --- |
| 0 | 8 | ASCII `S3KEXT01` |
| 8 | 4 | container version, 1 |
| 12 | 4 | total tail size including header |
| 16 | 4 | number of records |
| 20 | 4 | CRC32 of everything after the header |
| 24 | variable | tagged records, no implicit padding |

Each record has a four-byte tag, a two-byte version, two reserved zero bytes,
four-byte payload length, four-byte payload CRC32, then its payload. Integers
are big endian. Maximum file size is 1 MiB and maximum record count is 256.
Duplicate tags and malformed lengths/checksums are rejected. Record order is
irrelevant: `CAMP` stores campaign metadata and the native chapter checkpoint;
`S1EX` and `S2EX` independently store the donor chapters. The container itself
does not interpret payloads. `trilogy_progress.c` interprets known versions.

`CAMP` version 1 is 384 bytes: the 256-byte campaign codec, eight big-endian
32-bit slot tokens, eight native chapter entries, and 32 reserved zero bytes.
Its sequence field is the next unused slot token. `S1EX` and `S2EX` version 1
are 64 bytes each, with eight chapter entries. Each entry is an eight-byte
`stage:u16, checkpoint:u8, cleared:u8, token:u32` tuple. The S1 cleared mask
tracks the three GHZ acts; S2 tracks EHZ1. These versions describe the first
experiment, not unimplemented stages. The native cleared flag is supplied by
the original game's save logic. Character, shared emeralds, enrolled packs,
current selection and overall completion live in `CAMP`.

Slot tokens detach deleted/reused slots from older chapter records, including
unknown versions retained on disk. An unsupported chapter version disables
only that chapter; unsupported campaign metadata protects all campaign edits.
Existing plain native saves can be attached without changing their progress.
ROM availability and campaign membership are separate: existing saves enroll
newly added chapters explicitly; installing a donor alone changes no progress.
No Save remains transient and must never call the eight-slot persistence API.

Persistence hooks remain installed when the mod is off. They preserve unknown
record tags and record versions byte-for-byte while updating native SRAM.
Unknown or damaged tails remain opaque and are preserved verbatim; only the
native prefix is usable in that case, and extension edits are refused.
Truncated native prefixes and unreadable/oversized files are write-protected.
Writes use a same-directory temporary file, flush it, preserve the previous
whole file as `.bak`, and atomically replace the primary. Saving a native prefix
with a damaged tail retains an existing backup. A `.lock` file serializes
updated runners' compare/backup/replace sequences; a stale writer cannot
overwrite a file changed since load. Related extension records are staged as
one transaction. Extension generation participates in the runner's dirty check,
so extension-only progress can trigger autosave.

The native `Write_SaveGame` hook synchronizes existing extension profiles with
native character, emerald and completion changes. Native deletion clears the
matching extension slot; the native new-slot hook detaches its old identity.
Boot-time native checksum repair is not treated as a player deleting a slot.
These hooks create no campaign records for ordinary unextended play. They
preserve imported chapter checkpoints during native play without donor ROMs.
Imported stage checkpoints/menu selection are not yet connected to gameplay.

Compatibility is with this updated recomp. Older executables currently reject
extended sizes and can overwrite files after rejection. An external emulator
must explicitly support the tail or receive an exported native prefix; this
implementation does not claim universal emulator compatibility.

## Source-backed terrain conversion

Use the revisions and hashes in `ghidra/annotations/provenance.json` and the
byte-matched private builds in the primary engine's `build/disassembly`.
The four acts decode through bounded Nemesis, Enigma and Kosinski readers.
S1's one-based 256px chunks are split/deduplicated into S3's 128px chunks;
flip/solidity bits are converted. Alternate loop chunks are retained in a
parallel collision layout. S2's fixed FG/BG rows become S3 row pointers.
Both collision indexes are expanded to S3's byte-at-word-stride representation.
Placements and ring groups are decoded separately; placements are not yet
instantiated as working objects.

Private-ROM decode results:

| Stage | Chunks | Blocks | Tiles | Non-ring placements | Rings |
| --- | ---: | ---: | ---: | ---: | ---: |
| GHZ1 | 165 | 439 | 830 | 132 | 155 |
| GHZ2 | 169 | 439 | 830 | 170 | 123 |
| GHZ3 | 165 | 439 | 830 | 212 | 123 |
| EHZ1 | 256 | 500 | 914 | 135 | 226 |

## Current integration boundary

This is **not yet the accepted playable campaign**. A developer-only terrain
harness is selected with `SONIC_TRILOGY_STAGE` (`1000`, `1001`, `1002`, `2000`)
and `SONIC_TRILOGY_ROM` (the appropriate verified donor). It enters through
native Data Select, keeps native S3 player logic, installs converted resources,
and substitutes stage drawing/collision data at verified hooks. Base and donor
ROM hashes are checked. Native saving is disabled in the terrain harness.
Machine snapshots and netplay are guarded while the harness is selected.

Still required: objects/platforms/hazards and loop path switching, checkpoint
restore, rings, stage art animation and music, GHZ boss/capsule, title cards,
pack picker and complete campaign/SRAM-slot integration, Blue Spheres return,
all-character traversal, all transitions, and widescreen integration. Do not
describe terrain screenshots or the generic native renderer harness's PASS as
validation of these missing behaviors.

## Validation so far

`trilogy_campaign_test`: stage graph/pack combinations, roster/emerald bounds,
replay gating, fixed-endian codec and transactional malformed-save rejection.
`trilogy_sram_test`: plain prefix, all pack permutations, unknown records,
replacement sizes, extension generation, every tail-byte corruption,
preservation during native writes, atomic backup path, stale writer rejection.
`trilogy_assets_test`: malformed compressed input and optional private donor
decodes. Raw outputs remain private build artifacts.

`trilogy_progress_test`: all 16 enrolled/available pack combinations, eight
slots, retained checkpoints, native-only completion with unfinished imports,
explicit enrollment, replay gating, slot reuse, native emerald/zone decoding,
and independent future-version preservation. All four trilogy unit targets pass.

Combined native executable builds with MSVC. Initial mod-off input run passed
16 checkpoints without dispatch misses. GHZ1 terrain has visible converted
scenery and native S3 movement; its route stops at missing platform objects.
After adding terrain hooks, the mod-off run matched the original 16 screenshots,
RAM and VRAM checkpoints byte-for-byte. `run_trilogy_sram.py` additionally ran
all four appended chapter combinations through the executable with no donors,
forced normal native SRAM writes, verified the entire appended tail survived,
and compared the resulting native save and all 16 gameplay checkpoints across
the four cases. Every case passed with no dispatch misses.
A fifth executable case damaged both native campaign copies and verified that
the game's boot-time checksum repair preserved all appended chapter progress.
