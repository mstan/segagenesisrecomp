# E36 native whole-program optimization

Status: qualification in progress; [draft PR61](https://github.com/mstan/segagenesisrecomp/pull/61). E36 remains open until the actual CD and final portable gates complete. Owner `beads-3z5k.31`, campaign `beads-2wy8.1`.

## Implemented contract

Native game executables use capability-checked IPO in Release, RelWithDebInfo and MinSizeRel. `GENESIS_NATIVE_IPO=AUTO` retains ordinary compilation when unsupported; explicit ON reports a configuration error. Generators, tooling and Debug compilation are unchanged. Precise arithmetic and the required CPU ISA are unchanged.

MSVC PGO has explicit OFF (default), GENERATE and USE phases. `GENESIS_NATIVE_PGO_TARGETS` selects final-link targets; participating static catalogs keep their missing-profile guard. Profiles are per target in `GENESIS_NATIVE_PGO_DIR`. GENERATE uses exact instrumentation, training writes PGCs beside the executed binary, and pgomgr merges those counts into the PGD. USE links an immutable content-named owned PGD copy; changed training changes its link argument. Missing input and missing module data fail instead of silently reverting to untrained PGO. Profile data and instrumented binaries are build inputs, not release files.

A consumer can set target property `GENESIS_NATIVE_IPO_DISABLED=ON` to keep a measured ordinary-compilation selection when other targets are optimized. It explicitly skips both IPO and PGO. The supported S3-family MSVC build now sets it for Sonic3/SonicAndKnuckles; fresh actual projects prove those flags stay absent while S3K retains the qualified workflow. This durable adoption policy is [consumer PR16](https://github.com/mstan/Sonic3AndKnucklesRecomp/pull/16); existing binaries retain their original identities.

The standalone CD app, native catalog, device and disc-image targets share this interface. Its reference can reuse the qualified normal archive; its IPO candidate must rebuild all10,957 native sources as IR. Reusing that ordinary archive would not qualify whole-program CD optimization.

Portable LTO exposed a pre-existing LP64 declaration mismatch: the scheduler declared a uint32_t CPU chunk budget, while glue defined uint_fast32_t. The implementation now shares the fixed-width declaration through genesis_machine.h. Windows has the same effective width before and after this correction.

## Actual production cartridge results

All six6000-frame comparisons use `GEN_ENABLE_TRACE=OFF`, frozen matching ROM/input/host features and two measured physical cores fixed within each pair. Default and all available production captures are separate. Native state, audio state, every60th framebuffer hash, raw PNG, full WAV where enabled, rollback and every enabled capture file match; Sonic3K reaches real periodic16KiB SRAM commits. WRAM write tracing is unavailable in this production configuration and is not claimed.

Shared-host observations below are not isolated FPS estimates. A smaller negative percentage is a lower candidate cost. In particular Sonic3K default CPU improves while cycles/wall move adversely: neutral/inconclusive, not a proven gain or regression.

| Game / mode | CPU change | Cycles change | Wall change |
| --- | ---: | ---: | ---: |
| rka / default | -10.83% | -11.14% | -9.50% |
| rka / capture | -1.76% | -5.53% | -0.88% |
| sonic1 / default | -2.19% | -2.81% | -0.73% |
| sonic1 / capture | -6.01% | -5.95% | -6.22% |
| sonic3k / default | -4.04% | +2.59% | +3.25% |
| sonic3k / capture | -7.98% | -8.20% | -7.42% |

Training costs are separate from final uninstrumented runtime. Sonic1 trains on a separate12,000-frame title/attract/demo route; RKA and Sonic3K train on3,000-frame native routes. Actual MSVC use logs report0 invalid callsites and profiles covering99.1%,94.6% and98.6% of compiled functions respectively. The reported100% instruction counts mean training-observed dynamic instructions, not unseen-gameplay coverage.

| Game | Baseline PE bytes | Normal-PGO PE bytes | Instrumented training wall seconds |
| --- | ---: | ---: | ---: |
| rka | 8,813,568 | 10,407,424 | 272.4 |
| sonic1 | 5,448,192 | 5,411,840 | 479.9 |
| sonic3k | 35,600,896 | 37,484,032 | 262.7 |

The optimized-config and final-target policy corrections are CMake changes with unchanged selected Release runtime source. Exact original build/training heads are retained per stage rather than relabeling binaries as the later reviewed head. The LP64 ABI correction is qualified separately on Linux and in the final combined title builds.

## Layout and kernels

Ordinary IPO and trained normal MSVC layout are implemented and evaluated. An additional linker-only AGGRESSIVE profile variant uses the same merged training data. Its direct comparison reduces default CPU4.26% but increases capture CPU21.13% and cycles17.41%. This inconsistent package is excluded from the public defaults; normal PGO is retained. No universal slowdown is inferred from one ambient pair.

Existing qualified BATCHED plane, FM source-span and checksum kernels remain in use with their portable contracts. This run adds no new ISA-specific kernel and counts no earlier kernel gain again. Earlier generic CD catalog canonicalization remains separately qualified; its footprint savings are not attributed to LTO/PGO.

## Portable and final combined qualification

The corrected Linux947 candidate matches complete native/audio state, all40 frame hashes, full35,827,244-byteWAV and rawPNG. In the bounded matched AB/BA repeat, CPU changes are-8.30/-14.38percent with identical CPU1/nice10/environment. These are shared-host observations, not isolated effects. The ordinary baseline consistently has18 rollbackpasses/1event-queue divergence; candidate18/0. This is a baseline-only observed failure, not an invented equal failure or a claim that IPO fixes that defect. Earlier pre-fixB timing omitted nice10 and is invalid for matched performance; all raw artifacts remain. Linux cycle accounting is unsupported/zero.

Three actual final MSVC projects verify Debug has no IPO/PGO instrumentation flags while all three optimized configurations do. At final integration0b29685, all12 requested existing strict Release tests pass, including real diagnostic/UI providers, persistence callbacks, native machine/catalog and HD restore. The earlier strict constant-condition and stale renderer-only provider expectation were fixed without suppressing warnings or dropping diagnostics. No actual game/shard matrix was added for this test gate.

The final current-consumer suite preserves exact title/UI/core/ROM identities within each pair. It uses engine8283a40 (currentmaster plus accepted prior contracts) as ordinary reference and frozen103dfea for IPO/PGO+HD candidates. RKA's unchanged title uses its frozen accepted E35 reference. Current S1/S2/S3-family heads are264f921/fcc85ef/5303dfe; older campaign fixtures above are not relabeled as these consumers.

Current S1, S2, RKA and S3K retain qualified IPO+freshPGO. S1's initially adverse capture timing changes sign in its sole reversal, with48percent baseline drift; classify it as neutral/inconclusive. Unprofiled IPO is rejected for Sonic3 and Sonic&Knuckles after both timing orders retain adverse costs. Their delivered packages use the existing IPO_OFF/PGO_OFF interface in a separate ordinary-compilation cache. OFF CPU signs are mixed under variable load; adverse cycle and wall observations remain explicit, with no speedup or zero-overhead claim. No gain in another title offsets these observations.

Current RKA/S1 each pass an actual full5120x3584 readback and native/current-file or rollback checks. Current S3K produces2 real periodic SRAM P acknowledgments and1 startup-context A attachment; supported playback matches all6000 executions/final native+audio state and writes no native SRAM. Original private progress hash and mtime remain unchanged.

## CD build cost and remaining gate

Actual Release commands prove all10,957 native catalog sources,9 device sources,1 disc source and248 app sources use /GL and the no-data guard. SERVER's16GB host reaches C1002 in instrumented code generation. The exact already-compiled inputs and19.41 toolchain move to the owner PC without recompiling the catalog. Code generation completes there with observed peak working set16,678,633,472bytes/private commit15,477,526,528bytes, but a missing SDK resource compiler causes LNK1158 before any executable is created. That is an explicitly retained setup failure and build-memory cost, not game memory or a runtime result.

The exact19041 resource/manifest tools and their dependencies are now validated with real RC/manifest preflights. To avoid another redundant component-only pass, remaining CD qualification uses the combined103dfea E02/E36/E37 batch with the same10957-source IR archive, fresh final-program training and normal USE. This is authorized combined-PR validation, not an isolated LTO attribution. Source/operation/context ABI identities and source/binary/profile/SDK hashes remain explicit. No CD runtime gain or family completion is claimed before that final gate.

## Evidence and remaining gate

[Machine-readable results](engine-whole-program-results.json) retain per-stage source/binary/profile identities, commands, settings, per-game raw samples and startup/memory/training costs. Local raw root: C:/Temp/Codex/genesis-engine-whole-program-20261101. CD compiler inputs: G:/Temp/Codex/genesis-engine-whole-program-20261101-server. Final combined sources/artifacts: C:/Temp/Codex/genesis-engine-final-integration-20261101. Final cartridge raw evidence and selected OFF siblings: C:/Temp/Codex/genesis-engine-final-cartridge-qa-20261101.

Pending: actual full-IR final CD executable, fresh training/mergedUSE and canonical/GPU comparison; independent E36 whole-family disposition. Owner final play/merge/release handoff remains. No quiet-host wait or additional broad benchmark matrix is required.
