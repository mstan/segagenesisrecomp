# E25: Exact constant/silent PCM and CDDA spans

Original E25 (`beads-3z5k.23`) is qualified in [PR53](https://github.com/mstan/segagenesisrecomp/pull/53), stacked on PR52. This is a source?logical-queue?constant-window implementation. It is a modest observed Sonic CD improvement, with no universal speed/FPS or memory-saving claim. Independent source/ABI and qualification review pass; final source/records/cleanup/durability review precedes E26.

PCM/CDDA producers reserve a compact exact rational constant run, advance all authoritative device state, then commit. Logical samples/timestamps,4096-frame capacity, head/count/accepted/rejected/discarded values and interpolation anchors remain exact. Ordinary frame storage and all unproved/opaque/reentrant/stale/declined paths retain fallback. The source observer expands logical frames; constant windows avoid repeated interpolation and preserve combined-source clipping and exact next/last positions.

PCM proof covers disabled/no-active voices and step-zero voices including loop normalization. Moving muted voices retain their evolving-address kernel. CDDA transport silence, zero gain and proved constant raw input preserve FIFO consumption, fader and future state; anchored underflow emits nothing and does not advance the fader. No per-game addresses or sound-driver knowledge enter the engine.

Cost:32 extra host bytes per queue, an owned-sink capability pointer per source and32 grant stack bytes; original physical storage remains for fallback. No new thread/allocation. Production `782b70903f263d5e1569104bac4c48b1c020e440`.

Five contracts pass on both x64 and Win32. The frozen original mixer and source kernels compare every logical sample/stamp, all nonstorage state and future output, including nonzero PCM/0xff loops, moving muted voices/unmute, constant nonzero CDDA, zero input during fades, FIFO exhaustion/underflow, exact rational windows, mixed clipping, full/wrapped queues, refusal, retirement and reused slots.

| SERVER pair | Baseline CPU/cycles | Candidate CPU/cycles | CPU reduction | Cycle reduction |
|---|---|---|---:|---:|
| AB | 13.75s / 46919987071 | 13.75s / 45867804202 | 0.0000% | 2.2425% |
| BA | 13.84375s / 47667515474 | 13.34375s / 45827096338 | 3.6117% | 3.8610% |

All4 real CD runs match11 frozen checkpoints,21 artifacts/full WAV, and report no guest audio faults. Both constant paths execute: the first candidate has PCM1555347grants/frames and CDDA984144grants/984146frames. Each pair uses the same SERVER/configuration/fixed core; existing core-headroom.json samples are retained and linked in the results. The paced1800?3000 process-cost scope includes identical checkpoint inspection/capture work. Original frozen correctness provenance remains separate from this timing evidence.

Cartridge packages/evidence inherit accepted E24 without an applied E25 CD path; no new E25 cartridge measurement is claimed. No physical-device timing or CD save/load/rollback claim is made.

The supported build rebuilt all live I/O-header consumers through Ninja. Its qualified native archive has10956 M68k sources and one Z80 source using unchanged generic CPU interfaces, independent of SegaCDIO/coordinator layout; no full native archive recompilation is required.

See [identities, raw samples, affinity, limits and retention](engine-source-constant-span-results.json). Audited non-forced cleanup removed365943345bytes of duplicate binaries/build/overlay/temp data. Current title/unit binaries/configs, source and raw evidence remain; shared SERVER caches are retained for the next item and due cleanup at campaign end. No owner launch/merge/release occurred. Combined owner visual/play and merge handoff remains.
