# Sonic 3 family custom video experiment

The combined **Sonic 3 & Knuckles** target was implemented first. Standalone
Sonic 3 and Sonic & Knuckles share the renderer, with a separately verified
S3 address profile. S3's object-index table is ROM-based and its competition
flag has a different address; neither is assumed to match the combined cart.

Enable **Widescreen** in **Mods** and select Adaptive, 16:9, 21:9 or 32:9.
It is disabled by default. The in-game settings overlay exposes the same
controls. Adaptive follows the window without a 32:9 cap. Development CLI
overrides include `--widescreen fit`, `off`, `stage`, and arbitrary `W:H`.

## Implementation

- Read the native 128px chunks, 16px block definitions and interleaved
  foreground/background row pointers. Do not rewrite collision data,
  decompression, native camera limits or tile streaming.
- Capture six-byte object mappings into a host display list before
  `Render_Sprites`; match completed lists against the actual uploaded SAT.
  Keep the displayed list immutable and reproject retained world positions.
- Draw rings from the ROM layout with native per-ring consumption state.
  Expand the native collision search; leave collection, attraction, rewards
  and collision routines intact. HUD stays screen-relative.
- Expand object activation using the same rounded cells as native culling.
  The native pool has 90 dynamic objects; nearest-player placements take
  priority when it fills. Host rendering has no hardware sprite-line limit.
- Optional main-CPU headroom retains VBlank, DMA and audio clocks. Native
  level waits still control gameplay ticks. Disabled, loading, competition
  and special-stage modes retain native CPU timing.
- AIZ1's upper forest bands repeat their actual 512px art section, not its
  complete layout containing the separate coastline and fire compositions.
  The outgoing background snapshot survives replacement-block decompression.
  The native two-row-per-frame intro refresh is counted as unstreamed
  background, not compared as if it already contained the incoming forest.
- AIZ's HUD-hidden landing cutscene is still a level, not a menu. Loading
  flags and valid layout pointers gate stage readiness instead of HUD visibility.
  Its negative virtual background camera repeats the first initialized 512px
  canvas, matching the native streamer's zero clamp.
- Intro and main-forest block/tile/palette banks are decoded into host-only
  storage from the user's ROM. The expanded margins can display both during
  the emerald cutscene and native VRAM replacement. Native-center rendering,
  animations, collision and the guest decompressor are unchanged. Decoder
  bounds tests and optional ROM asset hashes cover both cartridge profiles.
- Title scenery repeats its initialized 40 tile columns while the logo stays
  centered. Fades keep the selected output dimensions.
- Blue Spheres uses a host-only perspective ellipsoid: horizontal radius
  follows the window while local sprite/tile scale stays fixed. An inverse
  ray/surface cache draws the checkerboard and occludes far-side spheres.
  The native 32x32 board is scanned across the enlarged frustum, without
  the prerecorded 15x16 scan or 80-sprite presentation limits. Original
  distance-frame art, animated rings, characters, messages and shadows are
  retained. Both counter panels/icons anchor to the true screen edges.
  Board/position/mapping state is latched at VBlank before native DMA;
  rendering never changes guest state. The exit fade keeps this projection
  until the native special-stage video mode ends. S3 uses mapping metadata
  at RAM $8400; S&K/S3K use $E480.

## Ground truth and validation

The ignored local disassembly checkout had an older ROM-based widescreen
patch. A separate stock `skdisasm` worktree at `1e1b5af` was assembled: all
2,097,152 bytes of the S&K half match the owner's combined cartridge. A stock
standalone Sonic 3 build also matches its 2,097,152-byte ROM. Hook addresses
come from those listings, not the modified listing or generated C edits.

`tests/runtime/run_sonic3_custom_video.py` uses free-running input scripts
and read-only diagnostics. It copies the executable into isolated test
directories because SRAM is executable-relative. Reusing the same save file
otherwise changes stale save-menu RAM even with identical gameplay input.

The combined-cartridge checks cover the title, Angel Island intro,
forest transition and a scrolling/jumping route. Sixteen native RAM, VRAM
and PNG checkpoints match the pre-port executable byte-for-byte with the
mod disabled. Fixed 16:9/21:9/32:9 and Adaptive resizing are exercised, with
terrain/background checks and dispatch-miss checks. The script separately
checks stable post-intro gameplay for missed/double ticks and publications.
The native intro asset-loading sequence still spans a few gameplay ticks;
this is not a claim that all transitions or all stage-length views run at
60 gameplay updates per second.

Standalone checks use `--variant sonic3` or `--variant sandk`, with separate
pre-port native references. S&K's first route is Mushroom Hill. The unit suite
also compiles the S3 address profile separately and exercises the actual Mods
launcher model with the consumer's compile gate. Runtime tests use one game
at a time and port 4386 in isolated diagnostic directories.

Example (from the consumer repository, with its actual nested engine):

```powershell
python segagenesisrecomp/tests/runtime/run_sonic3_custom_video.py `
  --exe build-sonic3-custom/Release/Sonic3KRecomp.exe `
  --rom segagenesisrecomp/sonic3k/sonic3k.bin `
  --out build-sonic3-custom/validation --modes off 16:9 32:9 2676:374
```

`tests/runtime/run_sonic3_blue_spheres.py` takes a user-owned save plus a
pre-change executable. Isolated runs verify byte-exact native RAM/VRAM
through moving, turning, collection, jumping and the red-sphere exit, with
byte-exact output when disabled. Fixed 16:9/21:9/32:9 and 2676:374 exercise
the same projection; the original save is hashed before/after. Unit tests
cover projection/intersection agreement, native camera reference points,
sprite counts beyond 80, host-only state and both cartridge address profiles.

Remaining certification includes other zones/boss routes, burning-island
transitions, bonus stages, other Blue Spheres layouts/emerald-clear sequences,
standalone special-stage runtime routes, and saves during expanded activation.
The experiment is not ready to publish as a full-game-certified mod.
