# Nuked-OPN2 (vendored, dev-only)

Die-accurate YM2612 (Yamaha YM3438) emulator by Alexey Khokholov (Nuke.YKT).

- Source: https://github.com/nukeykt/Nuked-OPN2
- Pinned commit: `335747d78cb0abbc3b55b004e62dad9763140115` (version 1.0.9)
- License: **LGPL-2.1-or-later** (see headers in `ym3438.c` / `ym3438.h`).

**Dev-only oracle.** Used solely as a die-accurate FM reference by
`tools/synth_replay` (replays the recomp's own chip-write ring through Nuked-OPN2
for a differential comparison against ymfm). It is **never compiled into or linked
with any shipped native binary** — same posture as the AGPL clownmdemu `_oracle`
build. Shipped releases remain PolyForm-Noncommercial + ymfm (BSD-3) etc. per
`RELEASING.md`.

Configure Nuked in YM2612 mode (`OPN2_SetChipType(ym3438_mode_ym2612)`) for the
MD1/MD2 discrete-DAC ladder. Drive `OPN2_Clock` at the internal slot rate
(1 slot = 42 system-master cycles; 24 slots = one 53267 Hz output sample).
