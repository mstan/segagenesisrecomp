# E34: host presentation and deadline ownership

Qualified bounded implementation: engine `81f76825dc17f9235a6ae557093de7f141c76d5a`, base6fefc01. Owner `beads-3z5k.29`, related existing Windows display issue `beads-hj4v`, parent `beads-3z5k`, campaign `beads-2wy8.1`. [Machine results](engine-host-presentation-results.json) link exact source/binary/input/configuration identities, raw measurements and independent review. Final owner combined build/play/merge/release handoff remains open.

## Result and bounded decisions

CD previously presented a completed frame, then performed a coarse sleep. Variable work before the next present therefore moved submissions; a long stall retained overdue deadlines. The presentation owner now schedules the exact896040/53693175 frame ratio immediately before SDL_RenderPresent, after upload/render command preparation. A full-period overrun or pause/menu/debug stop resets cadence rather than maintaining an overdue backlog. Headless execution, native/device clocks, audio generation and source pixels are unchanged.

The common pacer gains an integer rational constructor while preserving the existing cartridge floating-rate path. The common host wait gains a nonspinning park: the optional Windows high-resolution timer waits the full interval, while other targets round upward to an SDL sleep. The existing cartridge low-jitter sleep/spin path stays unchanged. The timer is owned within the process, reused, and closed at shutdown; no machine-wide timer setting changes.

| Original area | Disposition |
| --- | --- |
| CD coarse wait / final delivery ownership | Implemented exact pre-Present deadlines, safe overrun/pause/reset and nonspinning waits. |
| Cartridge deadline and renderer policy | Existing accepted high-resolution deadline/D3D11 work is preserved; it is not claimed as a new gain. Reuse beads-hj4v's prior physical display evidence and explicit remaining compositor limits. |
| CPU view, UI, upload ownership | E16 already supplies direct raw-image borrowing and conditional writable staging. Required UI/capture/raw ownership remains exact; no mutable native image is used as UI scratch and no new duplicate upload is added. |
| Vsync / driver fences | Measure actual present blocking separately. SDL retains renderer command/driver ownership; supported overrides and fallback behavior remain. Renderer capability flags alone do not establish active blocking or physical display cadence. |
| Further GPU resident/high-resolution work | Independently owned E37 remains open. Hidden submission timestamps do not qualify physical display or compositor behavior. |

This resolves the selected original bounded opportunity; it does not assert every future host/GPU optimization impossible.

## Qualification and measurement limits

MSVC and Ubuntu24.04/GCC13.3 each pass3/3 targeted tests: the common pacer, native wait and forced portable wait. Coverage includes three million exact rational recurrences against the original integer scheduler at three counter frequencies, the existing million-frame floating-rate check, full-period overrun, pause/reset, invalid/overflow input, parked deadlines and cleanup. The compiled portable fallback contains no intentional final busy spin. No additional physical-display matrix was repeated.

A matched actual Sonic CD route uses the same qualified native program archive, software SDL presentation, dummy video/audio output with full emulation/WAV, fixed measured core, settings, verified media and inputs. Both original post-Present scheduling and candidate use the same opt-in bounded sampler, recorded only at shutdown. All11 checkpoint dictionaries,21 artifact identities, full WAV and guest/native/audio counters match the qualified E30 final route in both arms. No audio faults or queue under/overflows occurred. Exact binary/source/archive/helper identities are in the linked build record. Actual game runtime is WindowsMSVC19.41; portable tests do not claim Linux game performance.

**SERVER was heavily contended. These are observations, not an isolated improvement estimate or a physical display/FPS result.** The fixed route1800-3000 yields1188 submission intervals after excluding only the first three samples at each predeclared QA pause/resume boundary; no value-based filtering is used.

| Metric | Original | Candidate |
| --- | ---: | ---: |
| Median submission interval | 31.1494ms | 16.79145ms |
| p99 absolute error from16.6881545ms cadence | 151.50925ms | 33.45005ms |
| Maximum submission gap | 396.6332ms | 247.9297ms |
| p99 time inside RenderPresent | 0.0945ms | 0.2267ms |
| CPU seconds, route1800-3000 | 14.546875 | 13.953125 |
| Cycles, same route | 49,553,826,708 | 46,697,909,811 |

CPU is neutral/inconclusive despite the observed4.08% reduction; the single pair cannot isolate that effect from contention. The1200-frame wall windows were48.71s and22.66s, further demonstrating unequal shared-host pressure. Both report software/dummy and renderer flags13; flags are not evidence of physical vsync. All3000 candidate submissions occur at or after their selected deadlines. Native timer use and actual present blocking are recorded. No claim of eliminating all compositor drops, meeting full-speed on this contended host, or changing uncapped throughput is made.

The initial build setup accidentally replaced the existing qualified native-archive include hook and began regenerating unchanged catalog objects. Only the task-owned mistaken build tree was stopped; the correct existing hook was restored before the successful matched build. The qualified10957-source native program archive was reused with exact hashes in both arms. No rejected/partial build is treated as runtime evidence. Failed-build disposable objects are cleaned separately; native archive and final raw evidence are retained for the combined handoff.

Ready [engine PR58](https://github.com/mstan/segagenesisrecomp/pull/58), stacked on PR57. Failed-build object cleanup removed 1565 files / 138936213B; the qualified archive, successful binaries and raw evidence remain.
