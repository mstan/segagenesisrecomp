# Windows host stall observer

This standalone development tool measures actual frame-progress gaps. It does
not participate in simulation, change inputs or modify target RAM. Its build is
independent of game targets; other platforms omit the Windows executable.

Build with CMake, then run:

```text
host_stall_observer PID FRAME_RVA SECONDS OUTPUT_PREFIX read-only TAG_RVA
```

`FRAME_RVA` identifies a 32-bit progress counter in the exact executable's linker
map. The optional `TAG_RVA` identifies one context byte, such as a game-mode code.
The observer resolves the loaded module base to handle ASLR. The Sonic 2 Python
wrapper validates the live executable hash and map timestamp before attaching.

The observer attempts a sample every 5 ms using a private high-resolution timer,
with a normal timer/Sleep fallback. Actual sample times and the largest observer
gap are retained. A frame counter unchanged for at least 50 ms produces an event.
CPU time/cycles, process I/O, page faults, working set, available physical memory
and system CPU counters distinguish busy work from waiting. Page-fault counts
include soft faults; they do not establish hard paging. No samples/events are
written to disk until observation ends. Runtime is bounded to 600 seconds.

`owned-child-stacks` additionally copies thread context and resident stack pages
while the caller's automated child is briefly paused, then resumes it before any
symbol lookup or unwind. Nonresident stack pages are skipped. Native call chains
may be truncated, especially across fibers. Each event records `probe_pause_us`;
subtract this from its duration for a conservative unpaused lower bound. Symbol
work may delay the observer, so inspect the sampling gaps. The interactive attach
wrapper never enables this mode or requests thread-suspend access.

Outputs are a bounded CSV sample timeline and JSON stall events. The Sonic 2
wrapper adds classifications, map-based function labels and a binary identity
manifest. `fixture.c` provides known waiting and CPU stalls for qualification;
Sonic 2's `run_host_stall_observer.py` verifies both classifications, native
caller names and the zero-suspension behavior of read-only observation.
