#!/usr/bin/env bash
#
# rb_sweep.sh — run the rollback matrix unattended and print one table.
#
# rb_loopback.sh answers "is this configuration sound?". This answers "which
# configurations have we ever actually tried?" -- latency, loss, ring depth,
# tip runway, a disconnect -- over loopback, which is where anything that can
# be established without a second machine should be established first. Ported
# from n64lle's tools/rb_sweep.sh (itself snesrecomp's); the cells are the
# same so the three engines' tables compare. RB_SWEEP_SEATS=3|4 runs every
# cell with that many processes (rb_loopback.sh RB_LOOPBACK_SEATS).
#
#   tools/rb_sweep.sh <exe> <rom> [seconds-per-cell] [-- <args for both peers>]
#
# It takes a shared lock ONCE for the whole grid
# (`$XDG_RUNTIME_DIR/genesisrecomp-emu.lock`): every run in it -- pre-flight
# and each multi-process cell -- is one instance at a time, and nothing else
# runs between them. RB_SWEEP_NO_LOCK=1 when the caller already holds it.
#
# Exit status is the verdict over the whole grid: 0 = every gating cell and
# every pre-flight check passed.
set -u

if [ -z "${RB_SWEEP_NO_LOCK:-}" ]; then
    exec env RB_SWEEP_NO_LOCK=1 flock "${XDG_RUNTIME_DIR:-/tmp}/genesisrecomp-emu.lock" "$0" "$@"
fi

