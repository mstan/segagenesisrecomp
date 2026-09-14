# Sonic 2 custom video (REV01, experimental)

The native renderer is the default. `sonic2_spec.c` supplies the optional
`GameVideo` interface and instruction hook. The game consumer enables
`RECOMP_UI_ENABLE_MODS` and reuses the shared built-in Widescreen provider:
Adaptive, 16:9, 21:9, and 32:9. No separate mod archive is required.

## Scene and object model

`sonic2_video.c` reconstructs foreground/background from the interleaved
128-pixel layout at $8000/$8080, chunk table at $0000, and 768-entry block
table at $9000. Canonical references are `GetBlock` ($E244), `ObjectsManager`
($17AA4), `BuildSprites` ($16604), and `RingsManager` ($16F88).

Each displayed frame snapshots terrain, VRAM art, scroll, and the host sprite
list. CRAM remains live for water-line palette changes. Sprite publication
matches the complete native SAT DMA bytes to the corresponding host list.
The displayed front buffer retains its own serial/SAT identity after producer
history slots roll over. If a native DMA contains an incomplete sprite list,
the last completed same-scene custom frame remains displayed instead of
flashing back to a centered native HUD and dropping expanded rings. Captured
camera coordinates keep retained world sprites aligned when scrolling moves
ahead; HUD coordinates remain screen-relative. Scene changes and renderer
toggles prevent an old frame from leaking into a different scene.
The host compositor lifts native sprite-count, line-count, and coordinate
wrap limits. Original object logic, collision, ring rewards, and stage
decompression still run; the renderer never rewrites collision/layout data.

The optional placement loader and 44 native cull comparisons share the same
128-pixel-rounded activation interval. In particular, a placement must not
spawn in a cell the culler will immediately delete. Rings are read from
Sonic 2's dedicated ring table, and both Sonic and Tails use the original
ring collision/consumption code with an expanded search interval. The
original 112 dynamic-object slots remain finite; nearest-player placements
are prioritized when an extreme viewport reaches capacity.

After mid-level opt-out, an already adopted loader keeps a native-width
activation window until the next level initialization. A fresh disabled run
never takes that path. This preserves live objects without reconstructing
their state when switching renderers.

Title scenery extends behind the centered logo; title cards/fades keep the
chosen canvas. Emerald Hill's parallax is reprojected at stage edges to avoid
camera-start sliding. Special stages use H32 plus shadow/highlight: their
repeating Plane B expands, while the native half-pipe projection, sprites,
and HUD remain centered. Two-player competition remains native; the shared
runner disables this renderer for netplay.

## Validation

### Enhanced gameplay cadence

The widescreen mod also supplies `GameSpec.main_cpu_divisor`: 4 during active
single-player levels/demos, 1 elsewhere. Wider activation can otherwise make
the original 68000 workload exceed its frame budget even while presentation
continues at 59.94 FPS. `Level_MainLoop` ($4360) still executes its original
`WaitForVint` and one physics update per tick; no movement or timer constants
are changed. The shared scheduler compresses only main-program CPU time
before bus operations stamp audio events, carrying fractional cycles across
accesses. VBlank/HBlank handlers (including interleaved IRQs), DMA stalls,
Z80, raster and audio playback retain native clocks. The default NULL hook
and disabled mod retain native timing. This is an opt-in enhancement, not
a correction to the original hardware clock.

`tests/runtime/run_sonic2_performance.py` exercises real controller routes and
queries the always-on frame ring for `Level_frame_counter` ($FE04), the actual
gameplay tick, separately from the VBlank counter. The per-game VBlank wrapper
also samples ticks and completed sprite publications at IRQ entry. Variable
native V-int/DMA debt can move the next tick across the end-of-wall-frame sample
point, producing a paired 0/2 there without missing a VBlank update.
`--require-full-rate` rejects missed or multiple ticks/publication gaps at
the actual VBlank boundary; `--zone` selects heavy-stage
routes. Host elapsed time is reported separately and includes probe overhead.
For example, use the executable/ROM paths below with
`--mode 2676:374 --zone cnz --out build-sonic2-custom/perf-cnz --require-full-rate`.

### Build and visual checks

Build from the game consumer with the intended framework checkout, especially
if a local `engine-local` override exists:

