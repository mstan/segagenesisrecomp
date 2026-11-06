# Host save-state persistence

Modern game adapters capture at a resumable native boundary. The producer
serializes that immutable snapshot into a unique temporary file. One SDL worker
then checksums it, patches the envelope, flushes/closes, and atomically replaces
the slot. The worker never accesses live guest, renderer or audio state.

Success is announced only after replacement. A failed write retains the old
slot. A second save while busy is rejected. A load during pending capture is
rejected explicitly; a load during writing drains that accepted snapshot first.
Shutdown drains accepted work before SDL cleanup. Failed thread creation uses
the same synchronous writer. The existing SRAM writer accepts fixed-size memory
blobs, so it is not reused for this seekable variable-size FILE serializer.

The CRC remains reflected IEEE CRC32 with identical incremental/file semantics;
an immutable256-entry table replaces the per-bit loop on every platform.
The independent bitwise oracle, chunk boundaries, short/error reads and32MiB
file test pass. No optional CPU instructions or platform-specific CRC variant
are introduced. Existing legacy game save paths remain unchanged.

## Measured candidate, Windows x64

Same host, native4:3,60Hz guest/144Hz presentation, no army, four saves per game.
The baseline is the released v0.7.4 family; raw per-save data, binaries and
settings are identified in `save-writer-20261106.json`.

| Game | Baseline request-to-confirmation ms | Candidate ms | Repeated audio frames, before → after | Decision |
| --- | --- | --- | --- | --- |
| S3K | 80.386,79.500,79.852,78.460 | 48.787,49.797,48.783,48.766 | 900 →0 | Improved save behavior; all four saved RAM images equal |
| S&K | 67.239,66.865,66.160,66.550 | 48.680,49.251,48.653,48.523 | 390 →0 | Improved save behavior; all four saved RAM images equal |
| S3 | 0.477,0.328,0.333,0.317 | 0.294,0.318,0.325,0.333 | 0 →0 | Neutral unchanged legacy path; sub-ms differences are not a proven speedup |

Confirmation latency includes native-boundary capture and main-thread collection
of completed work. It is not the time the game is blocked. Candidate S3K/S&K
runs have no recorded save-frame hitch events, versus four each in the baseline.
An earlier CRC-only experiment still had a124ms storage stall and3449 repeated
audio frames, and was rejected as the complete correction. Phase measurement
isolated an87ms atomic-commit spike, motivating the bounded worker.

Dummy audio output preserves the actual mixing/callback path while remaining
muted; this is not a speaker/device listening claim. General scheduler/pacer
variation and overflow counters remain in the raw results. Capture serialization
still writes the temporary FILE on the producer; this change does not promise
stall-free capture on arbitrary failing or saturated storage.

The production-header test deliberately blocks atomic commit, mutates live state,
and verifies immutable bytes, prior-slot retention, busy/pending requests,
timeouts, failed replacement/load ordering, shutdown and thread fallback.
Both modern Sonic adapters also pass six active/paused Blue Spheres cases and
30 regular save/load cases, including exact repeated/cold RAM/VRAM/PNG and
malformed/configuration rejection. Linux and final package/owner gates are
recorded separately before release.
