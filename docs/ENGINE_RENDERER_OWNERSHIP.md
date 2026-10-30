# E10 renderer ownership and viewport policy

Owner `beads-3z5k.8`, parent `beads-3z5k`, campaign `beads-2wy8.1`.
Engine production `96be781` (initial implementation `39596bf`), base `014fa72` (accepted E08); shared core
`f7dd616`. Engine [PR43](https://github.com/mstan/segagenesisrecomp/pull/43)
stacks on PR42. Mechanical Sonic1 adoption `81d3bc6`, final source pin `6ecb59e`,
[PR7](https://github.com/mstan/SonicTheHedgehogRecomp/pull/7), pins that source.
Independent final whole-scope gate ACCEPTED production96be781 / reviewed a75cd9f
and companion6ecb59e. Reviewer explicitly instructed E11 next after status/sync/cleanup.
No merge/release approval.

A complete-row renderer can now own native geometry without enabling expanded
world simulation. `GameVideo.renderer_enabled` is an optional presentation
predicate, appended after the existing callbacks. Legacy NULL predicates retain
the existing enabled/valid-width selection. Sonic1 mechanically uses its current
eligibility predicate: no scene rules, stock renderer rollout or title logic
were added.

`GameVideoFrame` separates requested simulation width, viewport policy and the
local row owner/output width. Width publication always happens before local
ownership, texture limits and allocation checks. A local failure selects native
presentation without configuring the game off or enabling legacy guest margins.
A texture limit below the logical request now chooses complete native fallback,
rather than passing a reduced width into any legacy adapter. The plan is host-derived; it adds no machine/save/rollback/session state. Online
mode/width inputs keep their existing sealed simulation configuration.

## Complete original-family inventory

| Candidate/contract | Disposition |
| --- | --- |
| Complete-row ownership independent of aspect and expansion | Implemented optional pure predicate and shared prepare/draw contract, used by real cartridge runner and mechanically adopted consumer. Explicit owner can draw actual native geometry while viewport is off. |
| Existing adapters/positional layouts | Original callback order retained, optional field appended, NULL preserves legacy behavior. Real unchanged RKA consumer builds/runs against both engines. |
| Simulation publication and local fallback | Implemented separate viewport flag; width callback runs first. Renderer decline, capacity/texture/allocation fallback cannot configure mode off or newly arm legacy canvas/margins. Current netplay configuration identity is retained. |
| Lazy/eager/native/overlay rows | One complete producer is selected, native original fetched only where needed, overlay once afterward. Bounded helper returns initialized actual width; oversize plan falls back without stale advertised columns. Sprite status remains at the original machine deadline. |
| Native geometry and presentation/capture | Uses existing `gvdp_active_width`, already H32/H40/margin/canvas aware, and exact machine row width. No hardcoded 320 row copy or invented width API. Final width, framebuffer hashes, screenshot and output paths follow selected presentation geometry. |
| Renderer-only adapters | May provide complete rows without configure/enabled/width callbacks. They retain ordinary view controls and receive no unusable widescreen mod, CLI or session configuration. Defaults and actual mode controls remain compatible. |
| CD ownership | Already uses aspect-independent observer/defer/captured-row contracts. CD title width-320 eligibility is title policy; unchanged. Accepted E08/E09 full-canvas/status/capture/Linux evidence is reused, without new CD title route or speed claim. |
| Title native-fidelity rollout | Excluded from this engine goal, retained in TITLE_OPTIMIZATION_BACKLOG T08/T07. Production Sonic1 keeps exactly its existing eligibility. The test-only native owner proves generic capability without shipping new title behavior. |
| SIMD/GPU/workers or raster production fusion | No new dependency or ISA requirement. Universal native fallback remains. These are separate E11/E13-E18 contracts, not excuses to leave E10 unused. |

## Qualification and costs

Seven frame/raster tests pass on MSVC and GCC. The frame test checks 120
geometry/viewport/ownership combinations, legacy/eager/lazy/overlay, complete
native producers with no native fetch, bounds and simulation-policy preservation
on fallback. Actual Sonic1 consumer `video_mod` and `sonic1_game_video` tests pass
for reference and candidate. Full CTest output, XML and build configurations are
retained outside disposable build trees.

Initial four clean consumer PEs were built from frozen RKA `de902a0a` / UI `faa3330f`
and Sonic1 reference `510679e6` / UI `47005fb`, supported regeneration, default
Z80 interpreter, trace/reverse/dev off, netplay on, MSVC Release MT. Source and
binary SHA256, flags, input/ROM identities, capacity reservations, host telemetry
and raw per-game samples are in [machine results](engine-renderer-ownership-results.json).
All A/B pairs used the same local host/configuration, logical CPU14, below-normal
priority, isolated profiles, dummy video/audio and software SDL output. Audio
emulation and full WAV recording remained enabled; no physical GPU timing claim.

Initial production39596bf clean BAAB runs: RKA6000frames and Sonic12400frames, accepted input routes16:9.
All machine/audio state, full WAV, final PNG, frame hashes, rollback and dispatch
diagnostics match exactly across arms. Both candidate games also restore their
own current-build state with exact saved/loaded RAM, make progress after load,
and reproduce the earlier accepted lifecycle pixels. No cross-build state
compatibility is promised or tested.

| Game | Retained CPU seconds, in execution order | Interpretation |
| --- | --- | --- |
| RKA BAAB, then targeted AB | B13.921875, A13.921875, A13.0625, B14.53125; A14.390625, B13.593750 | Neutral/inconclusive. Pair CPU reductions0.00/-11.24/+5.54percent and cycles+0.85/-13.13/+6.32percent. Earlier adverse pair reverses; same-arm ranges overlap. All samples retained, no speedup or reproducible regression established. |
| Sonic1 BAAB | B2.546875, A2.515625, A2.484375, B2.484375 | Neutral/inconclusive. CPU reductions-1.24/0percent, cycles-2.89/+0.03percent; no speedup claim. |

This package delivers a usable engine contract and fallback correctness, not a
new measured FPS gain. Earlier CD native-region/E08 gains belong to those
packages and are not attributed here.

## Focused actual ownership and resource-fallback checks

A separate task-only adapter clone wraps the existing width callback and lazy
producer. Its exact source diff, source SHA, CMake replacement hook, PE and build
configuration are retained. It never changes production source and its costs
are excluded from performance samples.

- Viewport off: explicit native ownership produces537600rows, twice requests the
  memoized native pointer per row and asserts pointer identity, covers actual
  H32/H40 widths256/320, and exactly matches clean production native mode pixels,
  frame hashes, state, full WAV and rollback.
- Viewport16:9: identical fixture/env/input arms the existing legacy WS request
  before every frame's policy decision. Normal image398 versus forced local
  texture limit160/native fallback320 both execute2472width callbacks, request398,
  keep guest margin0 and actual native width320, and match clean398 simulation,
  audio/full WAV and rollback. Local image dimensions intentionally differ.
- Existing synthetic H32/H40/canvas geometry, per-row palette/status and CD
  captured-row tests qualify geometry outside this gameplay route; no additional
  title rollout or gameplay matrix was introduced.

Independent final review also found that supported local limits (320 through
requested-1) still invoked legacy adapters at a smaller width. Both RKA and
Sonic1 derive guest hooks from their requested width and historically overwrite
it during rendering. Final96be781 fixes this generically: a local texture limit
below the logical request selects exact native fallback. No adapter receives a
reduced width, so legacy consumers require no title logic change. The targeted
supported360 limit preserves requested398 and chooses native320, with
exact canonical state/fullWAV/rollback. Final clean RKA/Sonic1 binaries and their
canonical-route checks pass and are retained separately from all precursor samples.
Final one-run CPU costs are RKA13.671875seconds and Sonic12.437500seconds; these
canonical checks are not a new matched performance comparison.
No initial39596bf result is presented as a test of this final fix.

The initial post-run assertion incorrectly required render-only `s_width` to be
398 after clamping bypassed all custom rows. `s_width` is assigned only inside
`render_scanline`; the separate simulation `s_requested_width` is published by
`width` before ownership. The corrected offline verifier expects rendered0 on
fallback and validates unchanged state/audio/requests. Original failed assertion
and all raw runs remain; no game was rerun to hide a failure. Final supplemental
post-run comparisons also needed JSON tuple/list normalization; offline verifiers
checked preserved exact records without repeating game runs. Earlier harness
errors requesting an absent optional RKA test, JSON encoding and optional Pillow
import were resolved without any game correctness failure; details/identities
are in machine results.

Evidence: `C:/Temp/Codex/genesis-engine-render-ownership-20261029` and
`G:/Temp/Codex/genesis-engine-render-ownership-20261029`. Frozen PEs, WAVs, captures,
inputs, logs, configs, fixtures and verifier are retained. Disposable CMake/WSL
builds and process-local compiler TEMP were audited and removed after acceptance; borrowed source/dependency
junctions and private ROM fixtures are preserved for ongoing serial work.

Workflow gap: GitHub will not assign `TechnicallyComputers` on this repository
(eligibility endpoint404, add-assignee returns an empty list). The account exists.
No invitation or message was sent. This required assignment remains unfulfilled
and is recorded in PR/Beads; no merge is authorized.
