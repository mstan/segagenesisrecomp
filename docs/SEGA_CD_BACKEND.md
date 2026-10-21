# Sega CD backend

Updated 2026-10-21. The reusable Sega CD / Mega-CD backend lives on
`feature/segacd-backend`; it has not been merged into engine master.
[SonicCDRecomp 0.0.1](https://github.com/mstan/SonicCDRecomp/releases/tag/v0.0.1)
is its released original-program consumer, available for Windows and Linux x64.
Users supply the qualified US disc and US Model 1 v1.10 BIOS.

Start with [Set up a Sega CD recomp](SEGA_CD_SETUP.md). The game project's
[build guide](https://github.com/mstan/SonicCDRecomp/blob/main/docs/BUILDING.md)
automates the reference build; [SONIC_CD_NATIVE_BUILD.md](SONIC_CD_NATIVE_BUILD.md)
documents the underlying frontends, generation and source-bundle workflow.

## Current status

| Area | Qualification |
|---|---|
| Main/Sub 68000 and Z80 | Original instructions execute as guarded generated native code. Misses stop visibly; no diagnostic interpreter is attached to the shipping runner. |
| Sonic CD content | 70/70 standard stage/time variants, 7/7 boss completions, 8/8 special stages, secret credits/exit and original bad-ending film/full credits/title return. Scope and unverified cases are in the [acceptance ledger](SONIC_CD_NATIVE_ACCEPTANCE.md). |
| Saves and transitions | Original clear/results, time travel, death/retry and durable backup-RAM restart/Continue boundaries have targeted qualification. |
| Presentation | Normal window, normal owner audio, recomp-ui and adaptive normal-stage widescreen. Special stages retain their original centered projection. |
| Source generation | Owned-input recipe and verified resume produce 333 catalogs / 11,290 C/header files. Required source-byte and cycle guards are preserved. |
| Released packages | Windows ZIP and Linux x86_64 AppImage; Linux requires glibc 2.39+. Both public downloads were verified. The game's [platform ledger](https://github.com/mstan/SonicCDRecomp/blob/main/docs/VALIDATION.md) distinguishes complete Windows coverage from focused Linux checks. |

The owner accepted the windowed Windows playtest with normal audio before
release. The Linux package passed a 2,791-frame native controls, scenery and
stereo-audio check with floors `0/0` and zero audio faults. Its final RGB frame
matches Windows, and two actual AppImage launches preserve settings and BRAM.
Physical Linux audio/controller devices and other distributions remain
unverified. Independent final original-game visual comparisons are recorded
in the acceptance ledger; cross-platform equality does not establish exact
original-game animation or analog color phase.

This is the original CD program and its runtime, separate from the S3K-hosted
CD campaign adaptation. Campaign and Shadow work remain preserved. Other CD
games, BIOS revisions and regions need their own catalogs and validation.

## Native execution and coordination

`runner/cd/machine.*` composes explicit CPU contexts, buses, video, devices,
audio, backup RAM and optional presentation. The Main/Sub 68000 contexts
consume guarded catalogs through the shared M68K flat-step ABI.
`z80_native.*` supplies generated Z80 execution. The native target does not
link the legacy interpreter adapter or attach the Main/Sub diagnostic floor.

`segacd_coordinator.*` coordinates CPU retirement and device boundaries on
the shared clock. Z80 banked accesses use the CD Main map, and ordered bus
operations retain their actual access boundaries. An ownership WAIT runs the
actual owner and retries only the denied access, preserving earlier effects.
Faults are terminal rather than replaying partial instructions.

Original instruction bytes and nominal cycles remain the catalog guard
contract, including loaded overlays and historical self-modified variants.
An explicit diagnostic probe may collect offline evidence for new catalogs;
it is a separate discovery/comparison tool. Building a catalog, finding a
handler or loading a stage alone is not gameplay acceptance.

## Devices and media

The reusable devices model PRG/Word RAM mapping and ownership, interrupts,
CDD commands/response latches, CDC sector input and FIFO, DMA, stamp graphics,
RF5C164 PCM and CDDA timing. The 75 Hz drive counter is separate from the
video-frame counter. The final CDD correction retains a completed command's
response until the next command instead of replacing it on a mechanical tick;
the original WWZ and secret-stage-8 handshakes complete with this behavior.

`disc_image.*` and `tools/segacd/disc_image.py` use verified SGCDIMG1 raw media
converted from supported BINARY MODE1/2352/AUDIO CUE/BIN layouts. Stored
sectors remain unchanged, virtual gaps are represented explicitly, and bad
formats/integrity failures produce diagnostics. See the
[media contract](SEGA_CD_MEDIA.md) for TOC, gap, hash and sector limits.

Selected device behavior is adapted from permissively licensed ares code,
with its ISC notice retained. [Implementation reuse](SEGA_CD_REUSE.md) records
the pinned sources, integration choices and hardware-accuracy limits. No ares
CPU, emulator frontend or alternate game-execution backend is linked.

## Audio, persistence and presentation

`audio_native.*` timestamps FM/PSG writes and PCM/CDDA mixing on emulated
time, preserves chip phases and delivers complete intervals to host audio.
One frame of delivery latency supplies resampling lookahead. SDL's callback
does not advance guest devices. Queue, clock and mixing faults are sticky and
reported; stopping does not execute an extra guest frame to invent samples.

`backup.*` validates a full 8 KiB backup image before replacing RAM. Host
writes use exclusive same-directory temporaries, flush/commit and atomic
replacement. The original BIOS formats a new empty profile; launchers seed
only absent saves and preserve existing settings/BRAM.

The `SonicCD` runner supplies a resizable `Sonic CD` window, keyboard/controller
input and recomp-ui. The menu pauses guest/audio delivery and isolates held
input on close. Optional `--debug-port` never changes the gameplay caption.
Automated checks use muted output; owner playtests use normal audio.

Sonic CD's scene/layout/render/culling adapters are game-specific. The
released launcher defaults to adaptive / Fit to screen, with native and fixed
16:9 choices available. Supported normal stages draw real additional terrain,
actors and HUD content; cinematic and special-stage framing remains original.
Another title supplies its own presentation integration and qualification.

## Build, test and integration boundaries

The native runner target currently lives in `tests/segacd/CMakeLists.txt`.
`SonicCDRecomp` consumes it from its pinned engine. The shared hardware/native
interfaces are reusable, while the current Sonic CD source recipe, named
catalog prefixes, BIOS/disc checks and `standalone_program.c` registration
describe that game. New titles own their input validator, loader/program
registration, CMake target, source recipe and presentation in a separate game
repository; do not relabel Sonic CD offsets as a generic disc configuration.

Use [the setup guide](SEGA_CD_SETUP.md#start-a-different-sega-cd-title) for the
integration sequence and catalog-free synthetic tests. The tests need no
owned images. Gameplay coverage uses source-qualified stage selection,
teleports, protection and forced hits, with bypassed prerequisites recorded.
Original result/save/next-stage triggers execute; their success flags are
not written by QA.

Current hardware-accuracy qualifications are instruction-boundary bus/T-state
timing, exact analog DAC/display conversion and marginal-media/drive-error
behavior beyond the qualified disc. They do not permit missing executed code
to be skipped or original handshakes replaced with completion flags.

## Historical evidence

The earlier implementation diary is retained in
[Sega CD backend milestones](archive/SEGA_CD_BACKEND_MILESTONES-2026-10.md).
Its pending/audio/paused-state statements apply to their dated checkpoints.
The [final acceptance ledger](SONIC_CD_NATIVE_ACCEPTANCE.md) and public game
validation page supersede those checkpoints for release status.
