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

The first implementation boundary is the shared VDP scanline render service:
consume the same VDP register/VRAM/CRAM/VSRAM and sprite inputs, produce the same
pixel row and caller-visible sprite status, and retain its ABI and scalar build.
RKA's measured scanline/sprite samples and the historical Sonic3K renderer cost
support this boundary across games. Broaden tile/row decoding and compositing
only where it removes repeated render work; the present plane-batching pilot
has inconclusive gain and is not a proven starting speedup. Exact pixels/status
are the promise for this candidate, so exact comparisons remain required.
FM block synthesis is a separate candidate supported by actual hot RKA/S3K
samples, not an additional change mixed into the VDP comparison.

| Title / role | Concrete route and comparison milestones | Production floor readiness and caller contract |
|---|---|---|
| RKA / primary | `tests/stage1-walk.input`: observe menu states at FFB002/FFB004, enter game, cinematic ends (FFB194=0), then sustained right/attack. Compare first controllable frame, scrolling/action frames and final 6,000-frame state; report active-game frames separately from startup. | Existing v0.1.13 Release is profile-ready with native accelerated SDL driver and trace OFF. Build both selections from one recorded title/engine/configuration before comparing. Check scroll/plane/sprite pixels, sprite status, player/menu progress and audio continuity; do not count cinematic time as an action-window gain. |
| Sonic1 / companion | Existing 6,000-frame GHZ script: wait for Game_Mode 0C, hold right, repeated A/B/C jumps. Reuse the 100 checkpoint floor, compare GHZ entry, running/jumping/scrolling and final progress. | Existing isolated scalar floor is functioning. Produce matched candidate/control builds without changing generated source bodies. Check exact framebuffer/status plus player progress, valid dispatch and unchanged sound; previous noisy timing is not a gain. |
| Sonic3K / companion | Existing `tests/regression/sonic3k_attract_12000.input` provides a concrete attract route: title/demo entry, moving demo gameplay, return/title cycle, final frame. Historical 12,000-frame gameplay samples establish cost but are not automatically this route's profile. | Historical production binary exists; first confirm its pinned route/ROM/configuration and bounded demo progression in the matched current floor. Compare scrolling/window/sprites and sound through demo transitions. Attract evidence does not qualify a newly scripted player stage or campaign. |

Before implementation, record the active-game events/frame interval, eligible
render coverage and a material whole-work reduction target (10% is a planning
target, not a universal acceptance threshold). Preserve one production
configuration per title. Establish comparable useful work with milestone/output
checks; collect all-thread CPU and wall time, including framework/pacing tails,
and keep coverage diagnostics separate from uninstrumented timing binaries.
Run primary RKA ABBA (two balanced pairs), then one A/B per companion. Apply the
local noise gate and common six-system protocol; do not repeat automatically or
expand to a parameter matrix. If valid measured savings do not exceed noise and
the declared useful-work target, park the VDP experiment as draft. If broader
render work is still hot but this boundary cannot deliver savings, record that
failure before selecting the separately measured FM boundary; do not chain
unbounded profiles or stack speculative changes.

A passing VDP candidate must preserve all three route contracts and avoid new
stalls at their tested transitions. Then provide the owner a normal-paced,
ready-to-launch RKA HLE/candidate desktop build with ordinary controls, ROM
selection instructions and a fixed LLE alternative. Ask the owner to judge
scrolling, responsiveness, sprite effects and audio during actual play, not a
benchmark-only session. After the measured gain, automated comparisons and
owner feel check pass, merge and enable the candidate only for the explicitly
qualified platform/title/configuration scope, retaining the LLE build opt-out;
close the owning issue with that scope and evidence. Companion automation does
not establish all games, campaign completion or every host platform.

## Measurement, decision and delivery protocol

Owner completion rule: establish a material game-workload gain and automated
compatibility, then deliver the final playable build for the owner's feel check.
After that check passes, integrate the prepared default change and close the
scoped work. Exhaustive game coverage and completed campaigns are not additional
completion requirements.

1. **Pin the workload and floor.** Use the three games and concrete routes above.
   Build LLE and HLE from the same title/framework revisions, compiler/options,
   ROM/firmware identities, presentation/audio settings and initial game state;
   only the selected implementation differs. Keep the replaced LLE service
   runnable. An old executable is discovery evidence, not a mismatched control.
   Use native game saves or replayed inputs when private savestates cannot cross
   builds. First resolve the named route/build gaps; do not perfect unrelated
   hardware before replacing a functioning operation.
   Verify that companion routes actually exercise the replacement; an unaffected
   title is a regression control, not evidence for that HLE service. If the
   chosen service changes, replace an unsuitable companion in the three-title
   set instead of accumulating extra games or claiming unexercised coverage.
2. **Attribute only what is missing.** Reuse suitable profiles and collect at
   most one new active-workload attribution capture per selected game in this
   implementation round. Identify the intended service's eligible dynamic work.
   Include worker threads and external modules or report them unresolved; a
   main-thread symbol histogram cannot supply a whole-process cost percentage.
   Capture diagnostics separately from performance. End discovery when there
   is enough evidence to select a useful service, not when every subsystem has
   a profile. The earlier six-launch discovery cap applied to that completed
   pass, not to the whole implementation/qualification program.
