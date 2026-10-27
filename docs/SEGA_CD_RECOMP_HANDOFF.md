# Original Sonic CD / Sega CD recompilation handoff

Current status (2026-10-21): development is active under the owner's resumed
goal. Use [SONIC_CD_NATIVE_ACCEPTANCE.md](SONIC_CD_NATIVE_ACCEPTANCE.md) for the
current pass/unverified ledger. The paused/headless-only state below is an
archived 2026-10-20 checkpoint and does not describe the current runner.

Updated 2026-10-20. This is the current continuation handoff. It supersedes older
sequencing and incomplete-status text in the campaign handoffs. Implementation
HEAD when this document was written: `17f74420376c2d384ef4345e851a0cb3bc533749`.

## Owner direction and paused state

The owner explicitly stopped development to check in. The existing goal is
**paused**; this documentation request does not resume implementation. All
known builds, probes and subagents finished before this handoff. There is no
healthy background build to restart or terminate.

When the owner explicitly resumes, finish **original Sonic CD running as a native
recompilation using a reusable upstream Sega CD backend**. Prioritize getting a
usable owner playtest, then complete the required validation and commit the work.
An independent Sonic CD repository is authorized if it materially helps the
consumer/build workflow. Do not substitute the completed C campaign adaptation
for original-program recompilation.

Use original Main 68000, Sub 68000 and Z80 programs. Diagnostic interpreters may
help development and comparisons; accepted native execution must not silently
fall back to them. Keep instruction-byte guards and genuine device/bus behavior.
Missing code/device support must stop visibly, not manufacture successful reads,
music, transitions or completion. Keep a short, explicit list of noncritical
hardware-accuracy qualifications; it does not excuse missing required gameplay,
sound, saves or native execution.

The owner explicitly authorized **Luna subagents for bounded grunt work**, with
adversarial parent review. This supersedes older campaign text saying to work
solo for this task. Do not duplicate workers or busy-poll them.

Do not merge CD into main/master or push source branches without the appropriate
owner instruction. Local checkpoint commits are authorized. Shadow already
merged separately and should not be reimplemented.

## Start here

Open **`F:/Projects/segagenesisrecomp/_wt-segacd-general-engine`**, branch
**`feature/segacd-backend`**. Read this file, then
[SEGA_CD_BACKEND.md](SEGA_CD_BACKEND.md) and
[SEGA_CD_REUSE.md](SEGA_CD_REUSE.md). Their chronological older milestones retain
their original limits; use the latest results below for current status.

| Work | Location / branch | Commit |
|---|---|---|
| General Sega CD backend | `_wt-segacd-general-engine`, `feature/segacd-backend` | `17f74420376c2d384ef4345e851a0cb3bc533749` |
| Previous audio/fader checkpoint | same branch | `42aa4a2d768a98353e26059c9436671ba4a67332` |
| Drive timing/input/native registry checkpoint | same branch | `bbb18445f8f492f49656352d3109df76eddfeca5` |
| Pinned M68k core | `_wt-segacd-m68k-core`, `feature/segacd-flat-step` | `7233852a6e45d8779b9dae8b29afd6b94d5231dc` |
| Isolated ordered Z80 bus-read fix | `_wt-segacd-z80-core`, `feature/segacd-ordered-z80-bus` | `4dfc5fc29fd67d23714126348e678e45952e9462` |
| Engine's current Z80 gitlink | `external/z80-recomp-core`; fix above is **not integrated** | `0e6606a41245d54c5fd0c3dc322ef9d6890d3923` |
| Completed adapted CD campaign | `_wt-sonic-cd`, `feature/sonic-cd-stage-pack` | `1efc1631294d5d03aaf12f73c75deea7a715146a` |
| Campaign engine | `_wt-sonic-cd-engine`, `feature/sonic-cd-stage-support` | `332ff1fbdb4073cddffd63305a34da4935c262c4` |

Paths in the table are beneath `F:/Projects/segagenesisrecomp`.

