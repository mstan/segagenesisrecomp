# E31: engine snapshot representation and identity

The bounded original opportunity is **rejected after implementation and built-game evaluation**. No E31 runtime change is retained. Engine code is restored byte-for-byte to `71b19b079982d400594c0dc561f9cea56c257c65` (qualified E30). This does not claim that every future algorithm is impossible. Owner combined build/play/merge/release handoff remains open.

Owner: `beads-3z5k.27`, parent `beads-3z5k`, campaign `beads-2wy8.1`. Isolated work/evidence: `C:/Temp/Codex/genesis-engine-snapshot-identity-20261101`. Machine results: [engine-snapshot-representation-results.json](engine-snapshot-representation-results.json).

## Evaluated decisions

| Opportunity | Implementation and result |
| --- | --- |
| Authoritative versus derived state | Audited the actual ten sections and sizes. RKA/Sonic1 snapshots are roughly155KiB; RAM65536B and machine90600B dominate. GVDP contains required64KiBVRAM; bus RAM/SRAM and pending chip/queue state are authoritative. The ARGB palette cache is already outside the snapshot and rebuilt, host pointers are scrubbed, and the event queue is already compact. No independent large derived image was found to remove. The unused2MiB audio-file image is not this rewind carrier. Title execution-payload reduction remains separate. |
| Exact section-only identity | Implemented independent section serialization, skipped unused execution-image serialization, retained the old whole-image oracle. Correctness passed. RKA adverse runtime repeated in reverse order despite cheaper digest; rejected. |
| Exact incremental prefix identity | Implemented exact4KiB comparison and cached streamingXXH64 prefixes, covering direct/native/DMA/import/restore changes without incomplete bus-only dirty hooks. Correctness passed, but digest median rose16?22us in RKA and16?19us in Sonic1; rejected. Existing one-shotXXH64 remains and is not new work. |
| Dirty pages, deltas and checkpoints | Implemented actual local rewind storage, not just a compression-size estimate. Exact completed snapshot comparisons cover every writer; only changed4KiBRAM/machine pages are stored against an immutable independently owned checkpoint, with full other sections and raw fallback. Implemented refcounts, rollover, evicted-base restoration, future-history drop/branching, transactional failure ownership and reusable capture scratch. Then compacted checkpoints to only referenced sections and added lazy first-use copying, size-query admission and uneconomic raw fallback. Final built-game RKA CPU/cycles repeatedly worsened; rejected. |
| Serializer/pool/thumbnail work | Original E32 remains open. The E31 prototype's capture scratch does not close that family, and a shared transactional capture API already exists in another unmerged PR. |

## Built-game result controlling rejection

Both actual native consumers use the same matched inputs, software SDL presentation, full audio emulation with dummy output, two measured physical cores, uncapped runtime, shadow off, no visible windows and isolated disposable profiles. Each pair stays on one host/configuration. Final source/binary/ROM/input hashes, commands, telemetry and raw per-game samples are retained in the machine results and original JSON files. No aggregate offset or universal FPS claim is made.

Final production timing disarms the diagnostic mutation selftest; those controls pass separately in correctness runs. `native-qualification-rewind-r5.json` runs B?A; `r6` runs A?B. Positive percentages mean more cost.

| Game | Pair | CPU | Cycles | Wall | Peak commit A?B |
| --- | --- | ---: | ---: | ---: | ---: |
| Rocket Knight,6000frames | BA | +1.128% | +1.610% | +1.577% | 75,747,328?71,475,200B |
| Rocket Knight,6000frames | AB | +2.256% | +2.040% | +2.331% | 75,984,896?71,487,488B |
| Sonic1,2400frames | BA | +2.105% | -2.204% | -2.465% | 76,066,816?70,209,536B |
| Sonic1,2400frames | AB | -3.000% | +1.149% | +1.231% | 76,210,176?70,230,016B |

Sonic1 timing is neutral/inconclusive. Rocket Knight's repeat adverse signal disqualifies the package under the per-game acceptance rule; its4.27/4.50MB peak-commit saving does not offset slower execution. The initial rotating-checkpoint codec trial stored46,410,451B rather than158,234,895B across999RKA probe snapshots (70.67% smaller), but that mechanism estimate was never treated as qualification of actual rewind ownership/runtime.

## Correctness and limits

Final compact/admission implementation passes actual normal and rewind-branch comparisons (`native-qualification-rewind-r4.json`, `native-qualification-rewind-branch-r4.json`). Guest/audio identities, periodic frame hashes, raw PNG, full WAV and rollback diagnostics match the frozen original. RKA restores oldest/middle/newest history, including a delta whose checkpoint's original ring entry has been evicted; Sonic1 restores its oldest history and resumes. Actual codec selftests cover unchanged state, every page boundary/partial last page, dense full fallback, compact checkpoint,1MiB nonpaged growth, malformed bounds and partition mutation/load/restoration. Existing native-save/file formats and wire identity were unchanged.

Independent controlled-model ownership validation passes31scenarios/27,440checks with zero live allocations. It includes full/raw/packed ownership, lazy bases, scratch reuse, allocation/save/store/load failures, branch/refill and clear/shutdown. It proves lifetimes, not the production codec. Its512KiB large-nonpaged and high-change steady cases retain exactly4,194,304B of state, equal to raw storage, with no optimization scratch/checkpoints. A delta?raw transition still peaks76,168B against72,000B raw state (+5.79%) while the last old delta retains a base and decoder; this caveat is preserved. The fixture lacks a separate first-use-checkpoint store-refusal control. No further candidate work is justified after the built-runtime rejection.

Rejected actual games compiled withMSVC19.44; the ownership model compiled and ran with WindowsGCC15.2. An initial targeted MSVC unit command built both fixtures but then requested a nonexistent `rb_digest_test` target. It is a setup error, neither a candidate correctness failure nor a portable pass. Planned additional Linux/CTest qualification was stopped after the Windows performance rejection; there is no Linux candidate runtime claim. The restored qualified baseline preserves supported portability. No new runtime PR is warranted.

## Retained evidence

Final rejected code: `rejected-delta/` and `rejected-delta-source.json`; exact final binaries: `native-binaries-rewind-r3/` with `native-builds-rewind-r3.json`. Earlier independent identity prototypes: `section-only-rb_state.c`, `rejected-prefix/` and their build/sample records. The private ownership report is `ownership-review/VALIDATION.md`. Raw screenshots/WAVs/logs and matched source/binary identities remain; redundant per-run executable/ROM copies are disposable. These records support bounded rejection, not shipping the prototype.
