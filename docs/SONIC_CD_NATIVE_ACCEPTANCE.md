# Original Sonic CD native acceptance

Updated 2026-10-21 on `feature/segacd-backend`. This is the current acceptance
ledger; older chronological handoffs describe earlier checkpoints. The requested
Sonic CD burndown is complete on this branch. No CD merge into main/master is
authorized. Campaign and Shadow work remain preserved.

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
| Independent final visuals | PASS WITH PHASE/ASPECT LIMITS | Final frozen binary: `G:/Temp/Codex/segagenesisrecomp/final-owned-input-qa-1/independent-review.json` and `parent-final-visual-audit.json`. Independent PPZ/TTZ/SS1/SS8 retakes plus parent visual/source/write review pass. PPZ/TTZ show actual400px expanded scenery; special stages retain the original256px projection centered with72px black bars, with no special-stage widescreen claim. Early blank/loading/title-card samples were rejected. `ending-visual-parent-review-1` retains the independent cinematic/credit layout comparison. Exact animation phase/pixel parity remains UNVERIFIED. |
| Controls/audio/recomp-ui/adaptive widescreen | PASS WITH SCOPE | Final owned-input binary: `window-owned-input-final-1/parent-final-presentation-audit.json`. Normal window and exact `Sonic CD` caption with debug port; real movement/jump/pause; F1 UI freezes emulation and blocks held input on close. Stereo capture last-second RMS2410/peak12874, audioFaults0, floors0/0. PID-qualified960x720 and1600x700 clients; final512x224 midstage capture has continuous terrain in both96px margins and visible HUD. Earlier stage-start black margins do not prove expanded scenery. Physical gamepad not newly tested. |
| Reproducible owned-input source build | PASS | Public recipe generation and full verified resume produce333 catalogs/11290 source/header files from owned BIOS/disc. `G:/Temp/Codex/segagenesisrecomp/sonic-cd-owned-input-build-1/source-build-provenance.json`: clean source1969498, pinned clean cores, configured/built true, zero imported archives. All2019176 required original M68K guards preserved across332 images (one Sub patch split); fresh native Z80 bootstrap has zero historical code-region guard gaps. Legacy-only data/stack variants are explicitly retired by source-role audit. Final CMake confirmation exits0. |
| Standalone owner package | PASS | `G:/Temp/Codex/segagenesisrecomp/sonic-cd-playtest-4/SonicCD/SonicCD.cmd` contains the final frozen binary, SDL2, required notices and a newly BIOS-formatted empty BRAM seed. Media paths only; no owned disc/BIOS copied. Python verifies15 package files; PowerShell `-ValidateOnly` passes. New profile defaults to a normal window, adaptive widescreen and normal audio; prior packages/saves preserved. Owner feedback on this exact package remains pending. |

All QA uses stage selection, teleports, protection and source-qualified actor
prerequisite cheats. The guest executes the completion trigger under test.
Result, save, success and next-stage flags are not supplied by QA. StageSelect
return-to-title and campaign progression are distinguished explicitly.

An earlier navigation helper also zeroed Sonic's radius bytes at offsets16/17.
Its terrain/collision observations are unverified. Original results/exit evidence
remains valid with this additional navigation bypass recorded. The helper now
preserves radii; SSZ's accepted cage collision and timer used restored source
hitboxes. The final representative interaction checks use the corrected helper.

## Final pass/fail/unverified ledger

- PASS: all70 standard stage/time variants,7 boss completions,8 special-stage
  completions, secret credits/exit, original campaign bad ending/full credits/title
  return, durable save/restart/Continue, representative mechanics and time travel.
- PASS: strict native Main/Sub/Z80 execution, complete owned-input generation,
  verified resume, clean source compile, standalone package integrity, final
  independent visual comparisons and window/controls/audio/UI/adaptive stage view.
- FAIL: none in the accepted final checks. Failed setup/timing samples are kept
  and excluded explicitly; they are not relabeled as gameplay passes.
- UNVERIFIED: physical projector contact, water air-bubble refill, physical
  gamepad, alternate all-time-stones good-ending prerequisite path and exact
  reference animation/pixel phase. These are coverage limits, not runtime stubs
  or claims of validated behavior. Special-stage widescreen expansion is not
  implemented or claimed; the original projection is retained.

The final focused presentation run reaches2923 frames with native
Main31911883/Sub46947438/Z8015968947, floors0/0 and audioFaults0. All16 raw byte
writes were the Stage Select row and player kinematics; no outcome flags were
supplied. Four independent final visual retakes also have floors0/0 and
audioFaults0; SS8 uses controller input only. Recent generation checks pass
10 M68K orchestration tests and8 Z80 generator tests. Earlier qualified runtime,
device, media, audio and portability tests remain applicable.

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

## Final build identity

The frozen `SonicCD.exe` is143287808 bytes, SHA-256
`80c14de9c887afd5d52d25360beeb1a8ce3d5da21544fc77c23a09c0798ca74d`.
Source commit is `1969498c637262b2a537b87ff059daa633acd31b`, with M68K core
`7233852a6e45d8779b9dae8b29afd6b94d5231dc` and Z80 core
`4dfc5fc29fd67d23714126348e678e45952e9462`. Source and both cores were clean.
The final acceptance documentation commit does not change runtime code.

Generation evidence is in
`G:/Temp/Codex/segagenesisrecomp/owned-native-regeneration-1`; compile evidence
is in `G:/Temp/Codex/segagenesisrecomp/sonic-cd-owned-input-build-1`.
`qualified-builds/native-owned-input-final-1/frozen-build-identity.json` binds
the executable, SDL2 and copied source/generation provenance. No relink is used
after this freeze. The owner package contains this same executable hash.
