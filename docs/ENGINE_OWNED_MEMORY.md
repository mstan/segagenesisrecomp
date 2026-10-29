# E03 owned data regions and bus transactions

E03 implements generic data-memory ownership for qualified generated operations. Runtime performance is **neutral/inconclusive** on the matched Sonic CD route; no speedup is claimed. Four full-game arms preserve state, graphics and audio exactly. Independent review accepted that neutral classification; current-build restore passes on MSVC/GCC. The independent final gate is accepted: [core PR #8](https://github.com/mstan/m68k-recomp-core/pull/8), [Genesis PR #37](https://github.com/mstan/segagenesisrecomp/pull/37).

Owner `beads-3z5k.2`, shared core `beads-rxq.4`, parent `beads-3z5k`, campaign `beads-2wy8.1`. Built engine `899939b0b0119eb1065bfdc4ae7d95c59a288537`, core `b2d95f2cbe81fd9eee03a00bc2f779e6d1253edd`, consumer `94f57796db9de4990e23476b576b29239095fb22`. Baseline is E01 engine `aec3c9c`/core `34e5aee`; this is an additional E03 package, not a comparison to the old release. Later API-comment/test/evidence commits are distinct from these built runtime identities.

## Ownership and exits

A portable optional runtime resolver yields complete guest-byte memory regions with separate read/write permissions. Independently qualified context-bus operations can reuse them within one instruction, including longword and MOVEM accesses. Views never cross an operation or trace boundary. Every bus/fetch/WAIT/reset/ack callback suspends reuse for its entire dynamic extent, including nested helpers, then discards all views. Fault unwinding also ends ownership. Ordinary HLE/live and unqualified operations retain the universal bus path; opcode and extension guards always compare current bytes.

The shared CD provider resolves BIOS split around the HINT-vector overlay, banked PRG ownership, Sub PRG protection and owned2M Word RAM. Main-host RAM publishes its normal bus timestamp at the first resolution in the uninterrupted instruction. Broad host regions are clipped so they cannot acquire CD memory/MMIO apertures. Ignored/protected writes, MMIO, unavailable ownership and noncontiguous1M data retain the faithful bus path. There is no second guest-memory representation, cached instruction identity, game-PC recognition or title algorithm.

The lane-aware CD word bus now resolves a complete transaction once: plain RAM/ROM, backup/PCM lanes,2M ownership,1M linear/cell views and packed dot priorities. MMIO still calls both register lanes in order and preserves word-only CDC/stopwatch/stamp behavior, failed-read outputs and partial effects. The cartridge bus already decodes complete words directly; that source was audited, needs no duplicate implementation and has no new gain claimed here.

Only the independently audited generated context-bus effect grammar/flags2 is reused from E02. Its rejected page-version/code-identity runtime is excluded. REGISTER is tested as a bit; TRACKED_BUS does not authorize register-only execution. Operation/descriptor ABI prefix remains compatible with frozen generated objects. The code is portable C with no new ISA or compiler dependency.

## Validation and findings

22 MSVC and20 GCC gates pass. The core oracle has320 matched CPU/full-memory cases; the word bus has82,944 full-device-state cases across all32mode/priority values, controls, banks, both CPUs and boundaries. Six broad-host aperture cases protect ownership/MMIO. A separate MSVC/GCC current-build restore assertion replaces CPU state and banked guest bytes between operations, then requires fresh resolution and exact ordinary-path CPU/memory equality. Logs and the successful capacity receipt are retained; no old state migration is required.

Independent review found two actual holes. A callback could repopulate a cache before directly changing a mapping; the new regression failed before dynamic suspension and passes afterward. A broad host allocation could cover CD apertures; another regression failed before clipping and passes afterward. The raw failed-before logs remain alongside passing XML. These checks cover nested helpers, WAIT/fault after the first longword word, reset/fetch/trace exits, invalid views, alignment, wrap and protection.

All four built-game3,000-frame arms match11inspect/picture checkpoints, full memory/pixel artifacts and complete WAV. NativeMain33,378,803/Sub48,429,321/Z8016,241,534 retirements, region retirements, CDD submissions and audio totals are unchanged; no floors or audio/queue faults. The candidate resolves14,330,018 Main and6,678,394 Sub data views. Those are participation counters, not a performance gain.

## Matched runtime result

| Arm | Process CPU seconds, frames1800-3000 | Process cycles |
| --- | ---: | ---: |
| B | 7.328125 | 32818514690 |
| A | 7.046875 | 32759888770 |
| A | 7.421875 | 35316533200 |
| B | 6.140625 | 28747807090 |

Paired CPU reductions are -3.99%/+17.26%; cycle reductions -0.179%/+18.60%. Baseline A/A drift is +5.32% CPU/+7.80% cycles on this shared host. One pair is slower in CPU with essentially flat cycles, while the other is faster. **Neutral/inconclusive** is the supported conclusion under the owner's per-game rule. Do not present the favorable pair as a proven18% win or infer other-game/host gains.

The EXE is 111,284,736 bytes versus 111,279,104 (+5,632). Two small views per CPU replace repeated mapping resolution; no guest-memory copy or page-version table is allocated. Existing effect metadata/object archive is reused with exact input identities. All work used measured capacity reservations; matched arms stayed on one host/configuration. Dummy outputs preserve full renderer/audio emulation but establish no physical-GPU/speaker result.

## Evidence and handoff

[Machine-readable results](engine-owned-memory-results.json) include raw samples, source/binary/package identities, costs and complete capacity receipts. Raw logs, captures/WAV, build receipts and frozen binary: `G:/Temp/Codex/genesis-engine-owned-memory-20261028`. Scripts, test XML, failed-before regressions and task/storage ledger: `C:/Temp/Codex/genesis-engine-owned-memory-20261028`.

The original E03 general owned-data contract is addressed with faithful exits. Cross-instruction pinning and every possible future memory design are not claimed exhausted. E02 remains rejected/open independently. The independent validator accepted the complete gate and instructed moving to E04 serially. Its saved validation record is VALIDATOR_E03.md at the task root. Nothing has merged or released; preserve native saves/settings and obtain the owner's combined final visual clearance before that handoff.
