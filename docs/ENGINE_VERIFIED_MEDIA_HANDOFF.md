# Verified media handoff (E27)

The selected original contract is technically qualified at runtime commit
`2276caa2ab94db546759e6f4f3334a742027f721`, based on accepted E26 `070dcdb`.
[Engine PR55](https://github.com/mstan/segagenesisrecomp/pull/55) and
[Sonic CD PR2](https://github.com/mstan/SonicCDRecomp/pull/2) carry the implementation
and mechanical adoption. Owns `beads-3z5k.25`, campaign `beads-2wy8.1`.
The combined owner play, merge and release handoff remains pending.

A cached-CUE launcher supplies its existing expected complete-image SHA instead
of independently reading the converted image. The runner hashes actual whole
image and payload together, checks both, then publishes an opaque owned image.
An expectation never substitutes for verification. Corruption, malformed bounds
and provider failures retain complete validation/restart. Source CUE/BIN
inspection, first conversion and direct SGCD launches keep their existing flow;
no title converter/cache redesign is included.

Windows holds a read handle excluding writers/replacement until close. Linux
uses a private read-only RAM snapshot when available, with a portable private-file
snapshot fallback. Resident data aliases those immutable bytes; workers close
before storage. Resource failure fails closed or uses the complete fallback.
The RAM FILE has no descriptor: E26 positioned prefetch becomes synchronous memory
reads. The actual Linux game gate exercises that path, rather than inferring it
from the reader fixture. Maximum snapshot size is 1,058,400,912 bytes. Sonic CD
uses 602,056,768 bytes and adds 457,850,880 bytes (436.64MiB) actual-game peak RSS.
The separate component probe observed about 441MiB added RSS. Windows does not
copy the complete image.

Six actual Windows 3000-frame runs preserve all 11 canonical checkpoints, 21
artifact byte counts/SHA and full WAV. Gameplay CPU reductions are mixed:
-2.45%, -6.11%, +0.97%; cycles are +3.47%, -6.64%, -3.87%. Baseline CPU span is
8.18%. Independent review accepts gameplay as neutral/inconclusive within observed
host variation; no gameplay gain or universal nonregression is claimed.

A corrected startup ABBA check asserts the same affinity and BelowNormal priority
for the old whole-file hash and native process. Verification plus frame0 native
initialization uses 6.16?10.02% less CPU. Elapsed observations improve 23.1?68.4%
but are too noisy for a firm latency claim. Common CUE work and PowerShell helper
startup are excluded; this is not first-visible-screen latency. Unmatched summed
startup wall from full-game pairs is excluded, with raw observations retained.

Two actual Linux 3000-frame runs each match the entire Windows canonical outputs.
CPU 18.61?18.46 seconds and wall 20.402226?20.401103 seconds are neutral. Total
startup CPU including old whole-file preflight is 1.36457337?1.35 seconds; wall
2.416547?2.518509 seconds. No Linux loading/FPS gain is claimed. Baseline prefetch
6jobs/2135hits becomes 2139 synchronous RAM reads, zero prefetch jobs and
wholeIdentityChecked=1. Full source/binary/native-archive identities are retained.
The private-file snapshot alternative regressed about 20.8% component CPU and
44.7% wall and was rejected as preferred; it remains a correctness fallback.

Focused checks pass 12/12 x64, 11/12 Win32, four Linux reader/provider suites and
15/15 consumer tests, including the real PowerShell cache-hit bridge with a valid
argument-echo PE. The Win32 media line69 assertion fails identically in the frozen
accepted baseline. Its raw overall-script JSON retains built=false/tested=false
after nonzero ctest; the retained existing LastTest.log proves 11 passes/1 failure,
with the known baseline proof and all executable hashes referenced. No rerun or
rewriting of raw observations was needed.

The staged Deck root, compiled object/unit scratch and duplicate SERVER captures
are removed. Exact raw JSON, Win32 individual logs, small Windows traces, complete
candidate visual/audio evidence and the native archive/tested ELFs needed for
remaining campaign work are retained. Cartridge code is unchanged and inherits
prior evidence. [Results and exact retained identities](engine-verified-media-handoff-results.json)
contain the limitations and final closeout references.
