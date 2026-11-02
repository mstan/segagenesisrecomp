# E20 independent final review

PASS: restoration, records, cleanup and the whole bounded rejection are accepted
at `f6a605d4ad4451ebc8289a0325b3ed86f154a35c`. Durable Beads updates and central
sync are confirmed.

- Restored `02c0fa8d32d1099f8b2d1728301e4b8cbc6a3b92` and accepted `10ace05`
  have the identical complete tree `d5ae13ffcc16f6fb652c1343bb3ffbcc0acfb9f0`.
  Current worktree is clean, remote head matches, and the final commit changes docs only.
- Two material source variants were implemented and evaluated. All 13 frozen
  oracle files independently matched the accepted source; native and fallback
  checks passed four times. Recounted 20 matched pairs and 40 completed runs.
  Cartridge PCM, full state, pixels and current rollback match; both CD orders
  match 11 checkpoints, 21 artifacts and the full 44,787,108-byte WAV.
- Fixed routes do not qualify globally. Compact routing reproducibly worsens
  Sonic1 shadow-off cycles by 6.97/4.54% and wall by 14.30/5.61%; shadow-on gains
  cannot offset it. CD CPU changes are -8.33/-0.773% and cycles -7.475/+7.111%,
  with large baseline drift. This is neutral/inconclusive, not an 8% win or a
  sufficient reason for a new compiler/backend selection. Neutral results remain
  acceptable; this decision rejects the extra selection rather than requiring
  every game to improve.
- Counts independently recompute to 22 resolved and 15 open across 37, comprising
  14 original goals plus mandatory E37. No runtime, test/build additions or new PR
  remain. This closes the selected unsuccessful opportunity without claiming all
  future FM designs are impossible or re-crediting the sample-block wrapper.
- Raw and unit record hashes match. Relocated unit binaries match their recorded
  hashes. Four disposable local build roots are absent (171,054,054 bytes);
  verified remote duplicate cleanup records 118,319,104 bytes, totaling
  289,373,158. Useful binaries/configuration/full evidence remain. The shared CD
  cache selects the original qualified source, and its restore job exited 0.

The focus correction was applied: finish the existing CD decision, stop repeats
and speculative rollout, then restore and record the outcome. No new workload,
benchmark framework or owner-facing launch was requested or run by this review.
See `VALIDATOR_E20_FINAL.json` for exact identities and checks.

Central `beads-3z5k.18` is closed, and final PASS comments are durable in the
owning issue, parent `beads-3z5k` and campaign `beads-2wy8.1`. The completed
central push log ends with `Push complete.` (root exit 0, 12.4 seconds); these
records were read independently.

Proceed to original E21 after copying this evidence into the final docs commit.
Root implements serially in an isolated worktree and uses a new independent
validation-only reviewer for E21. This reviewer does not carry over to that goal.
