# E11 whole raster fusion evaluation and batched default adoption

Owner `beads-3z5k.9`, parent `beads-3z5k`, campaign `beads-2wy8.1`.
Base `4166e74` (compiled production `96be781`), retained production `50b0854`.
Engine [PR44](https://github.com/mstan/segagenesisrecomp/pull/44) stacks on PR43.
Independent source/tests accepted; final PR/record gate pending. No merge/release approval.

The full native tile/window/scroll/decode/priority/shadow fusion was implemented,
qualified and reached by actual games, then rejected against the best existing
BATCHED renderer. It added no demonstrated incremental benefit in RKA and
reproducibly slowed Sonic1. This is a completed bounded whole-family experiment,
not a deferred design or a claim that every future raster algorithm is exhausted.

The useful production change reuses the already-delivered BATCHED producer as
the shared CMake default. Explicit SCALAR/BATCHED selections remain supported,
and existing caches are not forced. Before promotion, generic host/provider
compatibility holes were fixed. No FUSED option/runtime/counter is shipped.
Gains from selecting existing packed kernels belong to reuse/default adoption,
not to the rejected new fusion. No title algorithms or eligibility changed.

## Complete original-family inventory

| Candidate or required contract | Disposition and evidence |
| --- | --- |
| Plane A/B/window, full/column/cell/line scrolling, tile decode and flips | Whole row-local packed-span fusion implemented in96decaf, removing full-row plane arrays/derivation and composing final output directly. H32/H40, IM2, horizontal/vertical wrap, window edges, shadow/highlight, odd511 canvas and random modes qualified. Rejected versus best existing BATCHED after actual games. |
| Sprite lanes, SAT/private art, host providers, limits and status | Original exact sprite evaluation retained, then existing batched sprite rows reused in36d5c18. Both real private SAT pieces/art banks and native-art control tested. Limits/collision/overflow/status happen at the same deadline, including discard/lazy/captured rows. |
| Opaque callbacks and public extended plane colors | Final source captures host presence before invocation, normalizes arbitrary nonzero opaque/high flags before both packed and portable table composition, handles self-unregistering callbacks exactly once, and uses full8-bit scalar plane fetch/composition for extended plane providers. Sprite/callback/status work is not repeated on fallback. |
| Native packed kernel and universal scalar/table paths | Existing native BATCHED kernels retained; explicit SCALAR remains. Portable candidate disables native tile rows/composition and passes actual Win32/x86 plus x64/GCC. No assembly/CPU feature/new dependency or ABI requirement. |
| Immutable captured/deferred rows and current-build state | Producer/capture/status layouts unchanged; live writes, import/reset, immutable captured rows and palette output remain exact. Current-build saves restore exact RAM and continue gameplay; rollback probes pass. No backward-state compatibility work. |
| CD/cartridge ownership and runtime selection | Actual cartridge native/off routes exercise original VDP in both games; custom16:9 routes verify existing adapters/fallback. CD direct CMake target bypasses shared default and stays source SCALAR; prior E08/E09 exact CD capture/runtime evidence reused, no fresh title rollout/matrix. |
| Reachability, rather than unused prototype code | Task-only36d5c18 source clone/counters show RKA1,342,248 fused rows/422,222,336pixels and Sonic1518,181 fused rows/165,817,920pixels. Clone diff/build hooks/PEs/raw correctness retained. Those diagnostic costs are excluded and counts do not describe final BATCHED. |
| Best existing implementation counterfactual | RKA FUSED versus existing BATCHED on identical36source: CPU reductions+1.00/-3.15%, cycles+0.29/-1.40%, same-arm CPU drift-4.98%. No incremental win. Preserves already-delivered packed fetching/composition rather than attributing its gain to fusion. |
| Alternative architectural experiments | Initial direct streaming, provider consolidation, then existing sprite-kernel reuse all built and tested. Sonic1 regressions persist; no further FUSED tuning. GPU/workers/cache/final-upload are distinct E13/E15-E18 contracts, not silently counted here. |

## Experiments retained, including failures

`96decaf` initial complete fusion passes all state/audio/pixel contracts. RKA
SCALAR-to-FUSED CPU reductions31.51/24.02% do not prove an incremental fusion
benefit over existing BATCHED. Sonic1 samples are mixed.

`9337e36` consolidates generic host/private-provider compatibility. RKA remains
faster than SCALAR, but Sonic1 forced FUSED costs6.45/6.52% more CPU and
4.63/5.80% more cycles. The source is rejected for adoption.

`36d5c18` reuses existing batched sprite rows across the whole fused pipeline.
The focused final AB is RKA A6.234375/B4.718750 CPU seconds, while Sonic1
A1.531250/B1.765625 costs15.31% more CPU and11.06% more cycles. Sonic1 regression
cannot be offset by RKA. The decisive RKA BATCHED counterfactual above then
rejects additional fusion complexity for that game too. Git source, all PEs,
raw samples and census remain. No failed sample is hidden by later adoption.

A fresh unchanged Sonic1 BATCHED-default build on36 is neutral/inconclusive,
with CPU reductions-3.09/0.00% and cycles-2.46/-3.25%, same-arm CPU drift4.12%.
Its inherited template description mistakenly says FUSED; actual cache, PE and
runtime identity say BATCHED. Raw records remain; interpretation corrects it.

## Final actual default builds and measured costs

Both final50b0854 supported consumer builds use BATCHED with no plane override.
RKA is a fresh cache adopting the new shared default; Sonic1 retains its
existing title BATCHED default. Frozen consumers: RKAde902a0a/UI faa3330f,
Sonic1 mechanical6ecb59e/UI47005fb. Reference PEs are accepted E10 production
96be781; base4166e74 differs only in documentation. Shared coref7dd616 and
Z804dfc5fc remain unchanged. MSVC Release MT, netplay on, default interpreter
Z80, trace/dev/reverse off; full configure and source/binary identities retained.

Native/off RKA6000frames and Sonic12400frames use accepted input routes,
full FM/PSG/WAV, logicalCPU14, below-normal priority, isolated profiles,
SDL dummy video/audio and software output. Each BAAB pair stays on the same
physical local host/configuration with measured headroom and a capacity lease.
This measures built process cost, not physical GPU/upload or universal FPS.

| Game | CPU seconds in execution order | Paired CPU / cycles reductions | AA CPU / cycles drift | Interpretation |
| --- | --- | --- | --- | --- |
| rka | B4.000000, A5.703125, A5.718750, B3.984375 | 29.86/30.33% / 29.21/30.09% | 0.27/0.65% | Positive on this matched native route; existing BATCHED default adoption, not new fusion |
| sonic1 | B1.437500, A1.468750, A1.468750, B1.468750 | 2.13/0.00% / -1.95/-0.30% | 0.00/-0.19% | Neutral/inconclusive within shared-host measurement variation; no gain claimed |

All four final arms per game match machine/audio state, complete WAV bytes,
frame hashes, final PNG, rollback and dispatch diagnostics exactly. One existing
16:9 canonical route per game also matches, and each candidate restores its own
current-build state, preserves saved RAM, makes progress after load and matches
accepted lifecycle pixels. Native saves/user settings are isolated and untouched.
PE bytes/build costs, settings, input hashes, host telemetry and every raw sample
are in [machine results](engine-raster-fusion-results.json). No new CD gain claimed.

## Reproducible regression qualification

Three targets pass on MSVC x64, actual MSVC Win32/x86 and GCC Linux: SCALAR4166
versus BATCHED final renderer; portable BATCHED with both native kernels disabled;
and the unchanged independent12000case composition oracle. The renderer test
retains4000random/direct edge/geometry/import/reset/live-write/status/deferred
cases, plus real host/private/extended providers, noncanonical flag bytes2/128/255
and self-unregistering host callbacks. Guard bytes and full VDP state are checked.

Run explicitly against BATCHED; the script's implicit C default is SCALAR:

```sh
python3 tests/runtime/run_vdp_render_diff.py --gcc gcc --git git --baseline-ref 4166e74 --candidate-define GENESIS_BATCHED_PLANES=1
python3 tests/runtime/run_vdp_render_diff.py --gcc gcc --git git --baseline-ref 4166e74 --candidate-define GENESIS_BATCHED_PLANES=1 --candidate-define GENESIS_NATIVE_COMPOSE=0 --candidate-define GENESIS_NATIVE_TILE_ROWS=0
```

For Windows, specify the intended compiler and Git executable paths. The
retained CMake task harness provides equivalent MSVC x64/Win32 targets and a
frozen4166baseline source, avoiding WSL Git's inability to resolve a Windows
worktree metadata path. Full CTest logs/XML/configurations are retained outside
build trees. Historical fused-*.log/xml filenames contain the final BATCHED tests.
Initial BOM compile-string and task-harness root-path errors happened before
game launch/compilation; original errors and corrected runs remain documented.

Evidence roots: `C:/Temp/Codex/genesis-engine-fused-raster-20261029` and
`G:/Temp/Codex/genesis-engine-fused-raster-20261029`. Retain frozen PEs, source
prototypes/census hooks, full WAV/screenshots/states, inputs, configs and verifiers.
Disposable CMake/WSL/temporary trees are removed only after final review;
borrowed dependency/UI junctions and private fixtures remain for serial work.