EXE="${1:?usage: rb_sweep.sh <exe> <rom> [seconds-per-cell] [-- args]}"
ROM="${2:?usage: rb_sweep.sh <exe> <rom> [seconds-per-cell] [-- args]}"
shift 2
SECS=45
[ $# -gt 0 ] && [ "$1" != "--" ] && { SECS="$1"; shift; }
[ $# -gt 0 ] && [ "$1" = "--" ] && shift
EXTRA=("$@")
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="${RB_SWEEP_OUT:-$PWD/rb_sweep}"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
EXE=$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")

fails=0
pre_fails=0
cells=0

# ── pre-flight ────────────────────────────────────────────────────────────
#
# Two single-process checks that need no peer and invalidate everything after
# them when they fail.
#
# 1. The determinism probe (runner/rb_probe.c via tools/rb_probe.sh): save,
#    run, restore, replay, compare partitioned digests per tick, plus the
#    restore-symmetry check and the rb_state mutation self-test.
# 2. The rollback digest's transparency: a run that digests the machine at
#    every tick (GENESIS_RB_DIGEST_EVERY=1) must end on the undigested run's
#    machine (the benchmark state and audio fingerprints).
echo "pre-flight"
RB_PROBE_OUT="$OUT/pre_probe" RB_PROBE_SELFTEST=600 bash "$HERE/rb_probe.sh" "$EXE" "$ROM" 3000 30:8 attract \
    > "$OUT/pre_probe.log" 2>&1
pl=$(grep -m1 '^summary:' "$OUT/pre_probe.log")
if grep -q '^PASS' "$OUT/pre_probe.log"; then
    echo "  determinism probe        PASS  (${pl#summary: rb_probe: summary })"
else
    echo "  determinism probe        FAIL  (${pl:-no summary} -- $OUT/pre_probe.log)"
    pre_fails=$((pre_fails+1))
fi
dig_run() { # name env... : a 1800-tick benchmark in a scratch dir
    local name=$1; shift
    local d="$OUT/pre_$name.run"
    rm -rf "$d"; mkdir -p "$d"; cp "$EXE" "$ROM" "$d/"
    ( cd "$d" && timeout 600 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GENESIS_NO_LAUNCHER=1 "$@" \
        "./$(basename "$EXE")" "$(basename "$ROM")" --benchmark 1800 ) >"$OUT/pre_$name.log" 2>&1
}
dig_run dig_off
dig_run dig_on GENESIS_RB_DIGEST_EVERY=1
m_off=$(grep -oE '"(state|audio_state)_fnv1a64":"[0-9A-F]+"' "$OUT/pre_dig_off.log" | tr '\n' ' ')
m_on=$(grep -oE '"(state|audio_state)_fnv1a64":"[0-9A-F]+"' "$OUT/pre_dig_on.log" | tr '\n' ' ')
if [ -n "$m_off" ] && [ "$m_off" = "$m_on" ]; then
    echo "  rollback digest          PASS  (digested every tick = undigested: $m_off)"
else
    echo "  rollback digest          FAIL  (off: ${m_off:-none} / on: ${m_on:-none})"
    pre_fails=$((pre_fails+1))
fi
echo

# name | env assignments | forced-mispredict interval (default 45)
grid=(
  "baseline                |"
  "rtt 60ms                |RNET_SIM_LATENCY_MS=30 RNET_SIM_JITTER_MS=8"
  "rtt 200ms               |RNET_SIM_LATENCY_MS=100 RNET_SIM_JITTER_MS=25"
  "rtt 300ms               |RNET_SIM_LATENCY_MS=150 RNET_SIM_JITTER_MS=40"
  "loss 2%, fast link      |RNET_SIM_LOSS_PCT=2"
  "loss 5%, fast link      |RNET_SIM_LOSS_PCT=5"
  "loss 2% + rtt 200ms     |RNET_SIM_LATENCY_MS=100 RNET_SIM_JITTER_MS=25 RNET_SIM_LOSS_PCT=2"
  "min ring (depth 16)     |GENESIS_RB_SNAP_DEPTH=16 RNET_SIM_LATENCY_MS=100"
  "deep ring (depth 240)   |GENESIS_RB_SNAP_DEPTH=240 RNET_SIM_LATENCY_MS=100"
  # Every other cell runs the ENGINE's runway (12): rb_loopback.sh passes a
  # knob only when it is set. The "runway" column is read back from each
  # peer's start banner.
  "runway 4 (below rtt)    |GENESIS_RB_TIP_RUNWAY=4 RNET_SIM_LATENCY_MS=100"
  "runway 24 (above rtt)   |GENESIS_RB_TIP_RUNWAY=24 RNET_SIM_LATENCY_MS=100"
  "disconnect mid-match    |RB_LOOPBACK_KILL_AT=20 RNET_SIM_LATENCY_MS=30"
  # The follower's seat-0 pad is scripted (START+B held 6 of every 40
  # ticks), so the scene itself moves (title -> menus -> a level, jumping)
  # and, with a one-way latency above D (8 ticks = 133 ms), the initiator
  # mispredicts it on edges -- organic episodes the scene reads, on top of
  # the injector's.
  "input pattern, rtt 300ms|RB_LOOPBACK_TEST_PAD=40:6:90 RNET_SIM_LATENCY_MS=150 RNET_SIM_JITTER_MS=40"
  # Tip-extend only fires when a late edge lands while tip-hold is still open,
  # which needs the runway to outlast the round trip AND edges arriving faster
  # than one per episode. Non-gating, as on snesrecomp: an injected edge every
  # 6 ticks is far past anything real play produces.
  "STRESS tip-extend       |GENESIS_RB_TIP_RUNWAY=24 RNET_SIM_LATENCY_MS=100 RNET_SIM_JITTER_MS=25|6"
)

printf '%-24s %-9s %2s %5s %4s %4s %4s %3s %4s %5s %6s %10s %4s %5s %11s %11s\n' \
  cell verdict rc Ep Res Ab NACK WD Ext Stall runway 'tiphold' unop Chg 'live p50/99' 'repl p50/99'
printf '%-24s %-9s %2s %5s %4s %4s %4s %3s %4s %5s %6s %10s %4s %5s %11s %11s\n' \
  '' '' '' '' '' '' '' '' '' '' '' 'n/mean' '' '' 'ms' 'ms'
printf '%.0s─' {1..142}; echo

for row in "${grid[@]}"; do
    name="${row%%|*}"; name="${name%"${name##*[![:space:]]}"}"
    rest="${row#*|}"
    envs="${rest%%|*}"
    mis="${rest#*|}"; [ "$mis" = "$rest" ] && mis=45
    [ -n "$mis" ] || mis=45
    slug=$(echo "$name" | tr -c 'a-zA-Z0-9' '_')
    cells=$((cells + 1))
    # shellcheck disable=SC2086
    out=$(env $envs RB_LOOPBACK_SEATS="${RB_SWEEP_SEATS:-2}" RB_LOOPBACK_OUT="$OUT/$slug" \
          bash "$HERE/rb_loopback.sh" "$EXE" "$ROM" "$SECS" "$mis" -- "${EXTRA[@]}" 2>&1)
    rc=$?
    echo "$out" > "$OUT/$slug.verdict.txt"
    verdict=$(echo "$out" | grep -oE '^(PASS|FAIL)[^:]*' | head -1)
    [ -n "$verdict" ] || verdict="NO-RUN"
    case "$name" in
        STRESS*) [ "$rc" -eq 0 ] || verdict="FLAKY" ;;
        *)       [ "$rc" -eq 0 ] || fails=$((fails + 1)) ;;
    esac

    logs="$OUT/$slug"/*.log
    ep=$(cat $logs 2>/dev/null | grep -c 'RESIM episode' || true)
    ab=$(cat $logs 2>/dev/null | grep -c 'RB abort' || true)
    nk=$(cat $logs 2>/dev/null | grep -c 'RB follow refused' || true)
    wd=$(cat $logs 2>/dev/null | grep -c 'timed out waiting' || true)
    ex=$(cat $logs 2>/dev/null | grep -c 'RB tip-extend epoch' || true)
    st=$(cat $logs 2>/dev/null | grep -c 'RB chain stall' || true)
    res=$(echo "$out" | grep -m1 '^ledger' | grep -oE 'residual=-?[0-9]+' | cut -d= -f2)
    rw=$(grep -h -m1 'ROLLBACK start' "$OUT/$slug"/initiator.log "$OUT/$slug"/follower.log \
         2>/dev/null | grep -oE 'tip_runway=[0-9]+' | cut -d= -f2 | sort -u | paste -sd/)
    thn=$(cat $logs 2>/dev/null | grep -c 'tip-hold for' || true)
    thm=$(cat $logs 2>/dev/null | sed -n 's/.*RB tip-hold ended .* held=\([0-9]*\) ticks.*/\1/p' \
          | awk '{s+=$1; n++} END {if (n) printf "%.1f", s/n; else print "-"}')
    uo=$(cat $logs 2>/dev/null | grep -c 'RB drain: correction not opened' || true)
    # field cost, both peers pooled by mean of their p50/p99 (ms)
    chg=$(grep -h '^NETPLAY_DRIVER' $logs 2>/dev/null | grep -oE 'replays_changed=[0-9]+' \
          | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
    lv=$(grep -h '^NETPLAY_FIELDS' $logs 2>/dev/null \
         | sed -n 's/.*live_us p50=\([0-9]*\) p99=\([0-9]*\).*/\1 \2/p' \
         | awk '{a+=$1; b+=$2; n++} END {if (n) printf "%.1f/%.1f", a/n/1000, b/n/1000; else print "-"}')
    rp=$(grep -h '^NETPLAY_FIELDS' $logs 2>/dev/null \
         | sed -n 's/.*replay=\([0-9]*\) replay_us p50=\([0-9]*\) p99=\([0-9]*\).*/\1 \2 \3/p' \
         | awk '$1>0 {a+=$2; b+=$3; n++} END {if (n) printf "%.1f/%.1f", a/n/1000, b/n/1000; else print "-"}')
    printf '%-24s %-9s %2s %5s %4s %4s %4s %3s %4s %5s %6s %10s %4s %5s %11s %11s\n' \
      "$name" "${verdict:0:9}" "$rc" "$ep" "${res:--}" "$ab" "$nk" "$wd" "$ex" "$st" "${rw:--}" \
      "$thn/$thm" "$uo" "$chg" "$lv" "$rp"
done

echo
echo "Ep = RESIM episode lines, both peers (an episode logs once on each side);"
echo "Res = ledger residual (initiated - followed - refused); Ab = aborts; NACK ="
echo "BEGINs refused; WD = stage watchdogs ('timed out waiting'); Ext = tip-extends;"
echo "Stall = advisory chain stalls; tiphold = entries / mean ticks held; unop ="
echo "corrections not opened while draining; Chg = replayed fields, both peers,"
echo "that ended on a different machine than their mispredicted live run (the"
echo "correction was guest-visible); tick cost = mean over the peers."
if [ "$pre_fails" -gt 0 ] && [ "$fails" -gt 0 ]; then
    echo "SWEEP FAIL: $pre_fails pre-flight check(s) and $fails of $cells cells" \
         "failed — logs under $OUT"
elif [ "$pre_fails" -gt 0 ]; then
    echo "SWEEP FAIL: $pre_fails pre-flight check(s) failed; all $cells cells" \
         "clean — logs under $OUT"
elif [ "$fails" -gt 0 ]; then
    echo "SWEEP FAIL: $fails of $cells cells failed — logs under $OUT"
else
    echo "SWEEP PASS: pre-flight clean, $cells cells, every gating cell clean"
fi
exit $(( (fails + pre_fails) > 0 ))
