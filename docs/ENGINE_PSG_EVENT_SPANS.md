# E21: active PSG event spans and exact filter tails

The whole selected bounded opportunity is rejected. No new E21 runtime,
build/test fixture or feature selection ships. Production/build/tests restored by
d96ca6bb3779487f057218f9ade792b0eb2c920f exactly match acceptedf4b10d7;
both complete pre-documentation trees are a00ea442cc3ad0b3f50893dfe7961105fd425a3a.
Existing settled-muted spans remain unchanged and receive no new gain credit.
Owning beads-3z5k.19, parent beads-3z5k, campaign beads-2wy8.1.

Two material implementations were committed, built and measured:

- 11e2bac569484f2529cc19d644615296dc951fff takes an exact original scalar
  sample at each audible boundary, then emits constant-input integer-filter runs
  and retires every tone/noise phase, including muted channels, with closed forms.
- b95058e8333ac14937835668f436efd834352a95 replaces that repeated retirement
  with a whole-interval producer: per-channel runs contribute into a16KiB derived
  int16 input buffer, followed by one exact filter pass and one timing retirement.
  The four maximum contributions sum to32764; the buffer is fully overwritten,
  single-owner derived storage and never serialized. Short/high-frequency or
  unqualified intervals use the original scalar path in both implementations.

Both preserve boundary-before-sample timing, stale noise output after control
writes, tone2 noise reload, periodic/white LFSR and its rising divider, C signed
integer-filter division/clamping, arbitrary writes, scratch/remainder/drop and
current file-load/rollback semantics. Fixed-point fills occur only after the
actual clamped recurrence reproduces its prior output with constant input.
No chip layout, public API, native progress or netplay capability changes.

The existing PSG fixture was extended with all active/muted channel combinations,
noise modes, periods0/1/2/15/16/31/1023, maximum signed mixes/tails, current file
save/load and existing arbitrary-write/pending-rollback/overflow checks. Its oracle
is the entire frozen old scalar source, not a second build sharing changed math.
The source suffix matches accepted SHA2569884d105c48bfae4e32a2a632f56d2b429293eadc7e66532316e1d26028d9b4e.
Source1 native/fallback pass on x64 and Win32; source2 native/fallback pass on x64:
six exact checks. Extra adoption-only portability/stress work was not run for the
discarded source2. All prototype source and fixtures remain in pushed Git history.

## Actual built-game outcome

Twenty distinct matched pairs/forty completed runs comprise17 cartridge pairs
and3 CD pairs. Every cartridge arm matches full WAV, raw PNG, periodic frame
hashes, full guest/audio state, sample counts and current-build rollback probes.
Every CD arm matches11 checkpoints,21 captured artifacts and the full WAV;
CD rollback is not claimed. Tests are hidden/muted with real audio emulation,
isolated saves/settings and fixed matched affinity/features in each pair.

| Source/workload | Observed cost | Decision |
| --- | --- | --- |
| Run retirement, RKA default | Wall+0.30/-0.46%, cycles+0.28/-0.18% | Neutral |
| Run retirement, RKA shadow | CPU lower6.44/2.03%, but cycles/wall mixed | No proven retained win; cannot offset Sonic |
| Run retirement, Sonic1 shadow | After first improvement, repeats CPU+4.84/+6.25%, cycles+4.33/+1.22%, wall+4.24/+1.54% | Does not qualify |
| Whole interval, RKA | Costs mixed by order | No proven retained win |
| Whole interval, Sonic1 shadow | CPU+0.77/+2.44%, cycles+3.20/+5.94%, wall+0.41/+8.20% | Adverse in both orders |
| Source1, Sonic1 default | Small mixed/neutral observations | Not a positive throughput claim |
| Source2, Sonic1 default | Cycles+2.19/+0.80%, wall+1.82/+1.50%, CPU+1.06/0% | Small adverse observations within variation; no improvement claimed |
| Run retirement, CD AB/BA | CPU-1.65/+4.22%, cycles+3.11/+0.51% | Mixed/inconclusive; no CD gain |
| Whole interval, CD | CPU+18.06%, cycles+9.66% | No useful candidate established |

Cartridge baselinefa696e2 and candidates share MSVC19.44 and frozen consumer/UI,
core, SDL and settings. CD shares the staged MSVC19.41 x64/SDK19041 qualified
archive/toolchain; only the PSG source changes, with exact original headers.
CD's1200-frame cost window is paced at about20seconds; stable delivery wall time
is not an uncapped FPS improvement. Peak memory/binary identities and all raw
costs remain in the existing records. No universal slowdown, gain or nonregression
claim is inferred from mixed observations.

After two unsuccessful architectures, independent focus review required finishing
only the in-flight reverse/cart and prepared CD check, then deciding/restoring.
No more threshold tuning, inventory, instrumentation or speculative rollout was
added. Neutral per-game outcomes are allowed, but repeated Sonic regressions and
no established useful retained package reject both designs. This is an
unsuccessful whole selected opportunity, not a deferral or proof every future
PSG design is impossible. E25's remaining PCM/CDDA constant spans are separate.

Evidence and exact source/binary/settings/host/build identities, six unit PEs,
full audio/state/captures and per-game samples are retained outside delivery
folders at G:/Temp/Codex/genesis-engine-psg-spans-20261031. Disposable object roots
were removed after passing PE/config copies were hash-verified; shared consumers,
SDKs/toolchains and native saves/settings are preserved. The borrowed SERVER CD
cache is restored to its original qualified source selection. See
engine-psg-event-span-results.json for cleanup and final independent gate.
No E21 runtime PR is appropriate; owner merge/release handoff remains for earlier
meaningful PRs. No owner game windows, merge or release were performed here.

Independent final source/records/cleanup and durability gates PASS. Owning issue
is closed; central push16.7seconds exit0. See ENGINE_PSG_EVENT_SPANS_REVIEW.md
and engine-psg-event-span-review.json. Original E22 follows verification of this
evidence-only copy/push, with a new validation-only reviewer.