The completed campaign implementation checkpoint is `731461a100047bda4e5484cbefea3bd5ad30fbea`.
Its mod/save/70-variant mechanics/audio and visual evidence is under the campaign
worktree. It uses S3K-hosted adapted C code and does not qualify original CD
programs. Its owner build remains at
`_wt-sonic-cd/build/cd-owner-playtest/Sonic3KRecomp.exe`; do not present it as the
new native CD backend. Campaign order remains S1 -> S2 -> CD -> S3K -> Megamix.

Shadow and the independent Mighty/Shadow/Megamix stage controls already merged
to game main `4e387ee360c2da86d56581c06ab3d00016ce3f69` and engine master
`3f17adf000ead84754c189ffbea01b951c081c39`. All three require the owned Megamix
ROM and share its path. Preserve this behavior; it is completed work.

## Latest original-program status

Original BIOS/disc execution reaches Palmtree Panic with real Start/Right/A
inputs. Captures show movement, jumping, scrolling, loop traversal and 13 rings.
No success flags were injected. There is **no owner-facing native CD runner yet**;
the current consumer is a headless diagnostic.

The latest expanded native run completed 50 emulated seconds without a CPU or
device fault:

- `build/qa-bios-native-observed-cdda-movement-1/run.log`
- 625000002 Sub clocks; 79926201 coordinator steps.
- Native operations: Main 32210773 + Sub 47351677 = **79562450**.
- Diagnostic-floor operations: Main 38519 + Sub 312257 = **350776**.
- Approximately **99.56% native operations in this one test**, not project
  completion or whole-game coverage.
- 2151 media sectors, including 1649 CD audio sectors; CDDA error/status/underflow
  are all zero. 2204999 CDDA samples were captured, including inactive silence.
- Final Main PC `2019AE`, Sub PC `00C02A`.

**Pending:** the latest expanded native run's complete artifact/audio comparison
against the CDDA-integrated floor run has not been performed. Matching printed
final counters is not a substitute for that comparison.

The matching strict native-only run stops early on uncovered Sub PC **`019098`**,
called from `0189AC` (`BSR`), at Sub clock 25862478:

- `build/qa-bios-native-required-disc-gameplay-1/run.log`
- 2486163 native operations, zero floor operations, diagnostic exit 3.
- No CPU fault or media error; this is an AOT coverage miss.
- The current extension catalog covers `[018000,019000)` and misses this next
  page. Source-match the actual bytes beyond that boundary before extending it.

The latest high-limit hybrid trace lists **62 distinct remaining Main PCs** and
**146 distinct remaining Sub PCs** across its capture epochs:

| CPU | Remaining address families | Distinct PCs |
|---|---|---:|
| Main | `FF2000..FF208A` | 26 |
| Main | `FF3BB2..FF3C58` | 35 |
| Main | `FFFD06` | 1 |
| Sub | `005F2E..005F82` | 4 |
| Sub | `019098..019246` | 49 |
| Sub | `040010..040FD2` | 93 |

These are observed PC/opcode sets, not proven source identities. In particular,
the same RAM address can host several programs or rewritten dispatch stubs.
Identify the resident bytes/source per epoch and retain current-byte guards.
Batch these families into the next native build rather than linking once per
small stub.

## What is implemented and verified

The portable `runner/cd` device/backend foundation includes dual 68000 scheduling,
guarded instruction-sized AOT, diagnostic execution floor, synchronous ownership
WAIT, BIOS/PRG/Word/backup RAM maps, interrupts/timers/communications, CDC sector
delivery and DMA, CDD commands, RF5C164 PCM, stamp graphics and Word RAM-to-VDP DMA.
The running BIOS loads the programs; the probe does not preload guest code or
force the PC to simulate a boot. See the backend document for precise contracts
and existing unqualified edge cases.

Recent additions in `17f7442`:

- `runner/cd/cdda.c/.h`: a two-sector, explicit-state CD audio stream, LE stereo
  decoding, rational 12.5 MHz / 44.1 kHz timing, transport flushing and actual
  post-fader sample callbacks. Host mixing/resampling is still unfinished.
