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

## End-to-end engine strategy

### Current broader implementation (pending gameplay/performance)

`GENESIS_VDP_PLANE_IMPL=BATCHED` now covers the complete shared scanline
service: existing background tile spans, eight-pixel sprite row decoding and
a 64-entry immutable priority selector. Sprite cells/pattern addresses are
resolved once per row instead of per pixel; composition selects packed layer
colors instead of walking six conditional priority branches per pixel. The
scalar implementation remains buildable/default with the same public ABI.
No persistent cache/state serialization, timing identity or live handoff is
introduced. This is a native renderer optimization; no guest leaf is replaced.
The existing focused renderer differential passed its directed, 4,000 random,
odd-canvas and import-sequence cases. This is not a gameplay screenshot sweep
or a requirement for owner-visible pixel identity.

`--measure-runtime N --runtime-uncapped` measures finite full-runtime work:
guest/device execution, actual scanline rendering, texture upload/presentation,
audio synthesis, mixing and delivery-ring push. Only host manual pacing/vsync
are disabled. A failed audio device aborts measurement. JSON records wall FPS,
all-thread process CPU/cycles, implementation, scope, requested/completed frames,
audio flushes/mixed sample counts and bridge push/overflow counts. The bounded
audio ring drops oldest source frames when production outruns device playback;
all new samples are still synthesized/mixed/pushed. Thus this is full-runtime
uncapped throughput, not normal-speed audio quality. `--measure-runtime N`
alone retains normal pacing, suitable for CPU/frame confirmation if needed.
`--benchmark N` remains explicitly `uncapped-core`, excluding presentation and
audio delivery; it cannot alone qualify this change. Ordinary owner launches
use neither measurement flag and remain normally paced.

Malformed/conflicting measurement options fail before ROM/SDL initialization;
`tests/runtime/runtime_measure_cli_test.py <runner.exe>` checks that boundary.
The bounded full-runtime results below are complete; the owner visual/playcheck
is pending. The earlier plane-pilot measurements remain historical.

### Broader scanline result (Windows, 2026-10-06)

Separate Release scalar/batched builds used the same engine sources/compiler,
trace OFF and fixed per-game input/configuration. RKA title was `3e07af86`,
Sonic1 `a211eb4`, and Sonic3K `6bf6fd04`. One full-runtime uncapped pair ran for
each game. Only the marginal RKA/S3K results received a reversed pair; Sonic1
was not repeated and no matrix or gameplay screenshot sweep was run.

| Game | Initial FPS gain / process CPU reduction | Reverse FPS gain / CPU reduction | Disposition |
|---|---|---|---|
| RKA, 6,000 input frames | -2.530% / -4.885% | -15.409% / -15.945% | Failed gain; keep scalar, candidate remains draft. |
| Sonic1 GHZ, 6,000 input frames | +20.533% / +10.563% | Not run | Useful initial candidate for this Windows title/configuration; pending owner playcheck. |
| Sonic3K attract, 12,000 frames | +1.581% / +5.827% | +5.190% / +1.912% | Small/immaterial result for this planning target; keep scalar, no default qualification. |

Every arm finished successfully with the requested frames and matched per-pair
audio flush/mixed FM/PSG/pushed sample counts. End state/audio fingerprints also
matched as supplementary metadata; they were not a pixel-perfect gameplay gate.
Uncapped delivery overflow occurred as expected and is recorded, not represented
as normal audible output. Relevant renderer tests and 13 pre-initialization CLI
checks per arm passed. No current visual/playcheck has yet been claimed.

Compiler censuses bracket each arm. Initial RKA showed a resident MSBuild node;
later Sonic1/S3K snapshots and reverse legs observed foreign PSX CMake/Ninja/GCC
activity. Reverse RKA/S3K snapshots contained 4..8 foreign build processes.
Presence is not measured CPU interference, but host isolation is unproven and
the single Sonic1 pair is not a stability estimate. No foreign process was
killed or altered. These mixed results do not support a whole-system default.

Private evidence lives in `build/vdp-service-qualification/initial-pairs/`,
`reverse-rka/`, and `reverse-s3k/`: full raw logs/results, binary/ROM/input/config
identity and per-arm censuses. `build/vdp-service-build-identity.json` records
source hashes and six fixed builds. ROM/generated/binary payloads remain ignored.

