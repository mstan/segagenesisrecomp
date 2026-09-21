# Sonic trilogy campaign experiment

Tracking: central Beads `beads-tdq.3.2`. Work solo. Engine and consumer branch:
`feature/sonic-trilogy-campaign`, in `_wt-sonic-trilogy-engine` and
`_wt-sonic-trilogy-game`. Previous foundation commits: engine `0d3c182`, consumer `efd682c`.

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
Native Data Select now launches donor stages, retains their independent checkpoints,
and updates lives/emeralds in the native slot without advancing the native chapter.

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
Placements and ring groups are decoded separately. Common objects use native S3
routines; donor objects use native object slots, collision response and player abilities.
S1/S2 sprite maps are converted to S3 maps and art is allocated while objects are live.

Private-ROM decode results:

| Stage | Chunks | Blocks | Tiles | Non-ring placements | Rings |
| --- | ---: | ---: | ---: | ---: | ---: |
| GHZ1 | 165 | 439 | 830 | 132 | 155 |
| GHZ2 | 169 | 439 | 830 | 170 | 123 |
| GHZ3 | 165 | 439 | 830 | 212 | 123 |
| EHZ1 | 256 | 500 | 914 | 135 | 226 |

## Gameplay playtest boundary

GHZ1-3 and EHZ1 are available through native Data Select, including eight saved
slots and No Save. Settings use `[trilogy]`, `enabled=1`, `sonic1_rom=...` and
`sonic2_rom=...`. Independently verified donors decode once at startup.
`SONIC_TRILOGY_S1_ROM` / `SONIC_TRILOGY_S2_ROM` provide local automation overrides.
`SONIC_TRILOGY_STAGE` plus `SONIC_TRILOGY_ROM` remains a developer-only No Save
stage fixture; ordinary play does not need it.

Implemented: native rings/monitors/springs/spikes/starposts, platforms, bridges,
swinging platforms, ledges, breakable walls, rotating spike poles, badniks and
projectiles, GHZ loops/forced roll, EHZ layer triggers/corkscrew, GHZ boss/ball/
capsule, native signposts/results, chapter transitions, donor previews and
native S3 title lettering. The stock S3K chapter resumes after EHZ1. Attract
mode retains native behavior. Save loading retains unavailable chapter progress.
Existing slots explicitly add chapters using the native menu's advertised B action.

This is a first gameplay playtest, not the complete accepted campaign. Original
stage music, donor tile/palette animations, hidden bonuses and giant-ring/Blue
Spheres entry/return are still unfinished. Bridge sag, ledge fragments and some
object timings need more faithful ports. Full human routes/all-character clears
and end-to-end native campaign completion remain unvalidated. Machine snapshots
and netplay are disabled while this experiment is enabled.

The consumer's `PLAYTEST.md` describes the isolated launch folder and eight
starter slots. `prepare_trilogy_playtest.py` creates native slots via real menu
input; `trilogy_playtest_seed` advances two fresh fixture slots using the same
campaign API. Every preset is then reopened through Data Select and checked.
No existing user save is used to prepare those fixtures.

## Validation

Four trilogy unit targets pass: campaign graph/roster/replay/codec, SRAM
permutations/corruption/unknown data/stale writers, progress/token/checkpoint/
missing-donor rules, and bounded asset decoders. The verified private donors
decode all four acts. S3K and standalone S3 renderer unit tests also pass,
including host-supplied sprite-map reads.

Private executable checks:

- `run_trilogy_campaign.py`: new/reloaded campaigns, each missing-donor case,
  restored donors, and explicit enrollment of an existing native save.
- `run_trilogy_characters.py`: Sonic & Tails, Sonic, Tails and Knuckles launch
  in each donor chapter; No Save does not create campaign records.
- `run_trilogy_checkpoint.py`: an actual donor post touch, death, retry, quit
  and native-menu reload preserve checkpoint and lives (positions are fixtures).
- `run_trilogy_transitions.py`: GHZ1 -> GHZ2 -> GHZ3 -> EHZ1 -> native AIZ1;
  native results, eight normal-collision boss hits, capsule and saved clears.
  Position/velocity fixtures are explicit; no boss HP/progress flags are injected.
- `run_trilogy_route.py`: GHZ1 completed with controller inputs only and zero
  deaths. Other automated route attempts remain incomplete; they do not prove
  a stage is blocked, nor do they count as successful full playthroughs.
- Native renderer regression: 16 screenshots, RAM and VRAM captures still match
  the original mod-off reference byte for byte. Native save writes retain all
  four chapter-record permutations; native checksum repair preserves the tail.
- Imported GHZ1 and EHZ1 renderer checks pass at 4:3 and 16:9 with zero dispatch
  misses. Wider imported object activation has not been validated.

Runtime artifacts remain under the private `build/trilogy-*` directories.
A renderer PASS reports rendering/execution assertions, never a stage clear.
