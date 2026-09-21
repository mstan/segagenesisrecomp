# Sonic trilogy campaign experiment

Tracking: central Beads `beads-tdq.3.2`. Work solo. Engine and consumer branch:
`feature/sonic-trilogy-campaign`, in `_wt-sonic-trilogy-engine` and
`_wt-sonic-trilogy-game`. Previous foundation commits: engine `0d3c182`, consumer `efd682c`.
Remaining campaign work: central Beads `beads-3ixl`; draft publication: `beads-odyg`.

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
| GHZ1 | 165 | 439 | 896 | 132 | 155 |
| GHZ2 | 169 | 439 | 896 | 170 | 123 |
| GHZ3 | 165 | 439 | 896 | 212 | 123 |
| EHZ1 | 256 | 500 | 926 | 135 | 226 |

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

This is a first gameplay playtest, not the complete accepted campaign. Hidden
bonuses remain unfinished. Donor entrance rules lead to Sonic 3 Blue Spheres. Bridge sag, ledge fragments and some
object timings need more faithful ports. Full human routes/all-character clears
and end-to-end native campaign completion remain unvalidated. Netplay remains disabled.
Quickstates serialize the host state as described below.

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

## First playtest visual correction

The initial handoff contained obvious flower/waterfall corruption, wrong HUD
and ring colors, and misplaced backgrounds. Its launch/save tests passed, but
the screenshot review failed to identify these defects. Those automation results
were not evidence of graphical correctness.

The corrected importer reserves and fills the original GHZ stalk, flower and
waterfall slots and all five EHZ animation slots from the verified donor ROMs.
It also runs their water palette cycles and original background scroll formulas
(Sonic 1 REV00 for the supported donor). These are original assets, not S3-style
redraws. A required host scene at native width separates donor sprite palettes
from S3 player/common-object palettes while preserving the original terrain CRAM.
The same palette handling works in 16:9. The stock game remains unchanged when
the experiment is disabled.

Reference captures from the original S1/S2 runners are in the private
`build/trilogy-visual-references-v1` directory. Nine GHZ/EHZ route positions were
captured; starting scenes and several later positions were inspected alongside
those references. GHZ's static upper-right 180x75 region matches the donor
capture pixel for pixel. That is a bounded region check, not whole-game fidelity
validation. GHZ2/3 starts, boss, capsule and title were also visually inspected.
Native regression still matches all 16 PNG/RAM/VRAM captures. Checkpoint/reload,
boss/act-transition chain, GHZ/EHZ 4:3 and 16:9 checks pass. Asset tests compare
animation frames directly with donor bytes; renderer tests cover independent
sprite palettes and forced native width.

## Original donor music

`trilogy_music.c` reads seven cues per verified ROM: GHZ/EHZ, boss, act clear,
1-up, invincibility, game over and drowning. The S1 music is read directly;
S2 compressed music and its driver use a bounded Saxman decoder. A bounded
control-flow converter relocates branches/calls/loops, translates coordination
commands and tempo, preserves the original FM voices and PSG envelopes, and
copies original DPCM drums. Converted data lives only in private host memory.
The unmodified base ROM and the single campaign SRAM retain their hashes/layouts.

`trilogy_audio.c` gives the native S&K Z80 driver read-only virtual song/drum
banks and bank-independent donor envelopes. Native music selection installs the
donor tables before queuing a cue, and restores the original tables for native
stages, menus and special stages. S3 sound-effect envelopes are preserved. A
small Z80 compatibility handler preserves donor note-fill ticks without changing
S3 sound-effect semantics. The GHZ boss now requests its original boss cue.
This is a driver-format port, not a bit-identical recreation of either original
sound driver; speed-shoe behavior remains Sonic 3's.

Validation: all 14 converted cues pass the private-ROM decoder check and execute
in the real Z80 driver. `run_trilogy_music.py` captures full-rate audio, checks
non-silent/unclipped output, preserves all S3 SFX envelope pointers and verifies
1-up return. Final captures are in `build/trilogy-music-ghz-v3` and
`build/trilogy-music-ehz-v2`. Earlier GHZ/EHZ stage captures were compared with
original game captures over 25 seconds using phase-independent log-frequency
spectra (cosine similarity 0.935/0.925 at the matching offset). This numerical
comparison is not a claim of a human listening review or exact audio identity.
The transition test also checks restoration of music, envelope, drum and
note-fill tables against the native title baseline upon reaching AIZ.

## Shared special stages and cleared-slot browsing

Imported stages use donor entrance conditions with native Sonic 3 Blue Spheres:
GHZ1/2 use the original S1 goal-ring placements, visible with 50+ rings and
fewer than seven Chaos Emeralds. GHZ3 has no goal ring. Entering the ring runs
act results before Blue Spheres, then starts the next act on success or failure.
EHZ uses S2's four orbiting checkpoint stars, original art and expansion/expiry
timing. A fresh checkpoint requires 50+ rings and fewer than seven emeralds;
touching its stars enters Blue Spheres and returns to that checkpoint.
No starting giant rings or invented checkpoint giant rings remain. Native S3K
entrances remain unchanged. Seven Chaos Emeralds are shared across chapters.

