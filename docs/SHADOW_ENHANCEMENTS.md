# Shadow Audio + Screen Enhancements (Genesis / Mega Drive backport)

Backport of the gbarecomp "verified-enhancement" QoL layer to
segagenesisrecomp. All work lives on the `feat/shadow-enhancements` branch /
`_shadow_segagenesisrecomp` worktree (sibling of the `segagenesisrecomp`
checkout, off `master`). It does not touch the existing `dev`,
`vdp-own-port`, `declown-headers`, or `dev-genesisrecomp-runners` branches.

## Governing principle (the carve-out)

Faithfulness is the product; these are an opt-in layer on top. The one
permitted form of HLE here is a **verified-enhancement shadow**, allowed only
when ALL hold (recomp-template/PRINCIPLES.md, "Verified-Enhancement HLE Is
Allowed; Load-Bearing HLE Is Not"):

1. The emulated (canon) path keeps running and stays both the authoritative
   output and the verify oracle. The shadow is never ground truth.
2. The shadow is continuously, differentially checked against the canon stream
   and substitutes only after a proven window.
3. It reverts loudly (logs DEGRADED) the instant it stops matching.
4. It is opt-in and present-time, off by default; with it off the output is
   byte-identical (the `[FBHASH]` framebuffer hash, PNG dumps, and the WAV
   capture stay on the raw canon).

Worst-case failure is "the user hears/sees the authentic hardware output," and
it cannot mask a recompiler bug because the canon path it shadows is still the
thing being diffed.

Genesis note: the shipping native target is the **clean-room own backend**
(genesis_vdp + ymfm YM2612 + clean-room SN76489); the AGPL clownmdemu core is
the dev-only `_oracle` target. The shadow layer lives entirely in the
permissive runner, links no clownmdemu, and is safe to ship.

## What ports verbatim vs what is Genesis-specific

| Piece | Status | Notes |
|---|---|---|
| **`ShadowVerifier`** (envelope-correlation self-check, auto-gain, prove/strike/pause) | **DONE** — `runner/audio/audio_shadow.{c,h}`, C, compiles clean (`gcc -std=c11 -Wall -Wextra`, object produced) | Engine-agnostic; **byte-for-byte identical algorithm** to the gbarecomp C++ and snesrecomp C ports (thresholds, window geometry, calibration all unchanged). Only the header comment is console-specific. |
| Color-science core (xy→XYZ, primaries→matrix, Bradford, sRGB OETF) | **DONE** — `runner/video/color_lut.c` | Verbatim C transliteration of gbarecomp `color_lut.cpp`'s CIE core. |
| Present-path color LUT | **DONE** — `runner/video/color_lut.{c,h}`, compiles clean | **Genesis-specific:** 9-bit color (512-entry LUT, BGR 3-3-3) instead of GBA/SNES 15-bit BGR555; output is ARGB8888 (not RGB24); the GBA LCD-panel models are replaced by **CRT/composite** models. |
| **YM2612 FM shadow render** | **DONE** — `runner/audio/fm_shadow.{cpp,h}`, compiles clean against vendored ymfm | **Genesis-specific:** the "engine" is the *hardware* YM2612, not a software driver. A second `ymfm::ym2612` is fed the identical register-write stream; the enhancement is a **relaxed output low-pass** (keeps the cleaner, less-aliased highs the canon wrapper deliberately tames to match the reference) plus an active **ladder-effect correction** (`GENESIS_FM_LADDER=off` renders the shadow through ymfm's ladder-free `ym3438`). |

## Console specifics

### Video — 9-bit color, CRT/composite display

- CRAM holds 9 significant bits as `0x0EEE`: bits 3:1 = R(0–7), 7:5 = G(0–7),
  11:9 = B(0–7). The canon present path (`runner/main.c:md_colour_to_argb`)
  expands each 3-bit channel ×36 (7×36 = 252 ≈ 255) into ARGB8888.
- `SCREEN_RAW` (default) reproduces that ×36 expansion **bit-identically**, so
  default-off output equals the existing path exactly.
- Opt-in models: `crt` (generic SMPTE-C phosphor + γ≈2.4 + lifted black),
  `trinitron` (Sony P22-ish primaries), `composite` (gentler γ + higher black
  floor), `linear` (clean linear-light reference). Models go through the CIE
  core (panel primaries → sRGB via Bradford, sRGB OETF).
- **Composite-artifact nuance (documented, not faked):** a real Genesis drives
  a CRT over composite/RF. The visible "Genesis look" includes dot crawl,
  cross-color rainbowing, and the NTSC notch-filter horizontal blur that games
  exploited for transparency dithering. These are **spatial/temporal effects
  of the composite signal chain**, not a per-pixel color transform — they
  cannot be reproduced by a 512-entry LUT, and synthesizing them would mean
  guessing the signal chain. We therefore model only the per-pixel color
  (phosphor gamut + gamma + black floor) and explicitly leave the spatial
  artifacts out. A faithful composite filter would be a separate present-time
  shader stage over the framebuffer, out of scope here.

### Audio — YM2612 (6-ch FM) + SN76489 (PSG)

- Canon FM is `runner/audio/ym2612_ymfm.cpp` (ymfm, BSD-3). It applies a strong
  output low-pass (coeffs 6.910/4.910) specifically to **tame ymfm's excess
  highs and match the clownmdemu reference**, and gain 195/256.
- The shadow (`fm_shadow.cpp`) runs a **parallel ymfm chip** on the same writes
  but with a near-passthrough LPF, preserving the cleaner high-frequency
  content — that is the enhancement. Gain is kept identical so the verifier's
  level ratio starts near 1.0.
- **Ladder-effect option (`GENESIS_FM_LADDER=off`):** the YM2612 DAC "ladder
  effect" is a real quantization quirk — the 9-bit multiplexed DAC opens a
  ~7-LSB gap around zero (ymfm `dac_discontinuity`: negatives -3, positives +4),
  the audible crossover "crunch". This is now an **active transform, not a hook**:
  ymfm's `ym3438` class is the same FM engine without that discontinuity (the
  later CMOS Genesis FM revision that fixed it), so with `GENESIS_FM_LADDER=off`
  the shadow chip is instantiated as `ym3438` (clean DAC); otherwise it stays
  `ym2612` (authentic ladder) and only the relaxed LPF differs. We do NOT guess a
  correction curve — we use ymfm's own ladder-free model. The ShadowVerifier
  polices it like any other enhancement: a ladder-free stream that stops
  correlating with canon reverts loudly rather than substituting.
- PSG: the SN76489 is mixed in `runner/audio.c` (`MIX_INTO`, PSG ÷8 + FM ÷1).
  The FM shadow substitutes only the FM stereo samples before that mix; the PSG
  path is untouched. (A PSG-band shadow could be added the same way later.)

## Integration points (found on `master`; file:line in this worktree)

- **Canon audio mix / render:** `runner/audio/mixer.c` — `audio_mixer_drain()`
  walks cycle-stamped events, advancing/writing the canon YM2612 between writes,
  then `ym2612_render()` (`mixer.c:159`) fills the FM stereo buffer.
  - Shadow wiring added:
    - `mixer.c:25` `fm_shadow_init()` in `audio_mixer_init()` (arms from env).
    - `mixer.c:~135` mirror each FM `ym2612_write`/`ym2612_advance` into
      `fm_shadow_write`/`fm_shadow_advance` (same port/value/cycles).
    - `mixer.c:~146` mirror the tail-advance.
    - `mixer.c:~164` `fm_shadow_verify_and_substitute(fm_stereo_out, fm_n)`
      right after `ym2612_render` — feeds the verifier and substitutes when
      proven; logs `[AUDIO-SHADOW] DEGRADED ...` on revert.
- **Final delivery mix:** `runner/audio.c:audio_flush()` (`MIX_INTO` macro,
  ~`audio.c:162`) — consumes the FM buffer the mixer produced. Unchanged; it
  receives canon or proven-shadow FM transparently.
- **Canon video present:** `runner/main.c` — `s_framebuf` (ARGB8888) is filled
  per scanline (`scanline_rendered_cb` ~`main.c:200`, own-backend
  `own_scanline_sink` ~`main.c:240`), `[FBHASH]` hashes raw `s_framebuf`
  (~`main.c:1921`), then `SDL_UpdateTexture` uploads it.
  - Shadow wiring added:
    - `main.c:~92` `s_present_buf` + `s_color_lut` + `color_lut_setup()` (parses
      `GENESIS_SCREEN`; default raw/passthrough).
    - `main.c:~1336` `color_lut_setup()` call (next to `audio_mixer_init`).
    - `main.c:~1935` at upload: if a model is on, `color_lut_map_frame()` into
      `s_present_buf` and upload that; **`s_framebuf` is never modified**, so
      `[FBHASH]` and PNG dumps (`png_write_argb`, `main.c:854`/`1876`) stay raw.
- **Build (game CMakeLists, OUTSIDE this worktree** —
  `F:/Projects/segagenesisrecomp/<Game>Recomp/CMakeLists.txt`): add to
  `SONIC_SHARED_SOURCES`:
  ```cmake
  "${RUNNER_ROOT}/audio/audio_shadow.c"
  "${RUNNER_ROOT}/audio/fm_shadow.cpp"      # C++ (ymfm); CXX already enabled
  "${RUNNER_ROOT}/video/color_lut.c"
  ```
  `fm_shadow.cpp` needs the ymfm include (`${RUNNER_ROOT}/external/ymfm/src`,
  already on both include lists) and `video/` (already on both lists). No new
  link deps; no clownmdemu. The game CMakeLists were not edited because they
  live outside the isolated worktree.

## Gating (env, default OFF)

- `GENESIS_SCREEN={raw,crt,trinitron,composite,linear}` — present-time color
  model. Unset / `raw` ⇒ passthrough, byte-identical.
- `GENESIS_AUDIO_SHADOW=1` — arm the FM shadow. Unset/`0` ⇒ every `fm_shadow_*`
  call is a no-op, FM buffer untouched.
- `GENESIS_FM_LADDER=off` — request ladder-effect correction (currently a
  logged hook; see "What's left").

(Per-game `[video] screen` / `[audio] shadow` config keys can be layered later,
mirroring gbarecomp; env is the present interface.)

## Compile status

Standalone compile-checks (gcc/g++ 14, `-Wall -Wextra`, object produced):

- `runner/audio/audio_shadow.c` — clean (`-std=c11`).
- `runner/video/color_lut.c` — clean (`-std=c11`).
- `runner/audio/fm_shadow.cpp` — clean (`-std=c++17`, `-Iexternal/ymfm/src`);
  only warnings are from the vendored ymfm headers, none from our code.
- `runner/audio/mixer.c` (edited) — clean (`-std=c11`).

`main.c` is not standalone-compilable (needs SDL2 + the whole generated TU
tree); the added block uses only the documented `color_lut.h` API and standard
calls. A full game build (Sonic 1 native target) is the next gate.

## Status / next steps

Implemented: verifier ✓, video LUT ✓, audio FM shadow + verifier +
substitution ✓ (all env-gated, default OFF, revert-loud).

What's left:

1. **Wire the 3 sources into a game CMakeLists** (Sonic 1 native target) and do
   a full build + default-off byte-identical check (FBHASH + WAV unchanged with
   env unset), then A/B `GENESIS_SCREEN=crt` and `GENESIS_AUDIO_SHADOW=1`.
2. ~~**Real ladder-effect correction**~~ — DONE. `GENESIS_FM_LADDER=off` now
   renders the shadow chip through ymfm's `ym3438` (ladder-free DAC) instead of
   `ym2612`; no hand-rolled curve. Verifier-policed like the LPF enhancement.
3. **PSG-band shadow** (optional): a less-aliased SN76489 re-render, substituted
   pre-`MIX_INTO`, policed by a second verifier — same pattern as FM.
4. **Composite spatial filter** (optional, separate stage): if the authentic
   composite "look" is wanted, add a present-time spatial shader over
   `s_present_buf`; keep it documented as non-colorimetric and present-only.
5. **Per-game config keys** for screen/shadow, mirroring gbarecomp.

## Attribution

- `ShadowVerifier` (`runner/audio/audio_shadow.{c,h}`) and the color-science
  core in `runner/video/color_lut.c` are ported from JRickey/gba-recomp
  (`crates/gba-core/src/shadow.rs`, `crates/screen/src/{color,profile,lut}.rs`)
  via the gbarecomp C++ ports and the snesrecomp C port, © Jrickey,
  MIT OR Apache-2.0, used with permission. See `THIRD-PARTY-LICENSES.md`.
- The FM shadow chip is **ymfm** (Aaron Giles, BSD-3-Clause,
  `runner/external/ymfm`). The parallel-instance + verify wiring, the Genesis
  9-bit LUT indexing, the CRT models, and the ARGB8888 present path are ours.
