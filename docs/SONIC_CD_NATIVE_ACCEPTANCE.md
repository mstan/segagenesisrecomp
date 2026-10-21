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
| Boss death, actual stage-end/results path | PASS 6/7 | PPZ, CCZ, TTZ, QQZ, WWZ and MMZ. SSZ results/exit remains open. Combat and routes are explicitly bypassed. |
| Special stages | PASS 8/8 | Actual six-UFO collision/completion paths. SS1 also proves the time-stone award and actual next-act return. |
| Secret SS8 credits and exit | PASS | `ss8-original-completion-latched-parent-9`: original secret credits, then real Start returns to exact owned TITLEM. |
| WWZ results and return | PASS | `wwz-cdd-latched-retake-parent-2/parent-title-and-write-audit.json`: original results advance 0402 to 0500, then StageSelect returns to exact TITLEM. No next-stage load is claimed from this caller. |
| MMZ final boss | PASS | `mmz-final-original-ending-parent-4`: original firefly deaths, boss last-hit/death/escape and scripted stage exit. StageSelect returns to title; campaign ending is still open. |
| PPZ to CCZ save/restart/Continue | PASS | Actual clear writes durable BRAM; restart/Continue restores exact CCZ with no guest-memory writes. |
| Past time travel | PASS | Actual post/timer/WARP/owned R11B reload; player positioning/speed prerequisites were bypassed. |
| Future and reverse time travel | UNVERIFIED | Targeted original post/warp checks in progress. |
| Representative unique mechanics and death/retry | UNVERIFIED | Complete focused checks; loading a stage or finding a handler does not pass a mechanic. |
| Original campaign ending and credits | UNVERIFIED | Needs the Continue campaign caller and actual MMZ completion triggers. A QA-only initial zone-6 save is a prerequisite bypass, not save validation. |
| Independent final visuals | PARTIAL | Qualified PPZ, TTZ and SS1 comparisons retained; SS8 independent original comparison complete. Ending comparison remains open. Phase/DAC differences preclude raw pixel-parity claims. |
| Controls/audio/recomp-ui/adaptive widescreen | PARTIAL | Owner playtest and five adaptive window shapes passed previously. Focused final-candidate regression remains. |
| Reproducible owned-input source build | UNVERIFIED | Public converter/extractor/catalog generator/CMake exist; complete source and diagnostic-capture orchestration still needs qualification. Private imported archives are an incremental cache, not a clean build recipe. |
| Standalone owner package | UNVERIFIED | Package tooling and final candidate qualification in progress. Delivery must start windowed with normal audio and preserve saves. |

All QA uses stage selection, teleports, protection and source-qualified actor
prerequisite cheats. The guest executes the completion trigger under test.
Result, save, success and next-stage flags are not supplied by QA. StageSelect
return-to-title and campaign progression are distinguished explicitly.

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

Unfinished ending, mechanics, regressions, source-build orchestration and package
delivery remain acceptance work; none is classified as a hardware defer.
