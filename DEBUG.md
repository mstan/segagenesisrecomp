# DEBUG.md — Always-on rings + TCP commands

This document inventories the runner's observability primitives. The runner
is always-on instrumented; probes connect → query → analyze. **Never
arm-then-record.** See PRINCIPLES.md #17.

The runner exposes a TCP debug server (line-based JSON, default port 4378
for native and 4379 for oracle). All commands respond with a JSON object
keyed to the `id` field of the request.

## Ring buffer inventory

| Ring | File | Capacity | Always-on? | Purpose |
|---|---|---|---|---|
| **bus_ring** | `glue.c:182` | 64 entries | Yes (any build) | Last 64 M68K bus accesses; dumped by watchdog for stall diagnosis. NOT a Tier-1. |
| **frame_record (Tier-2 frame)** | `frame_record.h:177`, `cmd_server.c:288` | 600 frames (~84 MB) | Yes | Per-frame full M68K + Z80 + VDP + FM + PSG + WRAM snapshot + 256-byte game-specific tail packed by `g_game_spec.fill_frame_record`. |
| **reverse_debug Tier-1 (rdb)** | `reverse_debug.c:44` | 1,048,576 entries | Yes (compiled in) | Every WRAM bus write (frame, vint_runcount, addr, val, func, caller). |
| **rdb fire ring (vbla)** | `reverse_debug.c:457` | 65,536 entries | Yes | VBlank fire events with reason classification (THRESHOLD vs SUPPRESSED). |
| **oracle Tier-3 (t3)** | `oracle_trace.c:31` | 1,048,576 entries | Oracle build only | Per-instruction CPU state (pc, frame, D0-D7, A0-A7, SR). |
| **oracle snap ring** | `oracle_trace.c:136` | 64 snapshots | Oracle build only | Periodic full-state oracle snapshots for `rdb_oracle_step_back`. |
| **crash_report block ring** | `crash_report.c:30` | 64 entries | Yes | Recent function entries; dumped on watchdog timeout / fatal trap. |
| **audio event queue** | `audio/event_queue.c:12` | 4096 events | Yes | YM2612 / SN76489 register writes for audio probes. |

**Capacity gaps to fix in Wave 2** (see `humming-wibbling-hammock.md`):
- No Tier-2 *block* ring — frame_record is per-*frame*, not per-block.
  Block-trace ring + monotonic block counter is the headline Wave 2
  unlock.
- Existing rings are stack-allocated as static arrays. Wave 2 moves them
  to heap allocation sized via env var, escaping the Windows PE 2 GB BSS
  ceiling.

## TCP commands

Reference for probe authors. All commands take a JSON request like
`{"id": 1, "cmd": "<name>", ...args}` and return JSON.

### Lifecycle / control
- `ping` — sanity check.
- `pause`, `continue` — suspend / resume the game thread.
- `run_frames N` — single-step N frames forward.

### State queries (current)
- `get_registers` — current M68K + Z80 + cycle counters.
- `read_memory <addr> <len>` — generic 24-bit read.
- `write_memory <addr> <len> <hex>` — generic write (use sparingly; not
  state-history-aware).
- `read_ram <off> <len>` — WRAM read.
- `read_vram <off> <len>`, `read_cram <off> <len>`, `read_vsram` — VDP
  memory reads.
- `read_z80_ram <off> <len>` — Z80 internal RAM.
- `vdp_state`, `fm_state`, `psg_state`, `z80_state` — subsystem snapshots.
- `dump_vram` — full VRAM bin.
- `audio_stats`, `audio_wav` — audio subsystem state.
- `read_joypad_port` — current port latch.

### Frame ring (Tier-2 frame)
- `get_frame <frame_idx>` — single-frame snapshot.
- `frame_info` — current frame index + ring health.
- `frame_range <lo> <hi>` — bulk-fetch a range.
- `frame_timeseries <field> <lo> <hi>` — extract one field across frames
  (e.g., Vint_runcount per wall frame).

### Tier-1 reverse-debug ring
- `rdb_range <addr_lo> <addr_hi>` — set capture filter (legacy; the ring
  is always-on and captures all WRAM writes, but range filters select
  which reach client tools).
- `rdb_dump` — dump current capture window.
- `rdb_count` — entries available.
- `rdb_reset` — clear.
- `rdb_vbla_dump` — dump the VBlank-fire ring.
- `rdb_insn_counts` — per-instruction execution counters (gated).

### Tier 2.5 — breakpoints + step (existing!)
- `rdb_break <pc>` — function-entry breakpoint.
- `rdb_break_clear`, `rdb_break_list` — manage breakpoints.
- `rdb_step`, `rdb_step_over`, `rdb_continue` — step control.
- `rdb_insn_break <pc>` — per-instruction breakpoint.
- `rdb_insn_break_clear`, `rdb_step_insn` — instruction-level stepping.
- `rdb_get_state` — full register dump at break.