- `runner/cd/fader.c/.h`: primary-source encoded LC7883KM gain, shared stereo
  coefficient, 1024 field steps from unity to mute. EFDT transfer status remains
  separate in CDD. Do not replace it with an emulator's raw-16-bit ramp.
- Mapped fader writes and actual drive commands feed the CDDA device. The last
  audio sector is consumed completely before data-track delivery clears it.
- `recompiler/src/main_step.c --entries-file PATH`: validated whitespace-separated
  roots, combined with positional roots/files and deduplicated before CFG walk.
  This avoids Windows command-line limits and overlapping duplicate catalogs.
- Diagnostic audio/PC capture and snapshot controls in
  `tests/segacd/bios_dual_probe.c`.

All **20 synthetic CTest cases pass** on MSVC Release, MinGW and Linux
ASan/UBSan. Logs: `build/cdda-integration-{msvc,mingw,linux}-test.log`.
The source tests include actual mapped packet/fader-to-DAC integration,
pause/resume/stop, final audio-to-data boundary, reset/errors, rational clock
phases, source continuity, and generator entry-file validation/equivalence.

The 50-second **CDDA-integrated floor** run is independently audited:

- `build/qa-bios-cdda-integrated-floor-movement-1/`
- `independent-review.json` and `review_cdda_integrated.py`.
- All 46 baseline RAM/VDP/frame artifacts are byte-identical to
  `build/qa-bios-scripted-floor-movement-1`.
- Raw RF PCM, raw CDDA and its sector index are byte-identical to the earlier
  observation run.
- Every one of 2204999 post-fader PCM frames matches its CSV record.
- All 968875 source-anchored frames match actual sector samples and the
  **preceding** encoded fader gain; zero source/gain mismatches.
- Frame clocks, source capacity and sector-arrival causality pass.
- `parent-reset-phase-review.json` explains the 462-tick inactive gap from the
  actual Sub reset boundary; startup phase anchoring explains the other gap.
  These validate the implemented profile, not measured hardware DAC phase.

An earlier native run (`build/qa-bios-native-scripted-movement-1`) had all 46
artifacts identical to its floor counterpart with 70193539 native / 9719687
floor operations. Do not confuse that passed comparison with the newer pending
99.56% native comparison.

## Z80: prepared, not integrated

The current engine Z80 adapter still maps banked Main accesses through the plain
Genesis bus, bypassing Sega CD ownership. Its dispatcher can call SuperZazu on a
miss. This does not satisfy native Sonic CD acceptance. Z80 execution must move
outside coordinator `host_advance` callbacks to prevent nested CPU entry.

Read these private reports before implementing:

1. `build/qa-segacd-z80-adapter/design.md`.
2. `build/qa-segacd-z80-program-identity/report.md`.
3. `build/qa-segacd-z80-program-identity/catalog/flatstep/snapshot-catalog.md`.
4. `build/qa-segacd-external-byte-wait/README.md`.
5. `build/qa-segacd-z80-generated-wait/README.md` and `build-run.ps1`.

Twelve actual 8 KiB Z80 RAM snapshots form three images, differing only at
`1C09`. A private generated module exists at
`build/qa-segacd-z80-program-identity/catalog/flatstep/generated/sonic_cd_z80_step.c`.
It has 8192 guarded address cases; this is static preparation, **not real-game
Z80 execution or validated sound**. The real module has not been integrated.

The working generator is
`build/qa-segacd-z80-program-identity/bin/SmsRecomp-flat.exe`, compiled from
`F:/Projects/smsggrecomp/smsggrecomp` source at
`224d5bb2c150a2c295033d35dec629ef9ee42940`. The old shared `SmsRecomp.exe`
rejects `--flat-step`; do not accidentally use it. Generated image CRC constants
are metadata, not whole-upload runtime identity selectors. Retain per-instruction
guards and make accepted native misses loud.

