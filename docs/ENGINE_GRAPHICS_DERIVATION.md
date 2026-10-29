# E13 complete graphics derivation evaluation

Owner `beads-3z5k.10`, parent `beads-3z5k`, campaign `beads-2wy8.1`.
Base `0c78fc6` (compiled `50b0854` best-existing BATCHED), whole prototype
`1d0aab9`, retained palette-only production `999f392`.
Engine [PR45](https://github.com/mstan/segagenesisrecomp/pull/45) stacks onPR44.
Independent final whole-scope gate ACCEPTED production999f392 / readyPR45
reviewed5576577. Full bounded architecture rejected, useful pureDAC retained;
21originalfamilies remain open. Reviewer explicitly instructed fresh serial E14
after status/sync/cleanup.
No merge/release/owner-launch approval.

The complete art, palette and geometry reuse architecture was implemented,
qualified and reached by actual games. It produced no demonstrated benefit over
the existing packed renderer and row constants, so its mutable caches are
rejected. This is a bounded whole-family evaluation, not a partial row-cache
implementation or a deferred ownership prerequisite. It does not claim every
possible future cache design is exhausted.

The smaller retained package replaces repeated generic DAC channel derivation
with an immutable complete color domain. All512colors at normal/shadow/highlight
levels occupy6KiB of constants; current CRAM bits and shade select exact ARGB.
Ignored CRAM bits and unknown shade values preserve the old scalar contract.
There is no initialization, allocation, invalidation, machine identity, mutable
cache, new ISA or platform requirement. No title renderer/logic was changed.
Reduced arithmetic is not a measured speedup claim.

## Complete original-family inventory

| Opportunity / contract | Evaluation and final disposition |
| --- | --- |
| Decoded native/private art | Implemented512-slot/12KiB bounded cache of full current four-byte pattern values, both flips and current palette/priority applied separately. Address selects placement only; explicit valid handles zero/collisions. Used by native planes and sprites, including private providers. Actual high hit rates but no demonstrated runtime benefit; REJECTED and removed. Original packed decoding restored byte-for-byte. |
| Palettes and nonlinear DAC | Implemented complete immutable512x3 result domain. It receives the same current CRAM word/shade as the old function, so raw/direct/restored values need no notification. Existing cartridge callback/import palette ownership remains unchanged. Generic/captured CD rows and existing per-pixel/row palette consumers adopt the common helper mechanically. Retained separately from rejected mutable caches; final game qualification below. |
| Register-derived geometry | Implemented complete9-register tuple for plane/window bases, sizes, scroll mode/base and window positions. Per-row lines, live VRAM scroll/VSRAM, backdrop, provider decisions and status retain original sampling. High real hits but no demonstrated gain versus already-cheap once-per-row derivation; REJECTED and removed. |
| Direct/public/port/DMA/import writers, aliases and two VDPs | Full-value architecture reads complete inputs each use; direct odd/wrapped VRAM, DMA/import, same-address replacement, restore and interleaved machines cannot leave stale derived values. Pattern slot/address/pointer is never identity. These are evaluated correctness contracts, not missing future barriers. |
| Host/private/extended providers and callback timing | Actual private SAT pieces/banks, provider replacement and self-unregistering host callback covered in full differential suite. Current pattern bytes precede private color lookup, geometry locals precede opaque host callback, extended colors keep scalar full indices; no provider pointer/eligibility cached. Final VDP source restored exactly to accepted50, so it adds no provider behavior. |
| Mutable write epochs / broader ownership alternative | Port-only invalidation is unsound under existing public/direct/import/alias/private producers. Complete barriers/provenance or exact readback would be required. The implemented complete-value design already pays the necessary readback and measures high hits with no gain; additional mutable tracking is rejected for these workloads. No prerequisite deferral or claim all future ownership algorithms exhausted. |
| Full tile/row/image reuse and title data caches | Whole-pattern blocks would compare/read bytes not consumed by current row; required name/scroll inputs stay live. Packed span/tile decoding already exists. E15 full hardware-result reuse, GPU/workers and title assets/objects/layouts are separate original goals and are not counted as delivered here. |
| Immutable deferred rows, status, reset and current-build state | Complete raw state/pixels/guarded bounds/status/import/live-write/deferred/callback contracts pass, as do built cartridge rollback/current-build restore and CD checkpoints/full WAV. Derived prototype cache data never entered guest snapshots; final production contains no mutable derived cache. No backward save-state compatibility work. |
| Portability / host and netplay identity | Portable C integer/table code only. Actual Win32/x86 plus MSVCx64/GCC qualify all colors, including ignored bits and unknown levels. Final retained function/data identical to that qualified source; VDP source identical to accepted base. No simulation/profile/backend/capability negotiation or dependency changes. |

## Whole architecture actually exercised, then rejected

A task-only clone adds counters to the exact1dVDP/header without changing current
input reads, calculations or decisions. It is compiled through supported RKA/S1
builds with the original VDP source replaced exactly once. Clone/header/CMake
hook hashes, build settings, PEs and complete raw correctness remain. Counter
costs are excluded from performance evidence; no counters are shipped.

| Game | Pattern requests / hits | Geometry requests / hits |
| --- | --- | --- |
| RKA | 137,823,264 /122,652,221 | 1,342,248 /1,342,241 |
| Sonic1 | 53,087,155 /46,546,834 | 518,181 /518,179 |

Those diagnostic runs exactly match clean1d state, audio state/full WAV, frame
hashes, PNG and rollback. Unit hits alone are not the reachability evidence.
The real game paths used the caches heavily, and useful throughput did not follow.

Whole1d native BAAB RKA CPU B4.078125,A3.984375,A3.953125,B3.968750 shows possible
marginal overhead: CPU reductions-2.35/-0.40%, cycles-2.76/-0.90%, candidate drift
2.68%CPU/2.14%cycles. The independent reviewer required one opposite-order pair:
A4.156250/B4.156250, cycles19,619,206,240/19,623,009,480 (-0.0194% reduction).
That does not reproduce meaningful overhead, and it establishes no gain.
All six arms remain; no averaging hides a consistent loss.

Sonic1 native and both custom16:9 pairs are small/mixed and neutral/inconclusive.
CD whole1d BAAB CPU reductions+4.38/-4.20%, cycles+2.69/+0.83%, with AA drift
-2.19%CPU/+3.47%cycles, are also neutral/inconclusive. Every initial native/custom/
current-state/CD checkpoint/RAM/image/full-WAV gate passes. This rejects the extra
mutable bookkeeping for lack of demonstrated benefit, not for a correctness failure.
No further mutable-cache tuning or benchmark matrix was pursued.

## Final palette-only package and built-game results

The retained source restores `genesis_vdp.c` and renderer differential test
byte-for-byte to0c/50. The failed cache header is removed. DAC function/table are
byte-for-byte identical to the fully qualified1d code, with a reproducible
generator and supported all-color regression test. Both initial architecture
and final palette-only package identities are labeled in [machine results](engine-graphics-derivation-results.json).

Final supported Release MT RKA and Sonic1 use fresh build directories and
unchanged BATCHED defaults; their configs/source lists exclude diagnostic hooks.
Frozen consumers RKAde902a0a/UI faa3330f and mechanicalSonic16ecb59e/UI47005fb
are unchanged. References are accepted50PEs. CD reference is freshly compiled
0c (same compiled50), candidate999; unchanged qualified instruction archive and
catalog manifest hashes are retained. Coref7dd616/Z804dfc5fc stay unchanged.

Native RKA6000/Sonic12400 frames and focused existing16:9 routes use the same
physical local host/configuration within each pair, logicalCPU14, below-normal,
isolated profiles, dummy SDL outputs/software and complete emulated FM/PSG/WAV.
CD uses its accepted3001frame/11checkpoint route, cost window1800..3000.
Capacity reservations, measured headroom, source/PE/config/ROM/input hashes and
raw samples are retained. These are process costs; no physical GPU/quiet-host or
universal FPS claim. Final simplified-source checks use a minimal matched AB,
with earlier whole-architecture variation retained distinctly.

| Game / feature | CPU seconds in order | CPU / cycles reduction | Interpretation |
| --- | --- | --- | --- |
| rka native | A3.984375, B3.906250 | +1.96% / +1.17% | Neutral/inconclusive; no speedup claimed |
| rka custom16_9 | A10.593750, B10.500000 | +0.88% / +0.36% | Neutral/inconclusive; no speedup claimed |
| sonic1 native | A1.437500, B1.453125 | -1.09% / -2.24% | Neutral/inconclusive; no speedup claimed |
| sonic1 custom16_9 | A2.437500, B2.437500 | +0.00% / +1.96% | Neutral/inconclusive; no speedup claimed |
| Sonic CD accepted route | A5.734375, B5.687500 | +0.82% / +2.72% | One AB; neutral/inconclusive, preceding variation retained |

Final independent review requested exactly one opposite-order native Sonic1 pair
to resolve its one-CPU-quantum initial cost. The same final PEs/settings produce
B1.390625/A1.421875CPU seconds, cycles6,642,626,560/6,703,177,600: reductions
+2.198%CPU/+0.903%cycles, with exact prior-final/native correctness. The original
negative AB and this BA are both retained. The native cost is not reproduced;
this is neutral/inconclusive, with no gain or cross-feature offset claimed.

All final cartridge machine/audio state/full WAV, frame hashes, final PNG,
rollback/dispatch match. Each candidate restores its OWN current-build file,
keeps saved/loaded RAM exact, advances after load and matches accepted lifecycle
pixels. Every CD inspection, captured memory/device state, screenshot and full
WAV matches. Native saves and owner settings remain untouched. Source/PE bytes,
build/startup process costs and raw samples are retained. No earlier RKA30%
default-adoption gain is attributed to E13.

## Reproducible qualification and evidence

Prototype1d four targets pass on each MSVCx64, actual MSVCWin32/x86 and GCCLinux:
explicit50BATCHED versus candidateBATCHED and portable native-kernels-disabled
renderer, unchanged composition oracle and complete-value graphics unit. They
include4000random/fullstate/guards/provider/capture/import/reset cases, direct
VRAM/CRAM/VSRAM/register mutations/interleaved VDPs,20000colliding/restored
pattern values/both flips/current attrs, and all65536CRAMwords at6levels.
Final identical retained DAC domain is covered by those platform gates; final
supported color-only target and generator check also pass.

```sh
python tools/generate_genesis_dac_table.py --check
cmake -S tests -B build/graphics-tests
cmake --build build/graphics-tests --config Release --target graphics_derivation_test
ctest --test-dir build/graphics-tests -C Release -R '^graphics_derivation$' --output-on-failure
```

Use the normal supported generator/compiler for the host. Full CTest/XML/configs
are retained outside disposable builds. Historical fused-*.log/xml filenames
contain initial E13 graphics gates; supported-test-palette-final.log/xml is final
supported coverage. Task-only harness has explicit BATCHED flags on BOTH sides,
so it does not accidentally compare scalar versus scalar. Reproduce the rejected
whole architecture from retained Git1d/source snapshot and its exact configs;
final source intentionally excludes it.

Initial template preparation/missing-job bookkeeping failed before any builds or
game launch, then explicit scripts/jobs corrected it. No runtime failure or raw
sample was discarded. Source and evidence roots:
`C:/Temp/Codex/genesis-engine-graphics-derivation-20261029` and
`G:/Temp/Codex/genesis-engine-graphics-derivation-20261029`.
Full prototype/census/frozen PEs/WAV/screenshots/states/configs/verifiers are retained.
Audited owned build/WSL/TEMP cleanup remains pending final independent acceptance;
borrowed dependency/UI junctions and private fixtures are preserved.

Independent final review corrected stale Sonic1 diagnostic PE byte-size metadata
(5,429,760 inherited versus actual5,430,272). PE SHA and every runtime sample
were already correct. Original metadata is retained under metadata-corrections;
all embedded copies and receipt hashes are corrected, with no game rerun.