(These existed before Wave 0A. The Wave 5 plan to "port NES Tier 2.5"
is mostly a *generalization* of these primitives, not net-new
functionality.)

### Oracle-side (oracle build)
- `t3_range <lo> <hi>`, `t3_reset`, `t3_dump` — Tier-3 instruction trace.
- `rdb_oracle_break <pc>`, `rdb_oracle_break_clear` — oracle-side
  breakpoint.
- `rdb_oracle_step_insn`, `rdb_oracle_continue` — oracle stepping.
- `rdb_oracle_step_back` — reverse-step via oracle snap ring.
- `rdb_oracle_state` — oracle register dump.

### Watch / dispatch
- `watch <addr>`, `unwatch <addr>` — bus-access watchpoint.
- `dispatch_miss_info` — current dispatch-miss log summary.
- `addr_history <addr>` — recent activity at one address.
- `memory_write_log` — memory-write log dump (Tier-1 in human-readable form).

### Coverage / audio
- `coverage_dump` — function-execution coverage map.
- `fm_trace` — FM register-write trace dump.

### Per-game commands
- `g_game_spec.commands[]` is consulted before built-ins fall through to
  the generic dispatcher. Each game registers its own per-game TCP commands
  (see `sonic1_spec.c`, `sonic2_spec.c` `commands[]` arrays).

## Probe authoring rules

1. **Query the ring; do not arm it.** Ranges and filters bias *which
   results reach you*, not *whether the ring captured*. The ring is always
   on.
2. **Tolerate eviction.** Free-running rings advance during your query
   loop. Skip + continue rather than hard-failing.
3. **No printf telemetry on the runner side.** If you need a value the
   probe can't read, extend the ring shape or add a TCP command — never
   `fprintf(stderr, ...)` in a hot path.
4. **No pause/step lockstep across binaries.** For native-vs-oracle work,
   free-run both and use the eventual `oracle_block_diff.py` (Wave 3) or
   the existing `divergence_diff.py` as a fallback.
5. **One runtime per port.** `taskkill //F //IM <exe>` before launch.

## Existing probe inventory

| Location | Notes |
|---|---|
| `SonicTheHedgehogRecomp/tools/` | 60+ Python scripts; mostly Sonic-1-flavored exploration. Many `rdb_*` ring queries. Some are arm-and-record (anti-pattern); audit before reuse. |
| `SonicTheHedgehog2Recomp/tools/` | 8 scripts: `game_state.py`, `quick_status.py`, `ring_filter.py`, `vbla_breakdown.py`, `vint_audit.py`, `divergence_diff.py`, `check_dispatch_misses.py`, `_pause_both.py`. Cleaner; mostly free-running. |
| `segagenesisrecomp/tests/tools/` | Recompiler-side: `gen_disasm_*.py`, `gen_annotations_csv.py`, `gen_l*_fixtures.py`, `_check_toml.py`. |
| `segagenesisrecomp/tools/` | Wave 0A+: shared genesis-agnostic tooling. Currently: `audit_runner_purity.py`, `boot_smoke.py`. Wave 3 will add `oracle_block_diff.py`. |

The Wave 5 plan rationalizes these into a `tools/genesis/` shared root with
per-game subdirectories.

## zone_smoke.py — visual regression harness

`tools/zone_smoke.py` is the visual counterpart to `boot_smoke.py`. Where
boot_smoke hashes WRAM at a single frame (state regression), zone_smoke
hashes the FRAMEBUFFER at many frames during a scripted gameplay run
(visual regression). It's the safety net for codegen / runner changes
that could silently break what the game renders — exactly the class of
bug the CPZ scroll fix surfaced.

The runner exposes `--input-script <path>` (scripted button timeline +
RAM assertions; see `runner/input_script.h`) and `--hash-frames N` (emits
a `[FBHASH] frame=N w=W h=H hash=0xHEX` stderr line every N wall frames).
zone_smoke wires these together: launches the runner, parses the FBHASH
lines, diffs against a checked-in baseline JSON.

Per-game wrappers:

- Sonic 2: `_smoke.bat` at the release repo root.

Manual invocation (from the submodule):

```bash
# First capture (or after an intentional change):
python tools/zone_smoke.py --game sonic2 \
    --input ../../SonicTheHedgehog2Recomp/tools/smoke_enter_level_run_right.input \
    --hash-frames 60 --write-baseline

# Subsequent regression check:
python tools/zone_smoke.py --game sonic2 \
    --input ../../SonicTheHedgehog2Recomp/tools/smoke_enter_level_run_right.input \
    --hash-frames 60
```

