# E20: faithful FM operator/channel kernels

The selected remaining FM kernel opportunity is rejected. No E20 runtime,
compiler-profile selection, test fixture or build wiring ships. Accepted source
10ace05 is restored exactly by02c0fa8d32d1099f8b2d1728301e4b8cbc6a3b92;
both complete trees are d5ae13ffcc16f6fb652c1343bb3ffbcc0acfb9f0 before
this evidence-only update. The already-delivered sample-block wrapper is unchanged.
Owning beads-3z5k.18, parent beads-3z5k, campaign beads-2wy8.1.

Two material implementations were built and evaluated in isolated worktrees:

- 5e420634e8ce69cc1becd9e5461425dd4ef682f5 replaces generic operator routing
  with eight fixed OPN2 routes, retaining int16 intermediates, per-add clipping,
  stereo/feedback behavior, plus one pass over channel outputs.
- 4adfc3842fe0749e914c0f429cd75c7164675326 removes the expanded routes and
  uses the original generic 4op arithmetic inside the compact single channel scan.

Both retain all clocks, envelopes, phase/LFO, DAC discontinuity for zero/masked
channels, write behavior and serialized state. Native and compile0 fallback were
checked against the entire independently frozen vendor and wrapper in a separate
namespace, not two wrappers sharing the changed kernel. Each source passes both
checks: arbitrary writes, algorithms/feedback/pan, DAC/LFO/SSG-mode register cases,
status/PCM/state/filter/ring/remainder/wrap/restore and long unobserved7000-sample
spans across the vendor preparation boundary. The deterministic SSG-mode sweep
is not claimed to exhaust hold/invert/restart transitions; their math is unchanged.
All13 frozen reference files match Git10ace05 and their manifest. Prototype
source/test files remain in pushed Git history; they are removed from production.

## Actual built-game decisions

There are20 distinct matched pairs/40 completed runs:10 cartridge pairs for the
fixed routes,8 for compact generic routing, and2 CD pairs for compact routing.
Every cartridge arm matches full WAV, raw image, frame hashes, guest/audio state,
sample counts and real current-build rollback. Both CD orders match11 checkpoints,
21 captured artifact hashes and the complete44,787,108-byte WAV. CD rollback is
not claimed. Muted/hidden isolated QA preserves normal desktop focus and native
owner saves/settings. No final owner launch, merge or release was performed.

| Package/workload | Observed candidate effect | Decision |
| --- | --- | --- |
| Fixed routes, RKA shadow off | Wall+10.74/+3.96/+6.64%; CPU/cycles mixed | No qualified per-game-neutral default; no universal arithmetic slowdown claim |
| Fixed routes, Sonic1 shadow off | CPU-8.13/-9.30%, cycles-6.58/-4.19% | Useful observations cannot offset other configurations |
| Fixed routes, shadow on | RKA mixed; Sonic1 wall adverse with mixed CPU/cycles | No global adoption |
| Compact, Sonic1 shadow off | Cycles+6.97/+4.54%, wall+14.30/+5.61% | Reproducibly adverse; reject global adoption |
| Compact, Sonic1 shadow on | CPU-6.49/-4.97%, cycles-7.58/-9.70%, wall-11.45/-14.67% | Does not offset its default-path regression |
| Compact, RKA both modes | Shared-host mixed cost observations | No stable separate win claimed |
| Compact, Sonic CD AB | CPU-8.33%, cycles-7.475% | First pair only |
| Compact, Sonic CD BA | CPU-0.773%, cycles+7.111% | Overall neutral/inconclusive; no proven8% CD gain |

The CD baseline varies about19% in CPU and22% in cycles between the two pairs.
Same-affinity/host/configuration within each pair and complete raw samples are
retained. The1200-frame cost window is paced at about20seconds; stable delivery
wall time is not an uncapped FPS improvement. A proposed CD-only compiler/backend
selection was explicitly rejected because this evidence does not establish a
retained benefit worth that additional selection. Neutral outcomes are allowed;
the rejection does not impose a requirement that every game improve.

Cartridge baseline fa696e2 and both candidates use MSVC19.44.35229.0/compiler
14.44.35207 with the frozen representative consumer/UI/dependency inputs.
The earlier E18 narrative saying19.41 for these native games was a metadata
error: the original first configure log and E20 log both identify19.44. CD
separately uses the same staged MSVC19.41.34123.0 x64/SDK19041 in both arms.
All five candidate FM translation units and their full13-file header/wrapper graph
were frozen consistently; no mixed old/new class definitions were used.

## Disposition and retained evidence

After two material architectures, independent focus review instructed finishing
the prepared CD comparison and making a decision, rather than more repeats,
instrumentation, speculative feature rollout or a third tuning variant. The
whole selected bounded E20 opportunity is an evidence-backed unsuccessful result,
not a deferral or a proof that every future FM algorithm is impossible. No new PR
is appropriate because no useful production change remains. Existing qualified
engine PRs and accepted releases remain unchanged.

Exact identities, per-game raw costs, build commands/configuration, unit binaries,
profiles, full captures and samples are in G:/Temp/Codex/genesis-engine-fm-kernels-20261031.
The local four disposable object/build directories were removed only after copying
and verifying matching passing unit binaries/config; native and CD binaries remain
with their manifests. Borrowed consumers, toolchains, SDKs and shared CD cache are
preserved for the continuing campaign. The CD cache is restored to its original
qualified source selection. See engine-fm-kernel-results.json for per-game results,
cleanup status and final independent gate. No adoption-only extra-platform/stress
matrix was added for discarded code. Original code/build/test restoration is exact.

Independent final gate accepted; owning issueclosed and central push12.4seconds
exit0. See ENGINE_FM_KERNELS_REVIEW.md and engine-fm-kernel-review.json. The
validator explicitly instructed original E21 next after this evidence-only push.
