# Original Sonic CD native acceptance

Updated 2026-10-21 on `feature/segacd-backend`. This is the current acceptance
ledger; older chronological handoffs describe earlier checkpoints. Development
is active. No CD merge into main/master is authorized. Campaign and Shadow work
remain preserved.

The owner has tested windowed Palmtree Panic with normal audio and reported that
it looks and sounds good. This is a working original-program native consumer,
with recomp-ui and adaptive widescreen. It is separate from the completed S3K
campaign adaptation.

## Evidence and remaining work

Evidence root: `G:/Temp/Codex/segagenesisrecomp/sonic-cd-native`. Each accepted
run records its executable identity, owned image gates, commands and mutations.
Accepted Main/Sub 68000 and Z80 execution is native, with Main/Sub floor counters
`0/0`; misses stop visibly. Diagnostic generation/comparison is separately named.

| Check | Status | Evidence / limit |
|---|---|---|
| Standard stage/time entry and brief movement | PASS 70/70 | Exact owned full MMD at entry and endpoint; reuse the qualified sweep. This does not prove every object mechanic. |
| Boss death, actual stage-end/results path | PASS 7/7 | PPZ, CCZ, TTZ, QQZ, WWZ, SSZ and MMZ. Combat and routes are explicitly bypassed; campaign ending is a separate check. |
| Special stages | PASS 8/8 | Actual six-UFO collision/completion paths. SS1 also proves the time-stone award and actual next-act return. |
| Secret SS8 credits and exit | PASS | `ss8-original-completion-latched-parent-9`: original secret credits, then real Start returns to exact owned TITLEM. |
| WWZ results and return | PASS | `wwz-cdd-latched-retake-parent-2/parent-title-and-write-audit.json`: original results advance 0402 to 0500, then StageSelect returns to exact TITLEM. No next-stage load is claimed from this caller. |
| MMZ final boss | PASS | `mmz-clean-source-campaign-ending-parent-4`: original firefly deaths, boss last-hit/death/escape and scripted stage exit. Normal campaign caller reaches the original ending. |
| SSZ race/cage/results and return | PASS | `r73d-race-completion-clean-3/title-return`: guest win/crash, actual cage collision with restored source hitbox, original timer/results, zone0502 to0600 and exact TITLEM return. |
| PPZ to CCZ save/restart/Continue | PASS | Actual clear writes durable BRAM; restart/Continue restores exact CCZ with no guest-memory writes. |
| Past time travel | PASS | Actual post/timer/WARP/owned R11B reload; player positioning/speed prerequisites were bypassed. |
| Future and reverse time travel | PASS | `G:/Temp/Codex/segagenesisrecomp/timewarp-qa/run-future-reverse-7`: actual posts/timers/WARP, exact R11A to R11D to R11A images; player position/speed prerequisites bypassed. |
| CCZ flipper | PASS | `ccz-flipper-clean-source-parent-2`: actual landing/standing relation, real jump, guest flipper animation and launch velocity on the clean source build. |
| WWZ moving bounce platform | PASS | `wwz-bounce-clean-source-parent-2`: original upward platform motion and actual standing carry preserve the source 0x23 center offset. Player death recovery was a setup bypass. |
| PPZ past robot generator | PASS | `ppz-past-machines-clean-source-parent-1`: rolling collision, guest explosion sequence and good-future flag. |
| PPZ projector destruction | PASS WITH FORCED HIT | `ppz-projector-clean-source-parent-3`: active source-qualified projector receives a forced hit latch; guest explosion, timer, destroyed flag and deletion execute. Physical-contact trial remains UNVERIFIED. |
| TTZ water and death/retry | PASS | `ttz-r41a-bubble-actual-2/independent-review.json`: underwater countdown reaches zero, actual drowning reduces lives 3 to 2, and R41A reloads. No timer/life/restart writes. Air-bubble refill was attempted but remains UNVERIFIED. |
| MMZ layer and size changes | PASS | `mechanics-mmz-runtime-2`: actual layer 0→1, shrink 0→1 and reverse enlargement 1→0; raw radius bytes 1309→0A05→1309. No layer/mini/radius writes. `parent-final-mechanics-and-widescreen-audit.json` corrects the worker's mixed decimal/hex radius notation. |
| QQZ lift and conveyor-like surface | PASS WITH SCOPE | Original vertical-lift carry retained. `qqz-conveyor-native-1`: exact R51A, visually authored panel surface carries Sonic right with zero velocity fields and no directional input; adjacent floor remains stationary. Representative passive carry is proved; original symbol-name attribution is not claimed. Parent audited every write. |
| Original campaign ending and credits | PASS | `mmz-clean-source-campaign-ending-parent-4/parent-ending-completion-audit.json`: actual MMZ1/2 results, boss and exit reach exact ENDING.MMD, original bad-ending animation, full credit roll and automatic exact TITLEM return. 7512 raw byte writes audited; no result/save/next-stage flag supplied. Final frame24632, floors0/0, audioFaults0. Initial QA zone-6 save bypasses prerequisites; this is not save validation. Alternate good-ending prerequisites are not claimed. |
| Independent final visuals | PASS WITH PHASE LIMIT | Qualified PPZ, TTZ and SS1 comparisons retained; SS8 comparison complete. `ending-visual-parent-review-1` independently checks the clean-build cinematic framing and credit-roll layout against the original. Exact animation phase/pixel parity remains UNVERIFIED. |
| Controls/audio/recomp-ui/adaptive widescreen | PASS WITH SCOPE | Clean candidate: normal window and exact `Sonic CD` caption with debug port; real movement/jump/pause; F1 UI freezes emulation and blocks held input on close; nonzero captured audio, audioFaults0. PID-qualified resize tests produce320/512px frames. `widescreen-r11a-midstage-3` proves continuous terrain in both actual margins and visible HUD after camera settle/12 frames. Earlier resize screenshots with black margins do not prove expanded scenery. Physical gamepad not newly tested. |
| Reproducible owned-input source build | PARTIAL | `sonic-cd-source-build-1/source-build-provenance.json`: clean compile/link of all 332 catalogs and 11256 generated source/header files passed, without imported archives. Complete BIOS/disc-driven source/capture generation still needs qualification. |
| Standalone owner package | PASS CANDIDATE | `G:/Temp/Codex/segagenesisrecomp/sonic-cd-playtest-3/SonicCD/SonicCD.cmd` uses the clean source binary, required notices and an empty BIOS-formatted BRAM seed. Media paths only; no owned disc/BIOS copied. Python/PowerShell validation and final presentation checks passed; launch link delivered. Owner feedback on this final package remains pending. |