3. **Choose one replacement.** Record its caller ABI, inputs, outputs, observable
   side effects, supported operation scope, permitted tiny differences, expected
   cost removed, and candidate-specific useful gain before coding. Implement a
   shared service with build-time LLE/HLE selection and explicit build identity.
   Do not stack several speculative replacements into the same comparison.
4. **Measure equivalent active play.** Delimit a fixed gameplay window by guest
   frames and meaningful game events, excluding boot, warmup and teardown.
   Choose enough active work to dominate measurement granularity once, then keep
   it fixed. Report total process CPU milliseconds per guest frame (all threads),
   critical-path frame work, median/p95 frame time and missed presentation/audio
   deadlines where available. Record peak memory and code size, since constrained
   targets matter. Preserve normal renderer and audio production; a benchmark
   that omits presentation/audio is a core-only diagnostic, not end-to-end proof.
   The owner selected Windows first and authorized uncapping for useful
   measurements. Prefer a finite uncapped comparison where it preserves the
   same game, render and audio-synthesis work. Remove host frame-delay/VSync
   waits only in isolated benchmark configuration; do not change the guest
   timing model, resolution, effects, audio workload or HLE coverage between
   builds. Report uncapped FPS and milliseconds/frame alongside total CPU/frame,
   and verify completed render/audio work and game progress rather than trusting
   a frame counter alone. A legacy benchmark that skips rendering/presentation
   or audio remains core-only evidence; use a complete paced CPU/frame comparison
   until that benchmark path can exercise equivalent work. Normal capped play
   can show reduced CPU/frame even when FPS stays unchanged. Measure GPU
   completion/queue cost when work moves there; a shorter submission call alone
   is not a win. Keep the final owner-playtest package normally paced.
5. **Use a fixed comparison budget.** The primary game gets LLE/HLE/HLE/LLE:
   two order-balanced pairs, four measured executions. Each of the two companion
   games gets one LLE/HLE pair, two executions each. That is eight measured runs
   per candidate on one declared host/configuration, not a Cartesian matrix.
   Reuse their progression telemetry and final outputs; take expensive milestone
   captures outside timing, and use isolated LLE/HLE fixtures for detailed
   contracts. Do not automatically add separate full campaigns or trace runs.
   Keep team builds/profiling out of the timed window, record host load/power/
   thermal conditions, and preserve every result. A noisy or contradictory result
   stops that screen; fix an identified condition before a bounded replacement
   measurement. Never repeat until a passing subset appears.
6. **Decide from useful gain and compatibility.** Report both paired percentage
   and absolute savings, with the observed pair spread. About 10% lower whole
   active-workload CPU time is a planning aim, not a universal acceptance rule.
   A candidate may instead solve a declared frame-budget or stutter problem.
   Both primary pairs must show a clear consistent useful improvement beyond
   observed noise; two pairs are not a formal confidence interval. Companion
   single pairs screen for large regressions, not proof of zero performance
   change. Explain any apparent regression before broadening defaults. Exact
   promises require exact outputs; permitted approximations use a declared
   practical image/audio/result comparison. Check input, audio, progression,
   affected completion/IRQ consumers, transitions and relevant pause/reset/save
   behavior. No crash, softlock, stale buffer, lost completion or save corruption
   passes. A huge isolated kernel ratio cannot substitute for this decision.
7. **Hand off the actual finished candidate.** Provide the named primary game as
   a ready-to-launch normal-paced HLE package, an LLE comparison build, isolated
   save/checkpoint setup, launch instructions and checksums/build identity. Include
   a short before/after report, companion results and any tiny known differences.
   Prepare the intended default-selection/integration change in the draft PR so
   the owner tests the package intended to ship. Ask the owner to play normally
   and assess response, motion/collision, camera/scrolling, stereo where relevant,
   audio rhythm and continued progression. There is no prescribed full-campaign
   completion or multi-game human test matrix. Owner rejection reopens the
   affected behavior; fix and recheck that change before another handoff.
8. **Finish the scoped delivery.** After owner acceptance, integrate the reviewed
   candidate, make HLE the default for the supported titles/platform/service,
   retain a documented build-time LLE opt-out, and record the measured and manual
   evidence before closing the issue. Do not add unrelated qualification gates
   after the agreed playtest. If the replacement cannot deliver material gain,
   preserve its branch and draft PR with results, explain why, and choose a new
   boundary deliberately; an unsuccessful experiment is not a completed system.

The owner selected **Windows first; port measurements later**. Windows x64 is
therefore the initial implementation, measurement, final-playtest and default
scope. After automated checks, material gain and the owner's normal-paced feel
approval, finish that Windows delivery; a mobile/Xbox port is not a new gate
before closure. Later port work carries the winning candidate and relevant
routes to the chosen target and measures there before claiming target savings.
Do not multiply all hosts into the Windows discovery/comparison matrix.
