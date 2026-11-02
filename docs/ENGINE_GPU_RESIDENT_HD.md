# GPU-resident internal resolution (E37)

The optional internal-resolution setting renders original Genesis assets from
immutable hardware raster jobs on desktop OpenGL. Native is the default; the
setting applies on the next launch. Unsupported drivers, unavailable frame
leases and opaque custom output keep the existing native presentation.

The native renderer publishes compact tile spans while it performs its normal
scanline preparation. Each span references the original packed four-byte tile
row, its palette and priority; sprite visibility and operator masks preserve the
native composition. The three bounded frame slots own every referenced source
value. Exact content identity observes direct writes, DMA, private art banks,
palette changes and remaps, including mid-frame changes. A held frame remains
immutable through later frames, reset and restore. No address-only dirty cache
is trusted. The GPU keeps source slots resident and compares their exact values;
job uploads contain native-density descriptions, never an enhanced framebuffer.

The GPU samples these sources directly at the requested density. There is no
CPU loop or host image whose size grows with the scale. Native framebuffer
production, guest reads, hardware state, audio and canonical serialization retain
their existing owners. Restore/reset revoke published jobs; their source values
remain immutable, and the next publication is compared exactly with residency.
Scale and window geometry are local presentation settings. The same job contract
has an exact native-density portable CPU renderer without SDL or OpenGL.

Targets are admitted against the driver texture limit and a 512 MiB output
budget. Larger requests reduce the presented extent and log the admitted size.
The request is never reported as a full-density result when admission reduced it.
Generic hardware widescreen, extended palettes and private sprite sources are
represented. Title-owned complete-row renderers and arbitrary host overlays
require matching provenance before they can use this backend; their existing
native output remains intact. Title adapters belong in the title backlog.

## Owner-PC measurements

RTX 3080 Ti, OpenGL 4.6 NVIDIA 616.86, hardware timestamps; actual Sonic 1,
1200 full-runtime frames, hidden normal window, dummy audio, raw colors, native
view and deterministic run/jump input. Other host load is retained in the raw
records. These are measured throughput/headroom observations, not quiet-host
comparative speedup claims.

| Request | Actual output | GPU draw mean / p95 | Full-runtime FPS |
|---|---|---|---|
| 1x | 320x224 | 0.0222 / 0.0246 ms | 748.1 |
| 4x | 1280x896 | 0.1099 / 0.1208 ms | 785.5 |
| 16x | 5120x3584 | 1.5022 / 1.6497 ms | 272.3 |
| 32x | 10240x7168 | 5.7653 / 6.3468 ms | 114.1 |
| 64x | 10240x7168, reduced | 5.7499 / 6.3468 ms | 116.1 |

Full 16x and 32x demonstrate headroom. Full 64x is not qualified. All six tested
scales (including 2x) uploaded exactly 127,446,016 bytes over 1199 GPU frames:
2,010,112 source bytes and 125,435,904 job bytes. CPU preparation does not grow
with pixel density. One pending 4x query is absent from its CSV; complete upload
counters cover all frames and the summary distinguishes the two populations.

Full GPU readbacks match actual native output for twelve corpus renders at 1x
and 16x, including odd 511-wide canvas/IM2 at 8176x7168, shadow/highlight,
diagnostics, private art and extended palettes. Actual Sonic 1 full 16x readback
matches all 18,350,080 pixels, including after real built-runner window resize
and after working warm rewind. Across frozen E35/default/GPU1x/GPU16x, the
2400-frame route has identical state/audio hashes, periodic frame hashes,
native PNG and full WAV; mutation selftest, 18 rollback probes and warm restore
pass. The known Sonic 1 cold native-file load plus rollback-probe access
violation also reproduces with exactly the same baseline/candidate pixels and
RAM/VRAM; it remains a separate pre-existing failure, not a passed load check.

The existing native differential passes directed cases and 4000 random cases.
Both portable implementations pass 32 complete frame comparisons, direct-write
and private-source cases, live-lease/backpressure, actual VDP reset, and opaque
custom fallback. The fixtures actually build and run under Ubuntu GCC 13.3 as
well as Windows. These are portable job-contract checks, not lower-end GPU
performance qualification.

Owner `beads-3z5k.17`, campaign `beads-2wy8.1`. Source/binary/ROM/settings/host
identities and raw logs, CSV, WAV, state files and captures are retained under
`C:/Temp/Codex/genesis-engine-hd-rendering-20261101`; the GPU summary is
`qa/sonic1-gpu-scales-summary.json`, native gate `qa/canonical.json`, and detailed
GPU handoff `qa/e37-gpu-qualification.md`. Combined batch review, owner visual
handoff and merge remain separate gates.