All QA uses stage selection, teleports, protection and source-qualified actor
prerequisite cheats. The guest executes the completion trigger under test.
Result, save, success and next-stage flags are not supplied by QA. StageSelect
return-to-title and campaign progression are distinguished explicitly.

An earlier navigation helper also zeroed Sonic's radius bytes at offsets16/17.
Its terrain/collision observations are unverified. Original results/exit evidence
remains valid with this additional navigation bypass recorded. The helper now
preserves radii; SSZ's accepted cage collision and timer used restored source
hitboxes. The final representative interaction checks use the corrected helper.

## Runtime correction

`3fa3e6d` fixes the CDD response latch. A 75 Hz mechanical tick had replaced the
completed STOP response before the original BIOS consumed it. The next command
now replaces the response; mechanical movement retains it. SS8 and the WWZ
following load both pass with this fix. The mapped CDD/CDC/media/CDDA tests pass,
and the new regression fails against the old CDD implementation.

## Hardware accuracy qualifications

These are limited qualifications, not substitutes for required gameplay checks:

- Bus effects are scheduled at instruction boundaries; electrical bus/T-state
  precision is not claimed.
- Analog Genesis DAC intensity and display color conversion are not pixel-exact
  matches to the independent reference renderer.
- Drive-error and marginal-media behavior beyond the qualified owned disc has
  not been exhaustively checked.

Complete BIOS/disc-driven source-generation orchestration remains acceptance
work; it is not classified as a hardware defer. The clean source-bundle build
and owner candidate remain available while that recipe is qualified.
