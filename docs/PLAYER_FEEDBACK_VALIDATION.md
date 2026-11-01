# Replayable feedback implementation evidence

Recorded before v0.6.3 release integration.

## Qualification, 2026-11-01

The Windows x64 Release consumer was built with `GEN_ENABLE_TRACE=OFF`,
`GEN_DEV_TRACE=OFF`, and `SONIC_REVERSE_DEBUG=OFF`. Both focused CTests passed,
as did strict MinGW C11 warning checks for the new modules. No existing release
or owner save was replaced; all executions used isolated disposable profiles,
hidden software rendering, and dummy audio.

| Actual Sonic 3 & Knuckles workload | Result |
| --- | --- |
| Vanilla, title/data select into Angel Island, native quicksave/load | All 7,500 execution checks matched; final machine hash `262F24212ECC9794`, audio hash `22087E07C9CD74EE` |
| Knuckles & Knuckles, crowd 3, Adaptive 398x224, existing SRAM, native quicksave/load | All 7,500 execution checks matched; final machine hash `4C320FDD7F9E589F`, audio hash `7B6CB63958D3DB37` |
| Existing determinism probe, period 120/depth 8 | Seven restores and 56 resimulated ticks; all 956 recorded executions matched; probe reported zero divergence/asymmetry |

The first full gameplay test exposed the scheduling effect of requesting a
quicksave. Save requests are now replay events too; qualification retains the
full scheduler check rather than ignoring that difference. Dispatch-miss files
were empty for the qualified recordings and reproductions.

The focused fixture also checks all six controller rows, resource/option
restoration before character activation, viewport changes, imported states,
startup SRAM and its loaded flag, corrupt hashes/attachments, different builds,
interrupted frames, default-on upgrades, opt-out persistence, and retention.
This qualifies offline reproduction and checkpoint restores. Live network
sessions are outside this qualification.

The exact matching executable, raw reports, logs and result JSON are retained
under `build/feedback-20261101/evidence` (about 55 MiB). The source changes are
uncommitted; no release was published.

Cleanup remains outstanding: the audited, task-created generated C, compiler
intermediates, copied ROMs and disposable profiles total 423,053,603 bytes
(about 404 MiB) under `build/feedback-20261101`. Native PowerShell cleanup with
`Remove-Item -LiteralPath ... -Recurse` **without `-Force`** was rejected before
execution with `blocked by policy`. No shell fallback or protection bypass was
attempted. Preserve `evidence/` when removing the other temporary directories;
the audit found no reparse points, nested repositories or protected entries.

## v0.6.3 integration

Rebased onto the shipped engine `ae1c742`. All 108 Windows production CTests
pass. The recorder fixture also builds with GCC warnings-as-errors and passes
on Ubuntu, including startup context and native-save flush notifications.
Actual production Sonic3K recordings replay all 1,874 native-game executions
and 1,888 executions with Shadow/Sonic 1 stages and an existing campaign
journal. Both use isolated profiles; playback writes no native SRAM.
Campaign primary/backup journals and legacy import inputs are attached through
the optional GameSpec startup-context callback; successful SRAM flushes are
replayed as notifications at their recorded execution boundary.
Release integration evidence: `C:/temp/Codex/sonic-feedback-release-20261101`.
