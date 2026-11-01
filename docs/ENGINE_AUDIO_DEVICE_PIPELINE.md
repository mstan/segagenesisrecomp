# E23: synthesis-to-device pipeline

The whole selected bounded opportunity is rejected. No new E23 runtime, API,
feature selection or test/build wiring remains. Restoration commit
4191f5685f6b2402dcbea7e2cf321bc93755bd7f has the exact complete tree of accepted
fe66879a240fc408c88de42bc1f08c66deacc479:
cdceab20c456df607f8799da28967f37109925d8. No PR is warranted. Owning issue:
beads-3z5k.21; campaign: beads-2wy8.1. Final records/durability review remains.

This was a coherent pipeline replacement, not a copy-only experiment. The
prototype combines FM/PSG base mixing and saturation with qualified PCM/CDDA
interpolation and final saturation in an owned complete-frame producer. Trusted,
immutable, API-created windows use endpoint coverage instead of repeating a
whole interpolation walk. Exact quotient/remainder stepping and one retirement
walk replace repeated timestamp work. Post-WAV integer master gain and float-ring
publication share a block producer, with original scalar overflow/large-count
behavior. Opaque extra callbacks keep the original path; any callback setter
revokes the owned registration.

Canonical chip synthesis, immutable lookahead inputs, both clipping points,
pre-volume WAV capture, and the pull-thread FIR/controller/preroll/concealment
remain because they define observable samples, phase, latency or thread ownership.
Moving clipping or signed division after resampling would change those results.
Existing subsystem interfaces were not treated as fixed architectural boundaries.

Two implemented variants were built and evaluated:

- df15a7f9da43d048c13c81a3277103c85bc2a34c contains the full producer and
  qualification fixture (runtime c6f20f2, initial implementation b502151).
- b6f641bbe9a2041c05704e2c70020245ae873afd corrects identity master gain:
  100% volume dispatches directly to unchanged rab_push. The scaled helper also
  delegates identity gain without rewriting input samples. This is a generic
  mathematical identity, not a title check or a user rollout toggle.

## Correctness and actual workloads

Five focused contracts cover native queue/window integration, generic mixing,
opaque extra callbacks and WAV lifecycle, DRC state/delivery, and the owned frame.
Ten passing x64 check executions cover the prototype revisions. The independent
CD mixer oracle is the complete original fe668 Git blob, SHA-256
851a753aaa5f838792d6b5311dea7203bde64b99518dd2fa408c6d40beb3a9d0.

The complete-frame fixture compares generated samples, complete mix state,
pre-volume WAV, post-gain S16, float-ring state and representative nonzero pulls
against the frozen CD renderer and unchanged base mixer. Its independent expected
bridge reads pre-volume WAV samples, applies the old signed integer gain formula
and calls unchanged rab_push. Cases exercise clipping, source activation, missing
brackets/transactional underflow, gain 0/1/25/50/75/100%, divisors 1/8/64 and
registration revocation. Additional DRC cases cover ring wrap, overflow, reset,
large counters above 2^53 and five source rates. Dummy callbacks are paused and
pulled deterministically; muted zero samples are not the sole oracle.

Twenty completed game runs form ten matched pairs: four Sonic CD runs and sixteen
cartridge runs. CD preserves all 11 inspection checkpoints, 21 artifacts and the
full WAV. Cartridges preserve full WAV, raw PNG, periodic frame hashes, canonical
guest/audio state and current-build rollback probes. CD rollback/save-state
qualification is not claimed. All recorded comparisons pass correctness.

CD uses the qualified native archive/catalogs/UI and MSVC 19.41/SDK19041, with
frozen E22 binary 9d0c4625 as reference. Cartridges use supported frozen consumers
and regeneration, MSVC 19.44, and accepted fa696e2 binaries (their compiled inputs
were unchanged through E22). Audio is 100% master gain into silent SDLdummy;
CD removes the old --mute flag to avoid measuring a zero-gain shortcut. Each pair
keeps its host, features and affinity fixed. Cartridge delivery is paced, RKA at
1,800 frames and Sonic 1 at 900, including default and optional FM shadow profiles.

Source1 CD actually executes 11,196,766 owned generated samples and 2,999 bridge
helper calls per run. Those helper counts do not prove every internal block used
the fast overflow-free kernel. Source2 RKA trace [0,0,1800] proves unity/original
publication; it is not a scaled-kernel gain. The second CD build and source2 Sonic
1 builds are built preparation only, with no runtime/acceptance claim. Win32
adoption checks were not run for discarded code.

## Cost decision

Positive changes below mean higher cost. No aggregate offsets an individual
game/profile's unresolved adverse result.

| Variant and profile | CPU cost change | Cycle cost change |
| --- | --- | --- |
| Source1 CD AB | +4.70% | +0.87% |
| Source1 CD BA | -4.22% | -4.10% |
| Source1 RKA default AB / BA | +2.86% / +13.08% | +3.26% / +0.80% |
| Source1 RKA shadow | 0% | -1.21% |
| Source1 Sonic 1 default | -6.00% | -0.97% |
| Source1 Sonic 1 shadow AB / BA | +22.22% / -18.42% | -1.56% / -0.59% |
| Source2 RKA default, final ABBA | 0% / -9.68% | +8.75% / +3.70% |

CD and Sonic 1 observations are mixed/neutral-inconclusive. RKA cycle costs remain
adverse in paired order after the generic unity correction. The final balanced
run also has substantial baseline drift and mixed CPU cost, so this is not a
universal slowdown claim. It does not establish a useful retained benefit or
supported per-game non-regression for the added machinery. Paced wall time is
not an uncapped FPS result. Host overflow drops occur in both arms and vary by
order; generated-WAV parity is not proof of universally improved speaker delivery.

Independent review required stopping after this final bounded decision. Both
variants are rejected and all new code/fixtures/build selection is restored,
rather than deferring qualification or continuing timing until a favorable result.
This evaluates the whole selected contract; it does not claim every future
staging/resampler design is impossible. E24 ownership and E25 constant spans remain
separate opportunities.

The six SERVER source overlays are byte-exact accepted Git blobs, and the cached
original Sonic CD graph has been reconfigured and rebuilt. Old S16 was untouched.
583,190,637 bytes of disposable builds, per-run binary/ROM copies and remote CD
duplicates were removed; selected PEs, configurations, raw evidence and one unit
SDL dependency remain. The shared restored source/build cache remains for E24
reuse, with cleanup due after campaign integration. User inputs, saves/settings
and delivery folders are preserved. No owner launch, merge or release occurred.

Raw evidence: G:/Temp/Codex/genesis-engine-audio-device-pipeline-20261031.
Exact source/binary/package identities, per-game samples, raw record hashes,
binary relocations and cleanup are in engine-audio-device-pipeline-results.json.
