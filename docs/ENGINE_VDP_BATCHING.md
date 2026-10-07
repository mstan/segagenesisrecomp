# Shared-engine plane batching experiment

Owner: central issue `beads-q9k6`. Baseline: `d9d37b18e53277931e30485d0c13b61c6f8e766d`.

`GENESIS_VDP_PLANE_IMPL=SCALAR|BATCHED` is a fixed build choice defaulting to
SCALAR. BATCHED fetches and expands native tile-row spans for both background
planes, splitting at tile, vertical-scroll-column and window boundaries, before
the existing pixel compositor. Sprite evaluation, status, priority,
shadow/highlight, palette, widescreen margins and framebuffer delivery retain
their existing contract. This is an **exact renderer optimization**, not a
firmware or guest-function HLE substitution. No runtime selector or cartridge
leaf override is introduced.

The maintained scalar floor and candidate pass the existing renderer
differential: directed cases, 4,000 randomized cases, odd-width widescreen
canvases and import sequences compare framebuffer bytes and sprite status.
The build harness now accepts candidate compile definitions and renames current
public exports, allowing a scalar and batched copy in the same contract test.

## Opportunity ranking

Historical costs are workload-specific evidence from `docs/PERFORMANCE.md`,
not fresh profiles or predictions for all games.

| Rank | Category | Measured / estimated / unknown cost | Disposition |
|---|---|---|---|
| 1 | Graphics | Historical renderer dominance (original Sonic1 profile 72%); modern plane fraction unknown after earlier optimizations | Implement tile-row batching; require actual complete workload timing and correctness |
| 2 | Audio firmware / synthesis | S3K YMFM synthesis 12.388%, Z80 2.714%, PSG 1.279%; routing/mixing/delivery .214% | Strong later synthesis candidate; a replacement must preserve audible output/register behavior; do not assume table batching is HLE |
| 3 | CPU / scheduling | Interpreter fallback and dispatch differ greatly by title/coverage; HALT and frame cadence are observable | Preserve floor and inspect current attribution before another broad replacement |
| 4 | Memory / bus | Bus wrappers roughly 2% historically; earlier narrow candidates neutral/slower | Do not repeat small wrapper experiments without new cost evidence |
| 5 | DMA | VDP ports/DMA-related attribution .286% in prior S3K sample | Low priority for whole-workload material gain |
| 6 | External firmware | Z80 sound execution already has a floor; no DSP-style external firmware candidate identified | Audio synthesis remains higher measured opportunity than speculative firmware replacement |

## Reproduction and promotion gate

Run `python tests/runtime/run_vdp_render_diff.py --baseline-ref
d9d37b18e53277931e30485d0c13b61c6f8e766d --candidate-define
GENESIS_BATCHED_PLANES=1` with native Windows Python/GCC/Git paths.
Configure separate otherwise-identical title builds against this engine with
`-DGENESIS_VDP_PLANE_IMPL=SCALAR` and `BATCHED`. Existing finite `--benchmark N`
records now include `vdp_plane_impl`. Use the existing serial balanced-pair
harness, fixed affinity, full state/audio fingerprints and gameplay checkpoints.
The local noise gate in `docs/PERFORMANCE.md` applies; component-only timing
does not establish a whole-workload gain. Device cosim shares this engine and
cannot independently prove device correctness.

## Bounded native Windows result

Both fixed selections built successfully against an isolated Sonic1 title at
`a211eb4` and the same freshly generated source bodies. The existing 6,000-frame
real-input Green Hill workload matched all 100 framebuffer checkpoints at a
60-frame cadence. Interpreter diagnostics reported no true dispatch misses or
stack mismatches. Five measured pairs also matched complete architectural-state
and audio-state fingerprints (`350E1D9763B9161E`, `28A1D43F651DC5EE`). This is
bounded gameplay/renderer evidence, not a completed campaign or all-title gate.

Five CPU-2, normal-priority, order-balanced gameplay pairs after 600-frame
warmups produced FPS deltas of **-4.389%, +22.597%, +9.741%, +35.679%, +7.077%**.
The median was +9.741%, but the **40.067 percentage-point spread failed the
local 3-point noise gate**. Timestamped compiler censuses bracket every arm;
7..19 foreign compiler/build processes were present around measured legs.
There is no reliable isolated speedup claim and no repeat-until-pass result.