The private external-byte prototype retries only a denied byte while running the
real Sub owner. Parent review corrected skipped Sub time, missing reset refresh,
reentry guards, address normalization and fault/zero-budget handling.
An **actual generated Z80 LDI** proof then showed source read once, destination
commit once after four Sub retirements, and architectural state matching the
immediate-grant case. The owner-fault case stops without a destination write.
This API is still private, not production. Z80 clock conversion, WAIT charges,
interrupt/raster/audio synchronization and actual sound-driver behavior remain
unqualified. On a fault the generated instruction may already have advanced
PC/R/cycles; halt rather than replaying the whole instruction.

The isolated Z80 core fix sequences `sms_read16` low byte then high byte with
16-bit wrap. Core CTest passes 2/2 on GCC and MSVC, including strict NDEBUG builds.
The engine still pins the predecessor. Its `external/z80-recomp-core` is a
junction to the main engine checkout; **do not mutate that shared checkout** to
integrate the fix. Inspect/isolate the worktree and gitlink properly. Native
Windows Git now reads the isolated core's canonical `F:/...` worktree pointers.

## Owned inputs and private artifacts

The owner has supplied both inputs; do not ask for them again.

- Disc archive: `E:/Downloads/Sonic CD (US).7z`.
- BIOS archive: `E:/Downloads/Sega Mega CD BIOS.zip`.
- Qualified BIOS: US Model 1 v1.10, 131072 bytes, CRC32 `C6D10268`, SHA256
  `807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.
- Extracted BIOS:
  `F:/Projects/segagenesisrecomp/_wt-sonic-cd/build/private-inputs/segacd-bios-us-1.10.bin`.
- Qualified CUE:
  `F:/Projects/segagenesisrecomp/_wt-sonic-cd/build/private-inputs/sonic-cd-us/Sonic CD (US)/Sonic CD (US).cue`.
- Fast derived image:
  `E:/Downloads/segacd-private-qa-cache/sonic-cd-us-mode1-gaps-4e822010.sgcd`.
  File size 602056768 bytes; **payload** SHA256
  `4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1`.
  The original F: image remains under `build/private-inputs`. Preserve the
  verified MODE1 virtual-pregap conversion; do not revert to zero/invalid headers.
- Valid 8192-byte BIOS-formatted backup input:
  `build/qa-bios-brmformat-contract/guest-format/formatted-internal-bram.bin`, SHA256
  `4390aad972163d9bbc8a8cd7d32e86b216e5a17a0d795c7d230942155e6a895b`.
  It was produced by real guest BIOS BRMINIT/BRMFORMAT/BRMINIT calls and verified
  from all 70 BIOS writes. Normal BIOS-menu formatting is still unverified.
  Erased RAM correctly shows the original formatting warning.

All `build/` references below are relative to the general-engine worktree unless
explicitly stated. Owned inputs, generated game/BIOS C, captures and saves stay
private; do not commit them.

Native build/cache:

- `build/segacd-tests-msvc-native-current/Release/segacd_bios_dual_probe.exe`.
- Safe copied executable from the last completed build:
  `E:/Downloads/segacd-private-qa-cache/native-observed-cdda-1/probe.exe`, SHA256
  `406ac66e2176b73baf10e1811ad15b30c652635efa58add5b877b61bccf6c42c`.
- Extra-module registry cache: `build/bios-native-current/overlays/modules.cmake`.
- Base catalogs: `build/bios-native-current/{bios,sub,main-ram,sub-patch,boot-main,boot-sub}`.
- Extended source/capture proposals and parent checks:
  `build/qa-bios-native-source-catalog-proposals-v3/parent-adversarial-review.json`.
- One 7795-entry R11A catalog with 4917 roots:
  `build/qa-m68k-entry-file/r11a-stage-main-entry-file/`.
- Transient runtime page and dispatch catalogs:
  `build/qa-bios-transient-main-ff1000-1/`.

The latest native executable includes these reviewed proposals. The root-file
catalog avoids the earlier seven overlapping R11A catalogs; do not register all
seven again. Early RAM at `FF0000` and later IPX source bytes differ substantially;
their guarded catalogs intentionally apply to different resident states.

## Lean QA plan accepted at the check-in

The owner asked to use teleports/cheats and subsampling rather than a large
cross-product. Adopt this for the original backend:

1. Derive the content inventory from actual disc tables. The established campaign
   inventory has 70 standard stage/time variants. The owner also referred to 79;
   account for special stages and other sequences explicitly instead of silently
   assuming the total. Do not restart discovery already documented in the
   campaign source audit.
2. Give every listed variant a brief load, representative movement/terrain and
   rendering pass, plus a retained independent visual comparison. Use stage/time
   selection, teleports and protection cheats rather than normal full routes.
3. Subsample deeper checks for genuinely shared mechanics, collision/scrolling,
   enemy interactions, audio and checkpoint/death/retry behavior across zones and
   time periods. Explicitly test each distinct boss, gimmick, time-travel path and
   special-stage system; sample shared code only when the sharing is supported.
4. Teleport near exits/encounters, then exercise actual triggers, boss defeat and
   next-stage transitions. Never set the success flag a check claims to validate.
   Record any prerequisite bypass separately.
5. Test saves and restart/resume at representative progression boundaries,
   including persistence across process restarts and meaningful completion
   state. Do not multiply every save scenario by every stage/time and mod mask.
6. Keep passed, failed, sampled and unverified checks explicit. A screenshot or
   loaded handler does not establish mechanics. Mandatory final visual
   comparisons remain required.

The adapted campaign's 16 stage-mod / 64 character-control configuration checks
and removal/reinstallation tests are completed separate evidence. Do not repeat
that matrix for every original CD stage. Run focused regressions if subsequent
shared integration changes those contracts.

Existing independent references are available under `build/qa-bios-reference-*`,
including the matched controller script in `build/qa-bios-reference-floor-script`.
Reference and native global-frame numbers can differ with loading profiles;
align meaningful in-game state/input for final comparisons and disclose remaining
differences. Preserve the working Genesis renderer instead of changing its DAC
palette simply to force another emulator's RGB output to match.

## Immediate burndown when resumed

1. Compare the newest native run's RAM/VDP/frame/audio artifacts with
   `qa-bios-cdda-integrated-floor-movement-1`, and preserve the manifest.
2. Source-match and batch the remaining Main/Sub coverage families above. Extend
   the BIOS extension beyond `019000`, identify the Sub `040xxx` resident program
   and remaining Main overlays/stubs, then require native-only boot/gameplay.
   Continue genuine boot/loading; do not preinject code or skip PCs.
3. Integrate generated Z80 execution and its Sega CD banked-bus WAIT path, the
   ordered core fix and deterministic clock/interrupt scheduling. Enforce native
   misses for accepted execution. Preserve earlier instruction bus effects across
   WAIT and halt explicitly on faults.
4. Connect RF PCM/CDDA/YM/PSG to the existing emulation-thread audio mixer with
   proper timestamp/rate conversion and transport/reset behavior. The existing
   `AudioExtraSource` receives the internal PSG mix rate (~223.7 kHz), **not**
   44.1 kHz. SDL callback timing must not advance emulated devices.
5. Build the owner-facing Sonic CD consumer/launcher with owned BIOS/disc input,
   real controls and backup persistence. An independent repository may help.
   Validate a meaningful playable path, audio, retry, transitions and save round
   trip before the first owner handoff; then complete the lean full-game QA.
6. Perform mandatory final independent comparisons, resolve failures, document
   remaining noncritical accuracy limits, commit and report a concrete test build.

Current known limits include unqualified physical drive/startup/DAC phase timing,
unimplemented CDDA emphasis/double speed (explicit faults), odd-byte CDC DMA
requests (explicit unsupported), and remaining CPU exception/timing edge cases
documented in the backend ledger. Original US Sonic CD's observed fader requests
have emphasis/speed flags off. Do not quietly call unsupported paths implemented
or let noncritical accuracy research obscure the standup's actual blockers.

## Reproduction and operating constraints

The successful gameplay probes use:

```text
BIOS, deadline 625000000, fresh existing dump directory, qualified IMAGE.sgcd,
CLI Start frame 600
SEGACD_PROBE_BRAM_IMAGE=<the qualified BIOS-formatted file above>
SEGACD_PROBE_PAD_EVENTS=1500:10:0x80,1650:10:0x80,1800:10:0x80,2350:550:0x08,2450:10:0x40
SEGACD_PROBE_SNAPSHOT_FRAMES=1200,1500,1650,1800,2100,2200,2300,2400,2500,2600,2700,2900,3000
SEGACD_PROBE_CAPTURE_AUDIO=1
SEGACD_PROBE_FLOOR_PC_LIMIT=4096
```

Use `SEGACD_PROBE_REQUIRE_NATIVE=1` for strict coverage, or
`SEGACD_PROBE_FLOOR_ONLY=1` for the diagnostic baseline, never both. Optional
boolean capture variables accept `1` or absence, not `0`. Frame 3000 is beyond
this 50-second run; do not claim it was captured. `SEGACD_PROBE_CAPTURE_MAIN_PC`
provides a one-shot live Main retirement capture for transient code.

Build the synthetic suite with target `segacd_test_suite`; it excludes expensive
owned-input probe targets. Native extra catalogs use the existing `-C
build/bios-native-current/overlays/modules.cmake` cache and target
`segacd_bios_dual_probe`. Extra module directories intentionally do not become
target-wide include paths; preserve that behavior to avoid recompiling all
existing catalogs on every addition.

F: metadata/link I/O can be very slow. Native links have taken many minutes while
healthy. Batch changes, yield/resume the same tool handle and inspect completion;
do not kill/relaunch a healthy build merely because it is quiet. Copy a completed
EXE to a fresh E: cache directory before long probes; do not execute the build
target while relinking it. Avoid putting another 600 MB image copy on C:.

Use PowerShell with `login:false` and absolute Windows executables:

- Git: `C:/Program Files/Git/cmd/git.exe`.
- CMake/CTest: `C:/Program Files/CMake/bin/{cmake,ctest}.exe`.
- Python: `C:/Users/Matthew/AppData/Local/Programs/Python/Python312/python.exe`.
- GCC: `C:/msys64/mingw64/bin/gcc.exe`.
- Linux checks: `C:/Windows/System32/wsl.exe -d Ubuntu -- bash ...`.

For engine status/diff, use `-c core.fsmonitor=false -c core.fscache=false -c
core.preloadIndex=false -c diff.ignoreSubmodules=all` and
`--ignore-submodules=all`; the unrelated existing `external/rbengine` gitfile is
broken. Do not repair unrelated shared worktrees. PowerShell `*>` logs are often
UTF-16LE; decode the BOM rather than assuming UTF-8. Never mix shells for Windows
file mutation or ShellExecute a quoted path by itself.

Keep automatic/background launches muted. Owner-facing playtests need normal
audio and a normal window. Gameplay/launcher captions must be clean game names
(`Sonic CD`), including when a debug port is present. Do not ship CD-speed or
host-loading shortcuts.

Only ares's ISC-licensed selected device code is reused in production; retain
`runner/cd/ARES-LICENSE.txt`. Pinned ares:
`9408cb43d4948fc3ea6e152a307a34348df3fe04`. Genesis Plus GX reference
`49c584764893b0505ac7f768a754f97330fa4392` is **noncommercial**, and BlastEm is
GPL; they are private QA references, not permissive production sources. For fader
details use `build/qa-segacd-cdda-audit/primary-fader-audit.md`, not superseded OCR
notes. Primary Sanyo EN3884B p9 establishes unity `4000`, 1024 field steps to mute
and the gain plateaus; raw de-emphasis `04` is 44.1 kHz, `08` is 32 kHz.

Track this work in central Beads **`beads-ct0o`**, under `beads-3vb`, at
`F:/Software/beads/issues`. The campaign issue is `beads-ritf`; do not force-close
it through its backend dependency. Record the new audio commit, latest native
results, paused state and handoff. Central sync must use
`C:/Users/Matthew/.local/bin/Sync-CentralBeads.ps1`; a previous attempt found
another helper running. Respect that lock and do not kill shared database/sync
processes. Source pushes/merges remain separate from central issue sync.