Ready normal-paced Sonic1 handoff: `build/sonic-batched-final/`
`SonicTheHedgehogRecomp.exe` with the owner's `sonicthehedgehog/sonic.bin` as the
positional ROM, without measurement/turbo/input-script flags. Scalar alternative:
`build/sonic-scalar-final/SonicTheHedgehogRecomp.exe`. Owner launch waits until
the coordinated measurement wave ends; positive feedback is still required.
`SCALAR` remains the shared default and this PR remains draft; no merge/default
promotion or issue closure is implied by the Sonic1 result.

Windows is the first supported target. Use three actual games, one production
configuration per game, and maintain a functioning LLE build with the same
caller ABI. The owner judges practical appearance and playability; prospective
validation does not require pixel-perfect old/new images or internal-state
identity. Historical exact comparisons above remain completed evidence.

### First engine boundary

Start with the shared VDP scanline render service: accept existing VDP
register/VRAM/CRAM/VSRAM and sprite inputs and deliver pixels plus caller-visible
sprite status through the existing interface. RKA's measured scanline/sprite
cost and historical Sonic3K renderer dominance support this shared boundary.
Broaden row decoding/compositing where repeated work can be removed. The current
plane-batching pilot's gain is inconclusive; it is not ready for owner handoff
or default promotion. The current plane batching is an exact renderer
optimization; a broader replacement may use HLE's internal freedom behind the
shared caller contract. Classify the final implementation honestly.

FM block synthesis remains a separate measured candidate, supported by RKA/S3K
audio samples. Do not combine it with VDP changes in the same performance pair.
If VDP work cannot produce material whole-game savings, record that result and
consider FM on its own rather than indefinitely expanding the graphics pilot.

### Selected games and floor readiness

| Game | Actual route / useful observations | Production readiness |
|---|---|---|
| RKA | `tests/stage1-walk.input`: menus, opening cinematic, then sustained right/attack after controllable entry; finite 6,000 frames. | Existing v0.1.13 Release runs with native accelerated SDL video, trace OFF. Use matched LLE/candidate builds from one pinned title/configuration. |
| Sonic1 | Existing 6,000-frame GHZ script: wait for Game_Mode 0C, hold right and jump. | Isolated scalar floor works; preserve equivalent generated code/configuration in the candidate. Reuse existing route without a new checkpoint sweep. |
| Sonic3K | `tests/regression/sonic3k_attract_12000.input`: title, moving demo gameplay and title/demo transition. | Existing production binary and historical cost evidence exist. Confirm the matched floor runs this route; historical gameplay cost is not automatically this attract route's cost. |

The caller contract is ordinary input response and game progress, valid render
outputs/status, and working sound through these routes. Preserve the ABI and
supported device behavior. Use relevant existing renderer/runtime focused
tests; add a test only for a concrete uncovered correctness risk introduced by
the implementation. Do not add automated screenshot/state/audio checkpoint
sweeps or campaign/lifecycle gates as a prerequisite for this bounded task.

### Measurement and decision

Before code, state the candidate boundary and a material gain target; 10%
whole-work reduction is a planning target, not a universal acceptance threshold.
Use one matched uninstrumented LLE/candidate performance pair per game and
report FPS and percentage gain with the same finite inputs/useful work. Uncapped
Windows measurements are allowed. Keep coverage diagnostics separate and
identify startup, pacing and framework overhead when interpreting results.

Repeat a reversed pair only if observed noise or ambiguity prevents a
conclusion. There is no mandatory eight-run quota, parameter matrix or
repeat-until-pass loop. Existing noisy Sonic1 results do not establish gain.
A cold route or immaterial/noisy gain parks the candidate; it does not justify
forcing a playtest. Any performance conclusion must exceed the actual noise and
compare equivalent useful work rather than a shortened or skipped workload.

### Visual check and owner handoff

Once the replacement runs, make one basic current-implementation visual sanity
glance: does the game look right and avoid garbled output? No baseline image
matching is required. Escalate with targeted investigation only if that glance,
a focused test or owner feedback reveals a concrete defect.

After objective gain and focused correctness pass, prepare normal-paced Windows
candidate builds of all three selected games with ordinary controls, ROM launch
instructions and the maintained fixed LLE alternative. Launch every ready
selected game for the owner without asking again for launch permission, and ask
whether each looks and plays right. Do not present the old inconclusive pilot
as ready or use uncapped benchmark mode for this human check.

Positive owner feedback plus material objective gain permits merge and default
candidate/HLE behavior for the supported Windows scope, retaining a fixed LLE
build opt-out. Record the supported games/configuration and evidence, then
close the owning issue. A reported defect returns only the affected behavior
to focused investigation; broader platforms remain outside this initial scope.