Exit codes: `0` match, `1` divergence (visible behaviour changed),
`2` environment / runner error, `3` no baseline yet.

A divergence after a codegen / runner change usually means one of:
1. The change was wrong — investigate the diff (use `--keep-log` to save
   the runner's full stderr).
2. The change is intentional and the baseline needs refreshing — re-run
   with `--write-baseline` AND commit the new baseline alongside the
   code change that justifies it (same discipline as boot_smoke
   baselines per PRINCIPLES.md #23).

Adding new zones / games: drop a new `.input` script next to the
existing ones and capture a baseline. The harness is game-agnostic; only
the `.input` flow and the resulting baseline JSON are per-zone.

## boot_smoke.py — Wave 0B safety net

`tools/boot_smoke.py` captures a deterministic minimal-fields snapshot at
a target frame (default 60) of a running game and either writes a baseline
JSON or compares against an existing one. Snapshot fields: `frame`, m68k
registers (PC, SR, USP, D0-D7, A0-A7), layout-resolved Game_Mode /
Vint_runcount / Vint_routine / PLC_pending (addresses come from the
per-game `[ram_layout]` in `game.toml`, so the script is game-agnostic),
and an FNV1a-64 hash of the full 64KB WRAM.

The runner is launched separately. `boot_smoke` connects, polls
`frame_info` until `current_frame > target`, then queries `get_frame target`
deterministically. Pure client-side — no runner-side changes required.

### Setup

The TCP debug server only enables when `debug.ini` is present next to the
exe. Drop a one-line file (the contents are a comment; existence is what
matters):

```
F:\Projects\segagenesisrecomp-release\SonicTheHedgehogRecomp\build\Release\debug.ini
F:\Projects\segagenesisrecomp-release\SonicTheHedgehog2Recomp\build\Release\debug.ini
```

```ini
# enable debug server (use compiled-in default port: native=4380, oracle=4381)
```

### Usage

```bash
# Capture / write a fresh baseline (after an intentional change).
python tools/boot_smoke.py --game sonic1 --port 4380 --write-baseline

# Default: regression check vs the on-disk baseline.
python tools/boot_smoke.py --game sonic1 --port 4380

# Inspect a divergence by dumping full 64KB WRAM next to the game's dir.
python tools/boot_smoke.py --game sonic1 --port 4380 --dump-on-diff

# Stretch the run to title screen (frame 300) or attract-demo entry (~2000).
python tools/boot_smoke.py --game sonic1 --port 4380 --frames 300
```

Baselines live next to each game's `game.toml`:

- `segagenesisrecomp/sonicthehedgehog/boot_smoke_baseline.json`
- `segagenesisrecomp/sonicthehedgehog2/boot_smoke_baseline.json`

Exit codes: `0` match (or write-baseline OK); `1` divergence vs baseline;
`2` connection / runner / ring-eviction error; `3` no baseline file present.

### When to run

Per PRINCIPLES.md #23, run `boot_smoke` locally before committing changes
that touch shared runner code. Any baseline change must commit alongside
the code change that justifies it -- never alone.

There is no GitHub Actions / cloud CI for this project (the build needs a
ROM that can't be checked in upstream). Discipline is local.

### Observability gotcha: shadow vs authoritative WRAM

Writes to the runner-side `g_ram[]` shadow (declared at `glue.c:51`,
documented as "not authoritative in Step 2") do NOT propagate to
`s_emu->state.m68k.ram[]`, which is what `wram_snapshot()` and the
frame_record's `wram[]` actually read from. Any runner-side write that
must be visible to recompiled code or to probes (boot_smoke included)
must go through `m68k_write8` / `m68k_write16` -- the bus path at
`glue.c:1007/1108/1134`. The shadow is for runner-internal bookkeeping
only.

This bit Wave 0B's landing sanity test: an early poke into `g_ram[0x500]`
showed up nowhere. Switching to `m68k_write8(0xFF8050, 0xCC)` produced
the expected divergence (`wram_fnv1a64: '2c733968d49f2d59' ->
'43416e8a0e4176ad'`).

## Wave roadmap for observability

- **Wave 0A:** docs, audit script, dead-code banner. (Landed.)
- **Wave 0B:** `boot_smoke.py` v1 -- minimal-fields snapshot, FNV1a WRAM
  hash, baseline JSON. (Landed.)
- **Wave 2:** Tier-2 block ring + monotonic `g_block_counter`. Heap-
  allocate existing rings via env var.
- **Wave 2.5 (conditional):** Tier-3 WRAM anchors + `wram_at_block`
  reconstruction.
- **Wave 3:** `oracle_block_diff.py` -- free-running first-divergence
  detection.
- **Wave 5:** Tools rationalization; tier-2.5 generalization; tier-4
  read-trace if needed.

See `humming-wibbling-hammock.md` for the full plan.
