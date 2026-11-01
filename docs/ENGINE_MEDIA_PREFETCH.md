# Bounded generic media prefetch (E26)

Candidate `f88e90b6fcefd1188ed73c87314757810c8d85ee` builds on accepted E25
`8efa456ffe0cf8c315772d6fbc28c0ab91a4b59f`. Owning issue: `beads-3z5k.24`;
campaign: `beads-2wy8.1`. [PR54](https://github.com/mstan/segagenesisrecomp/pull/54). The original bounded contract is independently accepted; combined owner handoff remains.

The raw image reader predicts adjacent requests and reads ahead on the existing
persistent host worker. Two 512-sector buffers retain the current block while
the next completes. Windows uses an overlapped handle reopened from the verified
file handle; POSIX uses `dup` and positioned `pread`. Neither changes the demand
reader's FILE cursor. Only complete, published blocks serve demand reads.
Resident first-track data keeps priority. Busy, allocation, worker and background
read failures retain synchronous demand reads with unchanged failure semantics.

Transport submissions, peripheral reset, Main RESET and media replacement cancel
speculation. Direct image callers use the explicit cancellation API for their own
reset/unmount paths. Close acknowledges worker completion before freeing buffers
or handles. Image APIs have one owner thread; only the supplied job runs elsewhere.
The pure reader requires no SDL service. The standalone runner installs the host
service by default; `GENESIS_ENGINE_WORKERS=0` retains synchronous operation.

Lazy buffer cost is 2,408,448 bytes per active image plus worker/handle overhead.
Seeks may still block on the first demanded sectors; speculative reads may go
unused. Full payload validation and first-track residency are unchanged. This
does not shorten guest mechanical delays or establish cold-storage/startup gains.
`GENESIS_CD_MEDIA_PREFETCH=0` exercises synchronous operation;
`GENESIS_CD_MEDIA_TRACE=1` enables optional demand-read wait observations.

Focused checks pass 8/8 on x64 and 7/8 on Win32. The remaining Win32 media-state
assertion fails identically on the separately built accepted E25 baseline.
The actual-worker fixture covers multiple distinct-byte windows, busy misses,
completed-next retention, seeks, cancellation, bounds, independent images,
pending close and allocation/create/admission/background-failure fallbacks.
The pure portable reader also passes GCC on Windows. POSIX is source-reviewed;
no Linux runtime qualification is claimed.

Existing Sonic CD checkpoint, pixel, full WAV and guest-error checks are reused.
The initial pair's complete correctness captures match; an unrelated inherited
audio-trace assertion prevented retaining candidate cost, so that pair is
excluded from timing comparisons. Two complete matched BA/AB pairs pass. CPU cost is lower 3.1697%/4.6778% and
process cycles 2.5933%/6.6889% on the paced 1800-to-3000-frame scope, including
identical checkpoint work. Baseline drift is -1.636% CPU/-0.897% cycles; shared
host variation remains. This is modest observed route gain, not universal FPS.
Baseline raw reads 2,139 become 10/12 synchronous reads plus 2,129/2,127 cache
hits, six jobs and no background failures. Demand-read callback wall time
includes scheduling; its 21.5/116.5 ms maxima do not measure physical disk latency
or prove all stalls removed. Startup wall times vary in opposite directions;
no startup gain is claimed. Final private bytes increase by 2,445,312/2,613,248;
the PE grows 5,120 bytes. Exact raw samples/identities and limitations are in
[the results](engine-media-prefetch-results.json).
Unchanged cartridge packages inherit E24 evidence; they are not new E26 tests.

Independent final source/runtime/record/cleanup review accepted the original E26
contract. Its verdict identity is retained in the results. No further E26 testing
is required; durable tracking closeout precedes original E27.
