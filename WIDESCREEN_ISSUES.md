# Widescreen (16:9) — Known Issues & Outstanding Work

Status of the PSX-style **post-patch injection** refactor (recompile the ORIGINAL
unmodified ROM + widen via recompiler `[[widescreen_site]]` injection reading the
runtime `g_ws_margin`; engine renders an always-16:9 window with pillarbox for
non-gameplay screens). Mirrors snes/psxrecomp — the disasm/ROM is never edited.

## Conversion status (PSX-pattern vs legacy patched-disasm ROM)

| Target | ROM ingested | Widescreen | Notes |
|---|---|---|---|
| **Sonic 1** (`sonicthehedgehog`) | canonical `F9394E97` | ✅ injection (29 sites) | user-confirmed good |
| **Sonic 2** (`sonicthehedgehog2`) | canonical `7B905383` | ✅ injection (29 sites) | minor BG bugs — see below |
| **Sonic 3 alone** (`sonic3` → Sonic3Recomp) | canonical `9BC192CE` | ✅ injection (18 sites) | user-confirmed good (incl. left-scroll fix) |
| **Sonic & Knuckles alone** (`sandk` → SonicAndKnucklesRecomp) | **patched-disasm** | ❌ NOT converted | canonical slice `0658F691` prepared (`sandk_canonical.bin`); not wired |
| **Combined S3&K** (`sonic3k` → Sonic3KRecomp) | **patched-disasm** | ❌ NOT converted | canonical = `Sonic3AndKnucklesRecomp/sonic3k.bin` (`63522553`); not wired |

## Outstanding bugs

### Sonic 2 — CPZ "diagonal corruption" (random missing sections), minor
- **Symptom:** in Chemical Plant Zone, sections of the (BG) image randomly missing
  / diagonal-corrupted, most visible on arm / before scrolling.
- **Root cause (analysis):** every disasm widening SITE is covered (verified the
  `Ws*` counts match the disasm diff exactly, minus OOZ which doesn't exist in the
  `fixBugs=0` retail ROM). The gap is **coverage, not sites**: S2 has only ONE
  `Screen_redraw_flag` reader (the FG path → `Draw_All`). **Plane B (BG) is not
  fully redrawn on widescreen-arm** — its margins fill only as you scroll (via the
  widened BG column draws). So CPZ's prominent BG shows stale margins until scrolled.
- **Fix direction:** find/trigger an S2 Plane-B full-redraw on arm (analogous to the
  FG `Draw_All`), or widen the BG initial-fill extent. Needs per-zone verification.

### Sonic 2 — "stale artifact to the left of the starting areas", likely INHERENT
- At a level's left boundary the 16:9 view reveals area *beyond where the level
  exists*; there are no real tiles there to draw. Only visible at the extreme start
  before moving right. The disasm approach has the same limitation (can't conjure
  off-level tiles). Treat as cosmetic/inherent unless a clamp is desired.

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
retarget a call, for row counts whose callee resets the count). Multiple transforms
may share one instruction address (`ws_site_for_kind`). All are no-ops at
`g_ws_margin == 0` ⇒ byte-identical authentic 4:3.