The optional `SPCL` v2 SRAM extension has eight 20-byte entries: a slot token
and four 32-bit act entrance masks. Old v1 synthetic entrance masks migrate to
empty v2 masks without changing earned progress or emeralds. Unsupported versions
remain protected. Absent-chapter data is preserved, and slot tokens prevent
usage attaching to a deleted/recreated save. No Save keeps its masks in memory.
The native SRAM prefix and existing chapter codecs retain their formats.

Cleared saves browse available acts using Up/Down in native Data Select.
The existing cleared slot 8 is retained; subsequent updates preserve every slot.

## Quickstates and enemy correction

Shift+F1..F9 saves and F1..F9 loads. Controller LB/L1 saves slot 1; RB/R1 loads it.
Files are `native_save_1.bin` through `native_save_9.bin` beside the executable.
States are private to a compatible build and require the same enabled donor set.
They restore machine RAM/SRAM, audio chips, campaign/extensions, imported object
positions/timers/art allocation, donor music tables and sprite publication state.
ROM buffers and host pointers are rebound, never serialized. CRC and compatibility
checks run before machine mutation; writes use a temporary file and atomic replace.
The game resumes at the level, Blue Spheres or results loop rather than reloading
the mode. Save during gameplay or special-stage gameplay/results; menu and loading
screens are not supported save points. Loading also rewinds the save's progress.

Motobug now follows S1's fall-to-floor initialization and 60-tick edge pause/turn.
The previous constant movement and repeated direction reversal could leave it
below the surface and oscillating in place. Initialization now also follows
S1 in withholding rendering/collision until a floor is found: the original
C50/34C placement inside the cliff is invisible in Sonic 1, verified against
the donor runner. It should not be moved onto an invented surface.

`run_trilogy_special.py` checks goal/checkpoint eligibility, no start rings,
Blue Spheres quickloads, outcomes, next-act/checkpoint return, emerald/entrance
persistence and process-exit reload. Positions and last-sphere outcomes are
fixtures; the goal approach/jump and checkpoint jump use normal inputs.
`run_trilogy_states.py` checks object replay, cross-process/cross-stage restoration,
and rejection of damaged or missing-donor states before mutation. It captures
Motobug sequences for visual review. These are not full human playthroughs.

Validation artifacts: `build/trilogy-donor-rules-v4`, `build/trilogy-states-final`,
`build/trilogy-results-state-final`, `build/trilogy-moto-final` and original S1
comparison `build/trilogy-moto-original-v1`. All 21 CTests pass, and
`build/trilogy-donor-native-v1` matches 16 baseline PNG/RAM/VRAM captures.

## Original-act comparison checkpoint

The installed build was compared with an independent Sonic 1 runner entering
each act through its original level-select menu. The port used native S3 Data
Select. All three starting player/camera coordinates match. Original and port
screenshots were inspected together; the owner reviewed the GHZ2/3 comparisons.

The actual loaded foreground maps match the original game's RAM after normalizing
the chunk format: 61,440 GHZ1, 50,688 GHZ2 and 73,728 GHZ3 blocks, with zero
differences in block IDs, flips or primary solidity. The decompressed 16x16 art
mappings also match byte for byte. This verifies terrain and starting positions;
it does not establish complete object or boss behavior. Private captures and
`audit.json` are in `build/trilogy-act-layout-before`; audit tracking is `beads-l53c`.

## Remaining work for the full campaign

The draft checkpoint contains GHZ1-3, its boss/capsule and EHZ1. It is ready for
continued playtesting, not a completed Sonic 1/2 campaign. `beads-3ixl` tracks:

- EHZ2 and its boss, followed by the remaining Sonic 1/2 zones, objects, bosses
  and stage events, with their original layouts, art and music.
- Source-faithful bridge sag, collapsing ledges/fragments, hidden bonuses and
  remaining enemy/object timing in the current acts.
- A larger stage catalog and backward-compatible progress/entrance-mask codecs
  as more acts arrive; existing native and absent-donor progress must survive.
- Complete input-only routes for every supported character, plus all bosses,
  death/checkpoint/reload paths, chapter transitions and full campaign clear/replay.
- Shared emerald and quickstate coverage throughout the expanded campaign,
  wider object activation, and packaging validation beyond the tested Windows build.

Blue Spheres remains the agreed special-stage system. Original Sonic 1/2 special
stages and netplay are deferred. Quickstates currently require a compatible build
and donor set; extended SRAM compatibility with older executables/emulators is
limited as described above. Private ROMs, binaries, saves and comparison images
are not included in the source checkpoint.
