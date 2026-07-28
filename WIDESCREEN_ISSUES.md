# Widescreen (16:9) — Known Issues & Outstanding Work

Status of the PSX-style **post-patch injection** refactor (recompile the ORIGINAL
unmodified ROM + widen via recompiler `[[widescreen_site]]` injection reading the
runtime `g_ws_margin`; engine renders an always-16:9 window with pillarbox for
non-gameplay screens). Mirrors snes/psxrecomp — the disasm/ROM is never edited.

## Conversion status (PSX-pattern vs legacy patched-disasm ROM)

| Target | ROM ingested | Widescreen | Notes |
|---|---|---|---|
| **Sonic 1** (`sonicthehedgehog`) | canonical `F9394E97` | ✅ injection (29 sites) | user-confirmed good |
| **Sonic 2** (`sonicthehedgehog2`) | canonical `7B905383` | ✅ injection (46 sites) | BG-arm + left-clamp + HUD fixes 2026-07-27 — see below |
| **Sonic 3 alone** (`sonic3` → Sonic3Recomp) | canonical `9BC192CE` | ✅ injection (18 sites) | user-confirmed good (incl. left-scroll fix) |
| **Sonic & Knuckles alone** (`sandk` → SonicAndKnucklesRecomp) | **patched-disasm** | ❌ NOT converted | canonical slice `0658F691` prepared (`sandk_canonical.bin`); not wired |
| **Combined S3&K** (`sonic3k` → Sonic3KRecomp) | **patched-disasm** | ❌ NOT converted | canonical = `Sonic3AndKnucklesRecomp/sonic3k.bin` (`63522553`); not wired |

## Outstanding bugs

### Sonic 2 — CPZ "diagonal corruption" (random missing sections) — FIXED (pending user confirm)
- **Symptom (was):** in Chemical Plant Zone, sections of the (BG) image randomly
  missing / diagonal-corrupted, most visible on arm / before scrolling.
- **Root cause (verified empirically 2026-07-27, plane-B dumps + hscroll bands):**
  two coverage gaps, no missing sites:
  1. **Arm-at-level-start ran after the initial fills.** The old
     `level_started_addr` gate held the margin at 0 through the whole
     `Level_TtlCard` load, so `DrawInitialBG` + the initial FG fill ran 4:3; on
     arm, only the FG got caught up (`Screen_redraw_flag` → widened `Draw_All`).
     Plane B is never redrawn by that path (verified: zero plane-B cell diffs
     across an arm) — its revealed margins held wrap-around/stale cells, offset
     per strip (CPZ hscroll bands: lines 0–159 = Camera_BG, 176–223 = Camera_BG2,
     diverging ~2:1 vs 8:1 to Camera_X) → the per-strip "diagonal" shear.
  2. **BG vertical-scroll row draws were never widened** (matches the reference
     disasm, which had the same gap): `Draw_BG1`'s simple rows (DBCE/DBE4) and
     `Draw_BG3_CPZ`'s per-strip-camera rows (DE24) drew 22 blocks from d5=-16,
     leaving margin-wide holes in fresh top/bottom rows whenever the BG scrolled
     vertically — CPZ scrolls vertically everywhere → "random missing sections"
     during play.
- **Fix (game.toml only, no engine/codegen changes):**
  1. `[widescreen]` drops `level_started_addr` and adds the 0x80-flagged
     transition modes (0x88/0x8C) to `eligible_modes`, so the margin is armed
     while the load sequence runs. The BG cameras start ratio-aligned at load,
     so `DrawInitialBG`'s full-512px fill is strip-correct including margins,
     and the (already-widened) per-strip seam maintenance keeps them correct.
     Also covers act transitions and death-respawn reloads (same load path).
  2. Eight new `[[widescreen_site]]` entries widen the BG vertical row draws
     (subimm d5 seeds + `call_widen` DrawBlockRow→CustomWidth at DBD0/DBD8/DBDA,
     DBE8/DBF2/DBF4, DE3C/DE4A) — same recipe as the FG `Draw_All` rows.
- **Explicitly rejected:** calling `DrawInitialBG` ($E300) on arm as a plane-B
  catch-up. It fills ALL rows relative to Camera_BG only (a3=$FFEE08); with
  strip cameras diverged mid-level it would repaint the fast strip at the wrong
  offset across the whole window — far worse than stale margins, and it would
  not heal until 512px of scroll. Do not resurrect that idea without per-strip
  fill logic.
