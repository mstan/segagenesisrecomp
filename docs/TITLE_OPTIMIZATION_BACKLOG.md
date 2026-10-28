# Deferred title optimization backlog

These items are retained from the42-family inventory and are **outside** the [engine optimization goal](ENGINE_OPTIMIZATION_BURNDOWN.md). They are not authorized implementation work by that goal.

Track any future implementation under the owning game's normal Beads hierarchy, relating it to `beads-2wy8.1`. Do not move title knowledge into engine code to evade this separation. Mixed original candidates were split into common capability versus game-specific policy/adoption; the two lists do not represent additive speedups.

- [ ] **T01: Whole game-routine HLE** — original N06.
  - Scope: Game decompression/copy/math/collision/object algorithms and their exact side effects.
  - Disposition: Deferred outside engine goal.

- [ ] **T02: Sound-driver HLE** — original N08.
  - Scope: SMPS or other uploaded driver command/music/effect/DAC semantics.
  - Disposition: Deferred outside engine goal.

- [ ] **T03: Game sprite and scene preparation** — original N13.
  - Scope: Replace or consolidate original BuildSprites/SAT generation with host object capture and scene publication.
  - Disposition: Deferred outside engine goal.

- [ ] **T04: Game world/layout publications** — original N17.
  - Scope: Sonic CD scene/SAT/layout capture and copies of the game-specific Main/Word RAM data model.
  - Disposition: Deferred outside engine goal.

- [ ] **T05: Whole Sonic CD asset loader** — original N30.
  - Scope: ISO/file-engine/decompression/Word RAM handoff and guarded original SP routines.
  - Disposition: Deferred outside engine goal.

- [ ] **T06: Prepared game asset caches** — original N31.
  - Scope: Per-title decompressed/transformed asset identities, dependencies and preload choices.
  - Disposition: Deferred outside engine goal.

- [ ] **T07: Custom scene eligibility and fallback adoption** — original N11.
  - Scope: A title adapter must establish whole-frame/custom scene eligibility and bind the common single-producer capability.
  - Disposition: Deferred outside engine goal.

- [ ] **T08: Stock-aspect custom renderer fidelity** — original N12.
  - Scope: Qualify original sprite limits, priority, objects and special scenes before enabling a game's renderer at320px.
  - Disposition: Deferred outside engine goal.

- [ ] **T09: Game sound catalogs and rollout** — original N07.
  - Scope: Capture/generate qualified driver variants and enable the common native Z80 backend for each title.
  - Disposition: Deferred outside engine goal.

- [ ] **T10: Game-native objects and snapshot payload** — original N03,N37.
  - Scope: Change title structures and compact/rebuild title-owned snapshot buffers while keeping guest-readable state and replay exact.
  - Disposition: Deferred outside engine goal.

- [ ] **T11: Custom art/effects and renderer adapters** — original N16,N18,N19,N21,N22.
  - Scope: Private art/layout caches, game special effects, custom image-reuse eligibility and title GPU/worker packet generation. Preserve the accepted Blue Spheres composition.
  - Disposition: Deferred outside engine goal.

- [ ] **T12: Game launcher media pipeline** — original N33.
  - Scope: Sonic CD PowerShell/Python/CUE conversion and verification integration using any common verified-media contract.
  - Disposition: Deferred outside engine goal.

- [ ] **T13: Game system-program services** — original N36.
  - Scope: SP/file-engine or other title routines identified by game-specific code/PC/state; these are not generic BIOS services.
  - Disposition: Deferred outside engine goal.

## Retained context

The Sonic CD eager original/custom double-render is confirmed, but an engine implementation must remain reusable. The title's scene eligibility and fallback policy remain here. Cartridge320px custom-renderer adoption likewise requires title-specific stock fidelity. Bios/system routines are engine work only when a standard reusable console-firmware contract is established; Sonic CD's original SP loader is title-owned.

The accepted original Blue Spheres composition and existing owner approvals remain valid. Do not reopen cleared title playtests or alter game geometry to pursue this backlog without a new request.

Raw42-family inventory and evidence: `C:/Temp/Codex/sonic-cd-loading-20261027/remaining-candidates.json`; audit `beads-3vb.30`. No new gain is claimed.
