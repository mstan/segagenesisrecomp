# E19: ordered audio timeline evaluation

The complete selected bounded opportunity is rejected. No runtime change is retained. Both owned deferred-span layouts and the already prepared simpler flat/bulk architecture were implemented, built and measured. Cartridge and CD policies remain separate where hardware clocks differ; no title-specific sound-driver semantics were added. Final restore/cleanup/Beads review is pending.

The owned cartridge queue cached sealed ordered FM/PSG intervals with lazy modulo32 origins, preserving canonical FIFO/pop/save/hash views, atomic restore validation and immutable same-drain worker input. The reassessment generalized per-part sealing and removed fresh-only copies. The independent reviewer then required evaluating the simpler original flat queue with bulk transfer and the common natural-run kernel; that alternative was also built and measured. CD used common exact clock scaling/stable ordering, lazy scratch and a deferred cursor. A missing cursor index found in source review was fixed before actual qualification, with repeated-boundary/capacity-compaction regressions.

All14 cartridge pairs match complete guest/audio state, framebuffer hashes/raw PNG, full WAV and real rollback probes. The two CD pairs match complete checkpoint/artifact/full-WAV routes against the frozen original; no CD rollback pass is claimed. Together these are16 distinct matched pairs/32 actual runs. Initial three existing component checks passed; changed mixer checks passed after each reassessment. Targeted independent FIFO/partial/wrap/capacity/restore/invalid-load/epoch tests and the existing independent qsort oracle cover logic shared by the frozen mixer wrapper. Prototype build/source/ROM/input/settings/host/core/config and raw per-game costs remain in the linked existing records. One old observer expectation failed after a successful baseline shadow run; its partial raw result is retained and not counted as a complete pair.

| Architecture / Sonic 1 shadow | CPU change | Cycles change | Wall change |
| --- | ---: | ---: | ---: |
| 9e921b6 / native-qualification-r2.json | +0.68% | +8.52% | +9.40% |
| a33431e / native-qualification-r3.json | +21.99% | +9.89% | +13.89% |
| f089961 / native-qualification-r4.json | +4.79% | +3.25% | +4.87% |
| f089961 / native-qualification-r5.json | +7.59% | +3.86% | +3.03% |

The supported Sonic1 shadow feature repeatedly regresses, including both orders of the flat implementation. RKA shadow observations improve, and default cartridge observations are mixed/inconclusive; those cannot offset the Sonic1 regression. Owned spans add about2MiB working-set/commit. The flat reassessment removes that storage but does not qualify.

| CD package / order | CPU change | Cycles change |
| --- | ---: | ---: |
| game-e19-owned-audio-r1.json | +22.75% | +25.41% |
| game-e19-owned-audio-r2-reverse.json | +5.24% | +3.18% |

CD uses the same staged14.41/SDK19041 toolchain and byte-identical common compiled objects; only native audio source/new timeline header change. The original staged CRLF text normalizes exactly to the accepted baseline, with both exact identities retained. Its1200-frame measured interval is paced at about20.3seconds, so stable wall time is not a speedup. Peak commit increases about12KiB. CPU/cycle cost is adverse in both orders. No further tuning or new harness is required for this selected scope.

Production and all build/test wiring are restored byte-exact to accepted5d7ff7a: restored commit 4bd78ef92d200e90461f323727c0ee4eb614fdc0, tree 026ed5bd73874949b3a8fd15aacd23ddee6ae48c. Pushed Git history retains all prototype sources; actual binary/configuration and raw observations remain outside delivery folders at G:\Temp\Codex\genesis-engine-audio-timeline-20261031. No PR is appropriate for discarded code. This rejects the selected current architectures, not every possible future audio algorithm. E20 faithful synthesis, later audio/host goals and E37 remain separate open work.

See engine-audio-timeline-results.json and beads-3z5k.16; campaign beads-2wy8.1. E19 remains open until the final independent restore/cleanup/records gate; no merge/release/owner launch approval.
