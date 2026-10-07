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