- **Remaining (documented) limitation:** a LIVE mid-level 16:9 toggle (runtime
  overlay) still reveals stale plane-B margins in strip zones; they heal as each
  strip's seam next advances over them (one scroll). The common launcher-on
  path no longer hits this (armed from load). A per-strip arm-time sweep (ramp
  the margin 16px/frame while firing each zone's own scroll flags) is designed
  but needs per-zone flag knowledge — revisit only if the toggle case matters.
- **Verified 4:3 unchanged:** boot_smoke (frames 60/300) + zone_smoke (63
  fbhashes, EHZ run) byte-identical vs a pre-change build at margin 0;
  dispatch_misses empty. (Checked-in smoke baselines are Windows-captured and
  do not match macOS runs even pre-change — compare against a same-machine
  baseline.)
- Debug: the runner now exposes a `ws_set {"on":0|1}` TCP command (arm/disarm
  the user widescreen request, engine state only) so probes can script arm
  transitions — see DEBUG.md.

### Sonic 2 — "stale artifact to the left of the starting areas" — FIXED (pending user confirm)
- **Was:** at a level's left boundary the 16:9 view revealed area *beyond where
  the level exists* (the 512px plane wraps → stray terrain/black at the left
  margin at level starts).
- **Fix (2026-07-27): camera left-bound clamp.** New `addmem` site kind (see
  vocabulary below): ScrollHoriz's Min compare/load ($D750 `cmp.w (a2),d0`,
  $D754 `move.w (a2),d0`) widens the bound value to `Min + margin`, **capped at
  Camera_Max_X_pos** (the word at `(a2)+2`, `base = 2`) and never below
  authentic Min — so the camera stops `margin` px inside the level's left edge
  and the revealed left margin is always REAL level data. At a boss lock
  (`Min == Max`) the cap makes it degrade to authentic values (no snap, no
  oscillation). Plus one `addimm` at $C19C (LevelSizeLoad's initial-camera
  clamp-to-zero → clamp-to-margin) so the level fades in already clamped.
  Camera_Min/Max RAM is NEVER written — game logic (LevEvents, boss triggers)
  sees authentic values.
- **Max side deliberately NOT clamped:** LevEvents boss triggers wait for the
  camera to REACH Camera_Max_X_pos; shrinking the effective Max risks
  softlocks. The right margin can therefore still reveal past Max at an act's
  far right — normally real level data, cosmetically fine.
- **Verified (macOS, EHZ1):** camera = 64 (= margin) at spawn with bounds RAM
  authentic (0/$29A0); run right → walk back left: camera re-clamps to exactly
  64 via the runtime path; player reaches the true wall (x=$10) and stands
  visible inside the revealed margin; margin-0 regression byte-identical
  (boot_smoke f60/f300 + 63 zone_smoke fbhashes); dispatch_misses 0/0.
- Note: player-vs-camera dynamics near the left boundary differ from 4:3 by
  construction (the camera stands `margin` px further right). Everywhere else
  the camera trajectory is authentic.

### Sonic 2 — HUD anchored to the widescreen edge (2026-07-27)
- `BuildHUD_P1` positions every 1P HUD piece with a `move.w #x,d3` immediate;
  six `subimm reg=3` sites ($40836, $4088C, $408AC, $408D0, $408F2, $4090A)
  shift SCORE/TIME/RINGS, the digit fields, and the lives block left by the
  margin, keeping the authentic 16px inset from the TRUE left edge. 2P split
  HUD untouched (widescreen gated off in 2P). Data-only (game.toml).

## Remaining work (next session)

1. **SonicAndKnucklesRecomp (S&K alone):** swap `sandk/sandk.bin` → canonical slice
   (low 2MB of canonical S3K = `0658F691`); build canonical S&K `.lst` from skdisasm;
   restore/regen discovery + reconcile `extra[]`; clean `[widescreen]` (drop carved
   `extra_ram_addr`/`redraw_flag_addr`); author `[[widescreen_site]]` from the S&K
   listing; recompile/build/validate.
2. **Sonic3KRecomp (combined S3&K):** same, using the full canonical S3K
   (`Sonic3AndKnucklesRecomp/sonic3k.bin`, `63522553`) + `sonic3k.lst`. Note: combined
   has pre-existing dispatch misses (separate bring-up issue, not widescreen).
3. **Sonic 2 BG-on-arm fix** (above).

## Recompiler injection vocabulary (in `[[widescreen_site]]`)

`mask10` (andi `$1FF`→`$3FF`), `addreg`/`subreg` (`D[reg] ± margin>>shift` before insn;
`scale` multiplies margin), `addimm`/`subimm` (widen a `moveq`/`move` immediate in
place), `cull_left` (widen a left-edge `bmi` non-mutatingly), `cull_window_left`
(mutate + `bhi`→`bgt` for a ring-window left clamp), `call_widen` (preset a reg +
retarget a call, for row counts whose callee resets the count), `addmem` (at a
word `cmp.w (An),Dn` / `move.w (An),Dn`, widen the VALUE read from memory by
+margin>>shift; `base` != 0 caps the result at the word `base` bytes after the
operand and never below the original — the camera min-bound clamp: Min+margin
capped at Max, authentic at a Min==Max boss lock). Multiple transforms
may share one instruction address (`ws_site_for_kind`). All are no-ops at
`g_ws_margin == 0` ⇒ byte-identical authentic 4:3.
