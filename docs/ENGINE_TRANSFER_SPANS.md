# E08 transfer spans and layout mapping

Owner `beads-3z5k.7`; campaign `beads-2wy8.1`. Production source `11a55fd`,
parent `e378efa` (E07 production `2079c51`), shared 68000 core `f7dd616`,
Z80 core `4dfc5fc`. Independent gate is pending; PR [42](https://github.com/mstan/segagenesisrecomp/pull/42), stacked on PR41. No merge/release approval.

The useful repeated transfer work on the current CD route is maintaining an
inactive controller's six-clock phase: 72,259,107 calls, with zero active DMA
words. Device service now owns that inactive clock span directly. It updates
only phase at the original boundary after PCM/CDDA/stamp work and before CDD
sector delivery. Activity is tested there, so a preceding callback can start
DMA and immediately reach the complete original controller. Active pacing,
FIFO reads, WAIT/protection, status, IRQ and completion behavior are unchanged.

The phase operation is exact for every uint8 phase and uint32 clock count,
including unsigned carry. It does not add state, pointers, snapshots or copied
views. `segacd_dma_advance` keeps its public behavior. The default-on internal
`SEGACD_DMA_IDLE_SPANS` build control exists for qualification, not a UI option.

The generic five-band cell mapper also replaces variable division with a bit
permutation. It retains separate 32/16/8/4/4-row bands, each byte lane, RET bank
selection and the existing complete-word bus ownership decision. There is no
layout table, retained view or new transfer ABI. `SEGACD_CELL_PERMUTATION=0`
compiles the original coordinate formula for qualification.

## Complete selected-family inventory

The original goal includes cartridge/CD VDP, CDC, PCM and generic Word RAM
layouts. Existing subsystem boundaries were not treated as architectural
requirements. The decisions below cover the current implementations, rather
than rejecting one memcpy and deferring the rest of the family.

| Candidate | Decision and evidence |
| --- | --- |
| Inactive CDC-DMA clock spans | Implemented phase-only ownership at the original device ordering boundary. Actual CD route has 72,259,107 inactive calls and no active words. Final clean timing is recorded below; diagnostic confirmation has72,259,107phase-only spans and zero complete-controller calls, with identical IO/DMA/layout/PCM participation. |
| Active CDC transfer spans | The existing scheduler already delimits each six-clock word. Between words, PCM/CDDA samples, stamp completion, CPU ownership changes, CDC decoder/host access and CDD delivery can observe or change state. A write can WAIT without consuming FIFO. The active controller retains its exact loop for direct API callers and callbacks that activate it during a longer span. The actual CD route has zero active words/multi-event eligibility, so a new active bulk protocol removes no demonstrated work. The unrestricted API already accepts longer clocks and preserves sequential partial progress. No unused batching ABI was added. |
| CDC FIFO/refill and CPU host words | A host word already checks and consumes the ordered pair within one bus operation; no intermediate host callback occurs. A fused pair consume/refill is a real possible replacement WITHOUT a new representation or ABI. Final actual participation is49,248 pairs/98,496 successful bytes;48 sampled pair bodies estimate0.001447seconds,0.01052% processCPU including timer/counter/preemption overhead. The two consume/refill calls (excluding the preceding destination/ready check and final value construction) are timed. The very small measured body budget rules this local fusion unproductive on the current route; FIFO capacity alone does not rule it out. DAC/DBC, DOUTEN, DTBSY/DTEN/DTEI, odd tails, end and failed-read state remain source semantics. |
| Decoder sector-to-ring copy | One decoded sector is already processed in one uninterrupted delivery operation; header/subheader/status and finite DECI are set at that delivery boundary. A ring-wrap split copy could replace the byte loop WITHOUT a new representation. The actual run decodes114sectors and writes131,712ring bytes; timing every whole copy loop estimates0.0000263seconds/0.000191% processCPU including measurement/preemption. This concrete copy budget rules that replacement unproductive for the current route. Header/status and DECI publication are not delayed. |
| PCM DMA and sample ordering | PCM is a distinct destination with odd/high then even/low writes, banked 4KiB window, +4 internal address, 19-bit wrap and timed sampling. It is not a contiguous memcpy destination. Existing per-word ownership remains, with active DMA tests for lane/bank/wrap/short transfer and callback-started transfers. Actual route wave writes/samples continue, but active PCM DMA words are zero. Neither sound-driver semantics nor skipping audio was used. |
| VDP cartridge source spans | DMA already owns a whole job and pays the aggregate raster-dependent 68K stall. Safe ROM/WRAM source batching could remove per-word callbacks/decoding, but must qualify SRAM overlays/ROM extensions, 128KiB source wrap and destination callbacks. RKA6000 and Sonic12400 whole DMA+fill job estimates are only 0.107/0.141% of process CPU; these rough instrumented estimates include loop/callback/store/stall body, timers and preemption and are not strict ceilings. A bulk-read interface with source-region guards, or a simpler qualified destination specialization without new host-pointer state, has little demonstrated whole-job budget. Both are ruled unproductive for the current built workloads. No title address/asset signatures were added. |
| VDP CD BIOS/PRG/Word RAM sources | The same possible source-span replacement must retain current ownership and the Word RAM delayed VDP-read latch (return prior latch then publish fetched word). Source-start address bins are not proof an entire span is safe. The corrected CD diagnostic estimates 0.550% process CPU for complete VDP DMA+fill jobs with substantial instrumented-run variation; the measured whole-job budget does not warrant a new bulk-read guard or simpler destination specialization. Neither inherently requires a serializer change; it is the demonstrated small budget that selects the decision. The latch, original bus callback and 128KiB wrap remain exact. |
| VDP VRAM/CRAM/VSRAM destination spans | VRAM has odd/even byte-lane and uint16 address wrap with arbitrary increment. CRAM has ordered arbitrary colour callbacks; VSRAM masks/ignores according to its index. The current DMA loop already holds the job and store state; no inter-word service call was found to consolidate. A specialized no-callback VRAM loop is local work inside the already tiny measured whole job. It was evaluated alongside source batching, not retained as an unexamined subsystem boundary. |
| VRAM fill | One trigger write lands normally before the high-byte fill loop, with auto-increment after each. Actual fills are 12/14/7 jobs (RKA/S1/CD); measured loop cost is under 0.001 second in each diagnostic. A memset is only equivalent for qualified increments/lanes and wrap segments, so it offers negligible demonstrated work removal. Trigger/address/state remain original. |
| VRAM copy and overlap | Sequential read/write means overlapping copies can propagate newly written bytes; memcpy/memmove is not an equivalent replacement. Generic non-overlap/contiguous span qualification was considered. All three actual routes contain zero copy jobs, so no unused copy ownership machinery was added. |
| Linear 2M/1M Word RAM | E03 already resolves an entire 16-bit transaction once. Owned contiguous 2M views exist with live ownership/WAIT; 1M words use the physical stride-four interleave and live RET selection. The interleave is a few masks/shifts, not a stored/copied layout. Current CD has 49,920 1M mapping calls. A bulk view would need a new qualified consumer span to amortize itself; ordinary single-word CPU reads cannot legally retain ownership across callbacks/remapping. No redundant linear memory copy exists to remove. |
| Five-band cell layout | Implemented direct bit permutation, eliminating the actual Release variable divide. All 131,072 byte offsets and 65,536 words in each RET bank are checked against independent band/cell coordinates and the old complete bus. Current CD cell-map count is zero, so no current title speedup is attributed to it. |
| Dot/nibble view and priority writes | Dot access selects high/low nibble and applies live transparent/priority behavior, rather than exposing ordinary linear bytes. The complete-word bus already owns the two lanes. There are zero dot-map calls in the current route. A copied bitmap or cached mapped view would add invalidation and remove no measured work; the direct nibble representation remains the qualified contract. |
| Stamp/CDDA and third-master ordering | Stamp uses the physical Word RAM representation at its permitted line deadline. CDDA/PCM sinks and CDD sector callbacks retain their original order and clocks. No bulk transfer crosses them; E07 event ownership is reused. The inactive DMA decision happens after preceding device work, before CDD delivery, with no new delayed callback or hidden completion. |

This resolves the supported current mechanisms and useful demonstrated
replacements. It does not claim that a future consumer, ownership contract or
transfer-heavy workload can never expose more batching.

## Qualification and retained evidence

MSVC and GCC Release each pass 16/16 relevant checks. The new inactive-span
oracle covers 72,478 cases including all phase bytes, carry thresholds,
UINT32_MAX and 65,536 deterministic randomized clocks. It checks the complete
DMA blob against the unchanged public controller and an independent uint64
modulo-six expression. Whole-device comparisons compile the original path
under separate symbols and compare all memory/device state and callback traces
for PCM/PRG/Word RAM, WAIT, protected writes, ring/address wrap, timer/stamp
activity, split clocks, and callback activation/cancellation/status changes.
PCM and CDDA callbacks in that fixture use disabled/stopped chips; active
playback correctness additionally uses the existing PCM/CDDA suites and full
built-game WAV evidence.

The cell oracle enumerates independent band/column/cell-row/pixel coordinates,
checks every byte lane and every word in both banks, proves full domain coverage,
and compares byte/word reads/writes plus whole device state. Counts are 262,144
byte and 131,072 word cases. Existing word-bus/memory suites cover live modes,
protection, aliases, dot priority and noncontiguous-view rejection.
GCC object disassembly retains `div` in the original mapper and none in the
candidate. No host-specific assembly or optional CPU feature was introduced.

Raw tests, full CTest stdout, scripts, object disassembly, capacity reservations
and private fixture identities are retained at
`C:/Temp/Codex/genesis-engine-transfer-spans-20261029` and
`G:/Temp/Codex/genesis-engine-transfer-spans-20261029`.
The first census script's PCM exit-report registration was wrong: its game ran
successfully, but report validation failed. That receipt remains marked failed;
the corrected r2 census and its separately frozen binary are retained. Sampled
controller timing includes fences/counters/preemption and is selection evidence,
not clean runtime cost or an optimization gain.

## Actual built-game result

The supported frozen Sonic CD consumer94f57796, its UIb9ef2f5, the qualified
native archive and compact catalog manifest remain unchanged. Clean A and B
are built from the same production source11a55fd with both qualification
controls0 and1 respectively. Their PE sizes are both115,635,200bytes.
A SHA256 is17c7eea4202b7af6b02690e6db4ffbd0289df4a7fe67478dbf5a1421fe5a7f7f;
B isdf50db311df368201fe71872391ee71134de9eb775bbbf49ba6b52b01204faac.

BAAB runs use one physical Windows host/configuration/capacity lease, dummy
SDL video/audio with full emulated audio, logical CPU14, isolated profiles and
the accepted3000-frame route. All44 checkpoints, capture artifacts and full
WAV hashes match the accepted earlier flow-region reference. Floor/audio
faults and audio underflow/overflow/backwards counts remain zero.

| Arm order | CPU seconds, frames1800–3000 | Process cycles |
| --- | ---: | ---: |
| B | 5.906250 | 28,277,776,540 |
| A | 7.562500 | 34,795,819,730 |
| A | 7.140625 | 32,435,519,680 |
| B | 6.375000 | 29,798,213,380 |

Matched pair observations are21.90/10.72% less processCPU and18.73/8.13% fewer
process cycles. AA drift is-5.58/-6.78% respectively; candidate CPU varies7.94%.
Both pairs improve on this route, with material shared-host variation. This is
observed CPU work reduction at the existing frame pacing, not a universal FPS
or guaranteed percentage claim. Startup wall samples are variable (B4.312/1.475s,
A1.048/0.818s); startup CPU/cycles are mixed/neutral and all samples are retained.
No startup speedup or proven startup regression is inferred from that ambient
sample. There is no new allocation, I/O, media residency or deterministic state.

The final separately retained diagnostic PE9b989172 confirms72,259,107inactive
phase spans replacing the same number of complete DMA-controller calls.
IO spans, activeDMA words(0), stamp spans7,552,953, PCM wave writes124,604 and
samples1,629,702, 1M mappings49,920 and cell/dot mappings(0) match the old census.
That work count is distinct from clean runtime timing. The final diagnostic
build initially stopped before launch because its script reused an existing
retention-directory name. It was resumed from the exact successfully built PE
under a new name; earlier binaries were not overwritten and no game was rerun
because of that pre-launch failure.

RKA and Sonic1 are structurally neutral for E08: their retained source graphs
(122/108translation units) contain no CD translation units; all production
changes are CD-only. Their accepted E06 clean binaries/runtime/current-state/
rollback evidence is reused. The E08 cartridge diagnostic routes supply
transfer eligibility/cost data; they are not mislabeled as clean A/B runs.

Complete raw samples, source/binary/settings/host identities, failure labels,
per-game packages and evidence links are in [engine-transfer-span-results.json](engine-transfer-span-results.json).
Independent acceptance remains pending. Current-build state layout/serialization
and all cartridge source bodies are unchanged. No merge, release, replacement
delivery or new owner-facing launch is authorized by this technical gate.
