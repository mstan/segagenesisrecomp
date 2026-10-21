# README showcase sources

Feature/catalog review: **21 October 2026**. These are real game captures.
No scenery, actor or HUD content has been synthesized or painted in.

| Asset | Capture source |
|---|---|
| `sonic-1-ultrawide.png` | Sonic 1 production validation, `production-validation-mods/fixed32/start.png`: Green Hill gameplay at 32:9. |
| `sonic-2-six-player.png` | Sonic 2 local development validation, `qa-six-final/party/follow.png`: six enabled actors, with Sonic, Tails, Knuckles, Amy, Mighty and Shadow. Imported assets remain local. |
| `sonic-3-ultrawide.png` | Combined S3K custom-renderer validation, `final-s3k/32-9/checkpoint-8.png`: Angel Island. This is native S3K scenery, not an imported Megamix stage. |
| `rocket-knight-wide.png` | Rocket Knight Adventures' public [`adaptive-bridge.png`](https://github.com/mstan/RocketKnightAdventuresRecomp/blob/main/docs/images/adaptive-bridge.png). |
| `sonic-cd-wide.png` | Sonic CD 0.0.1's public [`palmtree-panic-wide.png`](https://github.com/mstan/SonicCDRecomp/blob/v0.0.1/docs/screenshots/palmtree-panic-wide.png), copied unchanged. Native Windows capture; expanded Palmtree Panic scenery and original HUD. |

## Sonic CD release boundary

[SonicCDRecomp 0.0.1](https://github.com/mstan/SonicCDRecomp/releases/tag/v0.0.1)
is the original US CD program running on native Main/Sub 68000 and Z80
catalogs, with Windows/Linux x64 packages. Its engine pin is
`c8bcab94babc217955c8aab195d7de8d4b532331` on `feature/segacd-backend`;
the backend has not been merged into engine master. This is separate from
the S3K-hosted CD campaign adaptation. The screenshot demonstrates normal-stage
expansion; special stages keep their original centered projection. Full
coverage, Linux package smoke checks and unverified cases are recorded in
the game's [validation ledger](https://github.com/mstan/SonicCDRecomp/blob/main/docs/VALIDATION.md).

## Sonic 2 release boundary

The local development header declares `S2_MAX_PLAYERS = 6` and
`S2_NETPLAY_PLAYERS = S2_MAX_PLAYERS`. The public default-branch snapshot
`e1451eee49cf2238164e282d0c1a8f79ce8a76aa` and release `v0.8.3` still declare
`S2_MAX_PLAYERS = 4`. The README therefore labels the six-character image as
a development preview and keeps public downloads described as four-seat.
No six-peer WAN qualification is claimed from that screenshot or a constant.

## Sonic 3 content

The current [game README](https://github.com/mstan/Sonic3AndKnucklesRecomp)
describes independent Sonic 1, Sonic 2, Megamix, Mighty and Shadow mods.
[Stage-pack setup](https://github.com/mstan/Sonic3AndKnucklesRecomp/blob/main/docs/STAGE_PACKS.md)
and [Megamix notes](https://github.com/mstan/Sonic3AndKnucklesRecomp/blob/main/docs/MEGAMIX_STAGE_PACK.md)
give donor identities, conversion, provenance, save handling and validation
limits. Credit the SonicTrilogy/Megamix and Sonic Retro work as documented
there. The README preserves the imported-campaign restriction on quickstates,
rewind and netplay rather than combining independent feature claims.

The [Rocket Knight game](https://github.com/mstan/RocketKnightAdventuresRecomp)
owns its adaptive renderer and original/viewport enemy-activation policy.
Framework beta status does not certify every stage, route or renderer mode.