```powershell
cmake -S . -B build-sonic2-custom -G "Visual Studio 17 2022" -A x64 `
  -DGENESIS_RECOMP_ROOT=F:/Projects/segagenesisrecomp/SonicTheHedgehog2Recomp/segagenesisrecomp `
  -DSONIC_REVERSE_DEBUG=OFF -DGEN_DEV_TRACE=OFF -DGEN_ENABLE_TRACE=ON `
  -DBUILD_TESTING=ON -DRECOMP_UI_ENABLE_MODS=ON
cmake --build build-sonic2-custom --config Release --parallel 8
ctest --test-dir build-sonic2-custom -C Release --output-on-failure
```

Replace the example absolute checkout path for your machine. Runtime checks
need the user's REV01 ROM and a `debug.ini` beside the executable containing
`port=4442`. Use one running game at a time. Python requires Pillow.

```powershell
python segagenesisrecomp/tests/runtime/run_sonic2_custom_video.py `
  --exe build-sonic2-custom/Release/SonicTheHedgehog2Recomp.exe `
  --rom segagenesisrecomp/sonicthehedgehog2/sonic2.bin `
  --out build-sonic2-custom/checks
```

The default checks native, 320-pixel custom, four fixed widths through 64:9,
and full-stage width. Add `--modes fit --resize` for live adaptive resizing,
or `--zone cpz`, `--zone arz`, `--zone cnz`, or `--zone ss` with
`--modes off 32:9` for other scenes. Zone selection uses the real sound-test
cheat and controller inputs, never paused simulation, state warps, or RAM
patches. `--native-reference <baseline/off>` additionally compares all native
checkpoint RAM, VRAM, and PNG files byte-for-byte against a pre-port capture.

Checks cover unchanged initial stage data, zero dispatch misses, terrain and
background agreement with the valid native streamed area, screen-anchored
SCORE art, selected output width through transitions, and non-black title/SS
margins. Active custom gameplay must never drop its published sprite frame.
Unit tests cover partial sprite DMA, producer-history rollover, horizontal
and vertical reprojection of a retained frame, scene/toggle invalidation,
default opt-out, aspect selection, flips, block
addressing, spawn/cull boundaries, Sonic/Tails ring gates, H32 shadow mode,
and immutable art with live palette changes. Shared Mods tests exercise the
actual launcher compile gate and settings persistence.

`custom_video` exposes coverage/error counters. Two native streaming limits
are explicitly distinguished from renderer errors:

- `SwScrl_EHZ` initializes only 222 of 224 scroll rows. The custom renderer
  extends the final initialized row and reports the two excluded lines.
- `Draw_FG` streams 16-pixel rows from `(copied_camera_y & ~15) - 16` through
  `(copied_camera_y & ~15) + 240` (exclusive). During fast falls, live VScroll
  can advance before the next tile row is streamed. The custom renderer draws
  real stage geometry there and counts `native_unstreamed_terrain_samples`,
  rather than treating stale native VRAM as an oracle. A completed flat-color
  fade likewise has no visible geometry to compare during scene reload.

`scene_match_misses` and `scene_held_frames` report native sprite uploads that
did not match a complete publication, and the subset safely displaying the
retained front buffer. They are not terrain or dispatch errors.

The performance pass verified exactly one tick and completed sprite publication
per VBlank on EHZ, MTZ and CNZ input routes at 1603 logical pixels. Real-time
Adaptive at 2676x374 held 59.94 FPS with no audio delivery underruns/drops.
Native Sonic 2 checkpoint RAM/VRAM/PNG and Sonic 1 native/32:9 checkpoints
remain byte-identical to the pre-change captures. ARZ water, special stages,
fixed 16:9/32:9 and adaptive resizing also pass the renderer checks.

The older `boot_smoke_baseline.json` dates to commit `a632a85` (2026-05-18),
before the own-backend migration and removal of the old emulator core
(`cb645c5`, 2026-07-27). At wall frame 60 it expects
Vint_runcount=17, whereas both recorded pre-performance and updated runs have
Vint_runcount=0. The snapshot was not rewritten to hide this pre-existing
initialization-timing mismatch. Use the byte-identical native route captures
and `run_sonic2_release_smoke.py` for current own-backend release regression
evidence. The production smoke cold-boots persisted Mods choices without TCP
or debug instrumentation, compares ten RAM/VRAM/PNG checkpoints per reference
mode, checks native opt-out, and rejects dispatch misses.

Whole-stage width is a stress/debug option, not a guarantee that every actor
across an entire level can run simultaneously. Full boss routes, all acts,
and expanded special-stage track projection remain beyond this first pass.