Private evidence is under `build/qualification/genesis-pairs/`:
`five-pairs.log` contains the unchanged harness summary, and
`raw-arms-and-census.json` contains every full benchmark record and census.
`build/sonic-gameplay-smoke.json` is the scalar framebuffer baseline used by
the candidate smoke. ROMs/generated sources remain ignored/private.

**Disposition: SCALAR remains default; BATCHED is an opt-in draft experiment.**
It has exact focused and bounded gameplay results, but whole-workload performance
is inconclusive under recorded host contention. Broader graphics/audio engine
work should build on these measurements rather than treating the noisy median
as a promotion gate.

## Representative workload pool

Use at most these three existing workloads for bounded opportunity discovery,
reusing evidence before another short capture. Sampling ranks hypotheses; it
does not qualify replacement correctness or whole-workload speedup.

This pool is not an automatic matrix. The six-system discovery pass stopped
at three usable profiles (Doom, Mega Man Zero, RKA) and two rejected setup
launches: five launches within its six-launch maximum, one configuration per
game. No comparative performance pairs were added.

| Workload | Existing floor and route | Observed evidence / remaining gap |
|---|---|---|
| Sonic1 | This branch's `build/sonic-scalar-final/SonicTheHedgehogRecomp.exe`; `tests/regression/sonic1_benchmark_gameplay.input`, 6,000 frames | Reuse 100 exact framebuffer checkpoints and the noisy paired result above; no additional timing matrix. |
| Rocket Knight Adventures | Local title `RocketKnightAdventuresRecomp/build-release-v0.1.13/Release/RKARecomp.exe`; `tests/stage1-walk.input` | Distinct action route: ordinary menu inputs, cinematic, then right/attack; no memory writes. Release metadata records title `0606d763`, engine `790393ae`, trace/reverse-debug/dev-trace OFF. Bounded production capture below supports VDP/FM hypotheses. |
| Sonic3K | `_wt_perf_sonic3k/build-perf-vdp/Sonic3KRecomp.exe`; historical gameplay evidence in `docs/PERFORMANCE.md` | Reuse historical 12,000-frame profile: 81,144 mapped executable samples, renderer 68.138%, YM2612 12.109%, Z80 execution/bus 5.733%, PSG 1.257%. Costs belong to that build/route, not current all-title estimates. |

The recorded RKA engine supports `--benchmark 6000 <ROM> --input-script
<stage1-walk.input>`: finite, uncapped, hidden, no audio playback/presentation.
It still creates an SDL window and requires an accelerated renderer. The first
`SDL_VIDEODRIVER=dummy` attempt failed before gameplay and is excluded. Removing
that override allowed the same binary/configuration/route to finish 6,000 frames
with hidden D3D, vsync OFF, child exit 0 and zero sampling errors. No rebuild or
`--headless`/`--no-audio` option was needed; those options do not exist there.

The corrected main-thread capture yielded 583 samples: **277 external/unresolved
(47.513%)**, `gvdp_render_scanline` 148 (25.386%), `sprite_render_line` 18
(3.087%), and `own_scanline_sink` 8 (1.372%). Five YMFM clock/volume/output/generate
functions total 55 (9.434%); `ym2612_advance` adds two samples. These exclusive
nearest-symbol samples support shared VDP and FM-buffer hypotheses. Startup is
included, external code unresolved, inline/inclusive attribution unavailable,
and the sampler perturbs execution: emitted FPS is not a throughput claim or
qualification of the batching pilot.

Matching MSVC map symbols (7,104 text functions), COFF timestamp `6ac2fdb5`,
preferred image base `0x140000000`, and sampler ASLR correction establish
attribution identity. Binary SHA256:
`F1778ADCF4174E17615E3BCCB9157F48D0796F50AC017CFC52307E1049A2907C`.
Final state/audio fingerprints were `F828B15E800221E2` / `CE7E5D895E21EEDA`.
Private raw evidence is `build/profile-subsample-20261006/rka/`:
`samples-native.csv`, `attribution.json`, matching `symbols.txt`, and child log.
Failed `samples.csv` evidence is retained separately and excluded.

Puyo remains outside this three-game pool. Old local attract/input-fuzz
artifacts and trace-enabled binaries exist, but the inspected old directory is
not a Git checkout and `docs/PERFORMANCE.md` excludes Puyo as a reproducible
public regression target. Binary presence alone does not establish a floor.
