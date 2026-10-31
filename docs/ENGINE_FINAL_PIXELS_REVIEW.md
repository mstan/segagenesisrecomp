# E16/N20 independent final validation

**Verdict: ACCEPTED as delivery of the original bounded E16 shared final-pixel contract.** PR48 is ready for combined owner review. This is not merge, release, delivery replacement or owner-launch approval.

Reviewed head: `c20f9668214a99aa9cb38eed1d8010de19a3a211`. Production: `ea16061a702bbc0ad3ce769b880ed47cac31a888`; its machine/pixel/test source is unchanged from qualified `2981cf8`, with a later CD header ownership comment. Base: accepted E15 `c22af45`. Reviewer: `validate_e16`, validation only. No implementation, new build, test, profiling or game launch was delegated to this reviewer.

## Original scope

The complete selected contract covers indexed/ARGB conversion, copies, color processing, raw/UI staging and upload preparation across the machine and both frontends. The implementation supplies direct indexed delivery into owned raw storage, exact finite color mapping, synchronous borrowed/writable stage ownership and CD packed/borrowed upload preparation. It preserves fresh per-row palettes, raw captures, sprite/status/timing, contexts, custom/null/declined producers, overlays, interlace, filter padding/history and portable fallback. This is a coherent pipeline, not a single CD-copy or inverse-table change presented as whole completion. It does not claim every possible future fusion or backend variant is exhausted.

The inactive-storage metadata/output alias and fixture alignment findings were fixed before qualification. Native diagnostic storage dumps were correctly relabeled as upload storage/padding; active raw pixels and separate full-raw immutability proofs are distinct. No objective finding remains unresolved.

## Qualification and actual runtime

The independent frozen426 machine/color/DAC sources are byte exact. Four gates pass each on actual MSVC19.29 x64, actual Win32 and GCC13.3 x64 with ASan+UBSan. Actual eight unit PE architectures and four sanitized ELF identities/imports/flags were checked. The machine tests compare sixty meaningful synthetic route/mode cases per executable against the real frozen scheduler and production BATCHED VDP; guest glue/bus stubs are explicitly not compiled-game execution.

All eight clean matched game pairs were checked against raw identities, logs, process costs and output artifacts. Raw machine/frame hashes, PNG and full WAV remain exact; cartridge rollback and current-build own-file save/load/progression pass. Both CD implementations preserve all eleven canonical checkpoints, RAM/full WAV and execution floors. Ordinary persistence was isolated using copied ROMs/profiles.

Forty-eight actual native snapshots compare active raw pixels, full upload storage/padding and pre-Present software readback across UI, raw/CRT, filter, adaptive size and window transitions. Their task-only source clones were independently reconstructed. They use common candidate runtime with original c22main versus candidate main, backed separately by the frozen-machine/color oracles, and contribute no clean timing. Twenty-four actual CD SDL/UI cases compare original and candidate presentation source, including odd pitches, dimensions, nearest/linear, open/closed UI and source-memory immutability. The final283-entry QA retention manifest was verified.

`--measure-runtime` does execute the presentation path; only `--benchmark` skips it. The historical contrary preparation/preliminary assertion was corrected explicitly. Failed diagnostic environment/vsync observer assumptions remain recorded and excluded, rather than counted as passing transition or performance evidence.

## Per-game performance judgment

Color-enabled RKA and Sonic1 full-runtime workloads show substantial observed improvements: CPU reductions56.48% and66.26% respectively, with corresponding large cycle reductions. These are matched software-SDL/shared-host observations, not physical GPU, quiet-host, playable-FPS or loading claims. Default/raw results are conservatively neutral/inconclusive; no proven default gain is claimed.

Sonic CD is a **mixed neutral/inconclusive disposition**. The initial BA and reverse AB remain genuine, substantial adverse observations: CPU increased9.88% and18.31%, with cycles also adverse. They are not invalidated by rebuilding. Corresponding old/fresh `.text` sections are byte exact. A later pair on the same fixed logicalCPU14 reversed direction (CPU5.578125 to5.25seconds); focused physical-core BA was near neutral (5.546875 to5.5seconds). That last pair uses the same measured physical core including its two SMT threads in both arms, a distinct matched configuration rather than pooled data.

The earlier host headroom samples show42–59% ambient CPU usage versus9–20% in the later measurements. Ambient variation is therefore plausible, but its precise cause is not established. The retained mixed evidence supports this bounded neutral/inconclusive disposition; it does not prove universal absence of a CD regression or a CD speedup. Cartridge gains do not offset CD observations. No further benchmark matrix is necessary to state this limited conclusion honestly.

## PR, records and cleanup

[PR48](https://github.com/mstan/segagenesisrecomp/pull/48) is open, ready and stacked onPR47 at the reviewed head. The claim/hierarchy/external reference and tested/corrective milestones are durably recorded on `beads-3z5k.13`, parent `beads-3z5k` and campaign `beads-2wy8.1`. Counts intentionally remain19 until this verdict is recorded. The failed repository assignment attempt is documented and is not an unresolved source/protocol finding.

Audited cleanup removed3,995,547bytes of WSL unit intermediates and2,875,004bytes of SERVER unit intermediates after actual ELF/PE/config/log retention. Those retained artifacts were independently checked. Bounded supported build/common objects, qualified private media/archive/tools, failed/final QA and raw evidence remain for combined review/campaign reuse, with cleanup needs recorded. The task bulk inventory is868files/2,259,440,616bytes, with no symlinks/junctions/nested repositories. Preserve borrowed source/dependency worktrees, owner saves/settings and requested evidence during eventual cleanup.

The immutable machine receipt records the full artifact counts and identities, per-game samples, resolved findings and scope limits. Earlier independent milestone receipts remain unchanged; subsequent rechecks must use separate output.

**Root instruction:** record E16 as accepted `bounded_delivered`, update the remaining-family count19to18, attach this verdict and immutable receipt to ready PR48 and owning/parent/campaign records, finish bounded evidence-preserving cleanup/retention disposition and central synchronization, then proceed to original **E17 with a NEW validation-only reviewer**. Do not reopen accepted earlier gameplay checks or relaunch cleared owner playtests. Combined owner visual/play approval is still required before merge/release.

Immutable receipt: `VALIDATOR_E16_FINAL_AUDIT.json`.
