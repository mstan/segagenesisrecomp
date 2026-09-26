#!/usr/bin/env bash
#
# rb_loopback.sh — multi-process rollback soak for a Genesis port.
#
# Runs the port's executable once per seat -- two by default, three or four
# with RB_LOOPBACK_SEATS -- as the peers of a rollback session over UDP
# loopback (recomp-net's episode driver bound by runner/netplay/
# genesis_netplay_rb.c) and reports what the rollback machinery actually did,
# as an exact ledger counted by role and, per initiator/follower pair, by
# epoch. Ported from n64lle's tools/rb_loopback.sh (itself from snesrecomp's);
# the ledger, role and stop logic are unchanged, the knobs are Genesis names.
#
#   tools/rb_loopback.sh <exe> <rom> [seconds] [force-mispredict-interval] [-- <args for every peer>]
#
#   e.g. tools/rb_loopback.sh build/SonicTheHedgehog2Recomp sonic2.bin 45 45
#
# ROLES. The "initiator" is the peer whose validation injector
# (GENESIS_RB_FORCE_MISPREDICT) runs; every other seat is a "follower" with
# every validation knob pinned off. The initiator sits in SEAT 1: the rows it
# corrupts are the remote seats', and seat 0 drives controller port 1, the
# port every Genesis scene reads (title START, gameplay jump). The bits it
# flips are START|B (0x0090, genesis_netplay_rb.c) -- guest-visible in every
# scene the soak visits -- so a replay that restored nothing cannot pass.
#
# Env passthrough -- each is passed ONLY when set; unset means the engine's own
# default, which is what a player gets:
#   RNET_SIM_LATENCY_MS=N   add N ms one-way on EACH peer's receive path (~2N RTT)
#   RNET_SIM_JITTER_MS=N    uniform +/-N ms around that, reordering allowed
#   RNET_SIM_LOSS_PCT=N     drop N% of arriving datagrams on each side
#   RNET_SIM_SEED=N         make a jittered/lossy run repeat
#   GENESIS_RB_SNAP_DEPTH=N rollback ring depth (16-240)
#   GENESIS_RB_TIP_RUNWAY=N tip-hold quiet window in ticks (0-32)
#   GENESIS_RB_FORCE_FORK=N (initiator only) exercise the fork cap
#   GENESIS_NET_DELAY / GENESIS_NET_PREDICTION   D and P (default 8 / 12, the
#                           SNES and N64 harnesses', so the sweeps compare)
# The effective values are read back from each peer's "ROLLBACK start"
# banner and printed, so a run says what it ran rather than what was asked.
#
# SEATS. RB_LOOPBACK_SEATS=3 or 4 runs that many processes. Seat 1 is still the
# one initiator; seat 0 and seats 2..N-1 are followers. With more than two
# seats seat 0 is the relay (recomp-net's LAN hub, GENESIS_NET_TRANSPORT=hub):
# every other seat dials it and it forwards each datagram to the rest. Logs:
# initiator.log (seat 1), follower.log (seat 0), follower2.log, follower3.log.
#
# THE LEDGER. Every episode one seat opens is answered by EVERY other seat
# exactly once: followed, or refused (a line that names the epoch). It is
# graded per ordered pair (initiator seat -> answering seat) by epoch id (the
# epoch's low three bits are its initiator's seat), so a follower that
# answered one episode twice cannot cover for one that never answered
# another. residual = missing + duplicate answers + answers to an epoch its
# initiator never logged. A missing answer is explained only by a watchdog.
#
# Harness knobs:
#   RB_LOOPBACK_SEATS=N          seats, 2 (default) to 4
#   RB_LOOPBACK_OUT=dir          logs, screenshots (default ./rb_loopback)
#   RB_LOOPBACK_KILL_AT=N        kill the FOLLOWER N seconds in; the verdict becomes
#                                "did the initiator notice, degrade and exit by itself"
#   RB_LOOPBACK_DRAIN_SECS=N     how long the coordinated stop may take (default 30)
#   RB_LOOPBACK_TEST_PAD=spec    scripted input on seat 0 (GENESIS_NET_TEST_PAD,
#                                "<period>[:<hold>[:<mask-hex>]]"): organic
#                                mispredicts the scene can see
#   RB_LOOPBACK_FILES="src[:dest] .."  files staged beside every peer's exe
#   RB_LOOPBACK_SEAT<s>_ARGS="..."     extra arguments for seat s only
#   RB_LOOPBACK_PRESENTATION_MAY_DIFFER="why"  a run that deliberately gives
#                                peers different presentation (e.g. a forced
#                                GPU texture limit): confirmed-state PNGs may
#                                differ; the digests must still agree
#   RB_LOOPBACK_PORTS=a,b[,c,d]  UDP ports of seat 1, seat 0, seat 2, seat 3
#                                (default 9810,9811,9812,9813; see below)
#
# Stopping. At the deadline every peer gets SIGUSR1, which asks the driver to
# DRAIN; the ledger is graded only when every peer logged "RB quiesced".
#
# Exit status is the verdict: 0 = every peer agreed. Logs are left behind.
set -u

EXE="${1:?usage: rb_loopback.sh <exe> <rom> [seconds] [mispredict-interval] [-- args]}"
ROM="${2:?usage: rb_loopback.sh <exe> <rom> [seconds] [mispredict-interval] [-- args]}"
shift 2
SECS=45; MISPREDICT=45
[ $# -gt 0 ] && [ "$1" != "--" ] && { SECS="$1"; shift; }
[ $# -gt 0 ] && [ "$1" != "--" ] && { MISPREDICT="$1"; shift; }
[ $# -gt 0 ] && [ "$1" = "--" ] && shift
EXTRA=("$@")
OUT="${RB_LOOPBACK_OUT:-$PWD/rb_loopback}"
SEATS="${RB_LOOPBACK_SEATS:-2}"
case "$SEATS" in 2|3|4) ;; *) echo "rb_loopback: RB_LOOPBACK_SEATS=$SEATS (2..4)" >&2; exit 2;; esac

[ -x "$EXE" ] || { echo "rb_loopback: $EXE is not executable" >&2; exit 2; }
[ -f "$ROM" ] || { echo "rb_loopback: no ROM at $ROM" >&2; exit 2; }
EXE=$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -rf "$OUT"/initiator.* "$OUT"/follower.* "$OUT"/follower[0-9].*

# role and port of each seat. Seat 1 is the initiator; the port list is in
# the order seat 1, seat 0, seat 2, seat 3, so two seats keep 9700/9701.
role_of() { case "$1" in 1) echo initiator;; 0) echo follower;; *) echo "follower$1";; esac; }
# Genesis defaults 9810..9813, NOT the 9700 block: the NES/SNES/N64
# harnesses on this machine use 9700..9703, and two harnesses sharing ports
# cross-deliver datagrams (measured 2026-09-25: a concurrent NES soak on
# 9700/9701 wedged Genesis runs in pcap_freeze and "peer gone"). A random
# session id per run makes the session drop anything foreign that still
# arrives.
IFS=, read -r -a PORTL <<<"${RB_LOOPBACK_PORTS:-9810,9811,9812,9813}"
SESSION_ID="${RB_LOOPBACK_SESSION_ID:-$(( (RANDOM << 15 | RANDOM) + 2 ))}"
port_of() { case "$1" in 1) echo "${PORTL[0]}";; 0) echo "${PORTL[1]}";; *) echo "${PORTL[$1]:-$((9810 + $1))}";; esac; }
ROLES=()
for ((s = 0; s < SEATS; s++)); do ROLES+=("$(role_of $s)"); done
# initiator first in every table, as before
ORDER=(initiator follower)
for ((s = 2; s < SEATS; s++)); do ORDER+=("$(role_of $s)"); done

common=(SDL_VIDEODRIVER="${RB_LOOPBACK_VIDEO:-dummy}" SDL_AUDIODRIVER=dummy
        GENESIS_NO_LAUNCHER=1
        GENESIS_NETPLAY=1 GENESIS_NET_SLOTS=$SEATS GENESIS_NET_MODE=rollback
        GENESIS_NET_SESSION_ID="$SESSION_ID"
        GENESIS_NET_TIMELINE_EVERY="${GENESIS_NET_TIMELINE_EVERY:-50}"
        GENESIS_SCREENSHOT_AT_TICK="${RB_LOOPBACK_SHOT_TICKS:-600,900,1200}"
        GENESIS_NET_DELAY="${GENESIS_NET_DELAY:-8}"
        GENESIS_NET_PREDICTION="${GENESIS_NET_PREDICTION:-12}")
[ "$SEATS" -gt 2 ] && common+=(GENESIS_NET_TRANSPORT=hub)
# Passed only when set. A default spelled here ("${X:-0}") is a value the
# engine cannot tell from an operator's choice, and 0 is a legal runway.
for knob in RNET_SIM_LATENCY_MS RNET_SIM_JITTER_MS RNET_SIM_LOSS_PCT \
            RNET_SIM_SEED GENESIS_RB_SNAP_DEPTH GENESIS_RB_TIP_RUNWAY; do
    [ -n "${!knob+x}" ] && common+=("$knob=${!knob}")
done

# Every validation knob, pinned OFF for every follower as a GROUP. `env` does
# not clear the caller's environment, so a knob exported for the initiator
# would silently reach the followers too (snesrecomp hit that twice). The
# driver reads GENESIS_RB_<NAME> before RNET_RB_<NAME>, so pinning the alias
# beats a generic name leaking from the parent. Add new knobs HERE.
follower_off=(GENESIS_RB_FORCE_MISPREDICT=0
              GENESIS_RB_FORCE_FORK=0
              GENESIS_RB_FORCE_BOOT_FORK=0
              GENESIS_RB_FORCE_MOD_MISMATCH=0
              GENESIS_RB_FORCE_MODSET=)

role_args() { # role -> per-peer arguments (each peer runs in its own dir)
    printf '%s\n' "$(basename "$ROM")"
}
declare -A PID
for ((s = 0; s < SEATS; s++)); do
    role=${ROLES[$s]}
    dir="$OUT/$role.run"
    rm -rf "$dir"; mkdir -p "$dir"
    cp "$EXE" "$ROM" "$dir/"
    [ -f "$(dirname "$EXE")/annotations_from_disasm.csv" ] && cp "$(dirname "$EXE")/annotations_from_disasm.csv" "$dir/"
    [ -d "$(dirname "$EXE")/assets" ] && cp -r "$(dirname "$EXE")/assets" "$dir/"
    # RB_LOOPBACK_FILES="src[:dest] ...": staged beside EVERY peer's
    # executable (e.g. a party roster: every peer must run the same one, or
    # the config seal refuses the match).
    for f in ${RB_LOOPBACK_FILES:-}; do
        cp "${f%%:*}" "$dir/$( [ "${f#*:}" != "$f" ] && echo "${f#*:}" || basename "${f%%:*}")"
    done
    mapfile -t args < <(role_args "$role")
    # RB_LOOPBACK_SEAT<s>_ARGS="..": extra arguments for seat s only (e.g. an
    # input-only script: that seat's local controller).
    sv="RB_LOOPBACK_SEAT${s}_ARGS"
    [ -n "${!sv:-}" ] && { read -r -a sa <<<"${!sv}"; args+=("${sa[@]}"); }
    bind=127.0.0.1:$(port_of $s)
    # Two seats dial each other; with more, every seat dials seat 0 (the
    # relay), and seat 0's own peer address is unused.
    if [ "$SEATS" -eq 2 ]; then peer=127.0.0.1:$(port_of $((1 - s)))
    elif [ "$s" -eq 0 ]; then peer=
    else peer=127.0.0.1:$(port_of 0); fi
    if [ "$s" -eq 1 ]; then
        knobs=(GENESIS_RB_FORCE_MISPREDICT="$MISPREDICT"
               GENESIS_RB_FORCE_FORK="${GENESIS_RB_FORCE_FORK:-0}")
        [ -n "${RB_LOOPBACK_INITIATOR_ENV:-}" ] && read -r -a extra_i <<<"$RB_LOOPBACK_INITIATOR_ENV" && knobs+=("${extra_i[@]}")
    else
        knobs=("${follower_off[@]}")
        [ "$s" -eq 0 ] && [ -n "${RB_LOOPBACK_TEST_PAD:-}" ] \
            && knobs+=(GENESIS_NET_TEST_PAD="$RB_LOOPBACK_TEST_PAD")
    fi
    ( cd "$dir" && exec env "${common[@]}" GENESIS_NET_SLOT=$s GENESIS_NET_BIND=$bind \
        GENESIS_NET_PEER=$peer GENESIS_SCREENSHOT_AT_EXIT="$OUT/$role.exit.png" \
        "${knobs[@]}" "./$(basename "$EXE")" "${args[@]}" "${EXTRA[@]}" ) >"$OUT/$role.log" 2>&1 &
    PID[$role]=$!
done
alive() { local r; for r in "$@"; do kill -0 "${PID[$r]}" 2>/dev/null && return 0; done; return 1; }

KILL_AT="${RB_LOOPBACK_KILL_AT:-0}"
hard_killed=""
surv_rc=""
if [ "$KILL_AT" -gt 0 ] 2>/dev/null; then
    # Disconnect mid-match: seat 0 (a follower) is killed. The initiator must
    # notice, degrade and leave by itself; what it must NOT do is wedge waiting
    # on a peer that will never answer, which is the failure every stage
    # watchdog exists to prevent.
    sleep "$KILL_AT"
    kill -9 "${PID[follower]}" 2>/dev/null
    echo "rb_loopback: killed the follower (seat 0) at ${KILL_AT}s" >&2
    wait_left=$(( SECS > KILL_AT ? SECS - KILL_AT : 10 ))
    t0=$SECONDS
    while kill -0 "${PID[initiator]}" 2>/dev/null && [ $((SECONDS - t0)) -lt "$wait_left" ]; do sleep 0.2; done
    for r in "${ORDER[@]}"; do
        [ "$r" = follower ] && continue
        if kill -0 "${PID[$r]}" 2>/dev/null; then hard_killed="$hard_killed $r"; kill -9 "${PID[$r]}" 2>/dev/null; fi
    done
    wait "${PID[initiator]}" 2>/dev/null; surv_rc=$?
    wait 2>/dev/null
else
    sleep "$SECS"
    # Coordinated stop: every peer at once, then wait for each to leave by itself.
    DRAIN_SECS="${RB_LOOPBACK_DRAIN_SECS:-30}"
    for r in "${ORDER[@]}"; do kill -USR1 "${PID[$r]}" 2>/dev/null; done
    drain_start=$SECONDS
    while alive "${ORDER[@]}"; do
        [ $((SECONDS - drain_start)) -ge "$DRAIN_SECS" ] && break
        sleep 0.2
    done
    drain_took=$((SECONDS - drain_start))
    for r in "${ORDER[@]}"; do kill -0 "${PID[$r]}" 2>/dev/null && hard_killed="$hard_killed $r"; done
    if [ -n "$hard_killed" ]; then
        echo "rb_loopback: still running ${DRAIN_SECS}s after SIGUSR1:$hard_killed — killed" >&2
        for r in "${ORDER[@]}"; do kill -9 "${PID[$r]}" 2>/dev/null; done
    fi
    wait 2>/dev/null
fi

# grep -c prints 0 AND exits 1 when there is no match, so `|| echo 0` would
# emit the count twice and every later arithmetic test would choke on it.
count() {
    [ -f "$OUT/$1.log" ] || { echo 0; return; }
    grep -c "$2" "$OUT/$1.log" 2>/dev/null || true
}
count_all() { local n=0 r; for r in "${ORDER[@]}"; do n=$(( n + $(count "$r" "$1") )); done; echo $n; }
rc=0
printf '%-11s %4s %8s %7s %6s %6s %6s %8s %6s %5s %6s\n' role seat episodes aborts forks pcap late resim deferd attip stalls
for r in "${ORDER[@]}"; do
    ep=$(count "$r" 'RESIM episode')
    ab=$(count "$r" 'RB abort')
    fk=$(count "$r" 'FORK')
    pc=$(count "$r" 'pcap FREEZE enter')
    lt=$(count "$r" 'forced late row')
    df=$(count "$r" 'RB follow deferred epoch=[0-9]* span=')
    at=$(count "$r" 'RB follow at our live tip')
    stl=$(count "$r" 'RB chain stall')
    rs=$(grep -m1 -o 'resim_ticks=[0-9]*' "$OUT/$r.log" 2>/dev/null | cut -d= -f2)
    seat=$(grep -m1 -oE 'ROLLBACK start slot=[0-9]+' "$OUT/$r.log" 2>/dev/null | grep -oE '[0-9]+$')
    printf '%-11s %4s %8s %7s %6s %6s %6s %8s %6s %5s %6s\n' "$r" "${seat:-?}" "$ep" "$ab" "$fk" "$pc" "$lt" "${rs:-?}" "$df" "$at" "$stl"
    [ "$ep" -gt 0 ] || rc=1          # no episodes means nothing was exercised
done

# What each peer actually ran with, from its own start banner -- not what this
# script asked for. A knob the host rejects falls back to the default, and
# only the banner knows.
cfg_of() {
    grep -m1 'ROLLBACK start' "$OUT/$1.log" 2>/dev/null \
        | grep -oE "(^| )$2=[0-9]+" | head -1 | cut -d= -f2
}
echo
for r in "${ORDER[@]}"; do
    printf 'config %-10s slots=%s tip_runway=%s snap_depth=%s D=%s P=%s transport=%s\n' "$r" \
        "$(cfg_of "$r" slots)" "$(cfg_of "$r" tip_runway)" "$(cfg_of "$r" snap_depth)" \
        "$(cfg_of "$r" D)" "$(cfg_of "$r" P)" \
        "$(grep -m1 -oE 'started transport=[a-z-]+' "$OUT/$r.log" 2>/dev/null | cut -d= -f2)"
done
for r in "${ORDER[@]}"; do
    grep -h '^NETPLAY_FIELDS\|^NETPLAY_DRIVER' "$OUT/$r.log" 2>/dev/null | sed "s/^/$r /"
done

# Tip-hold: how often it was entered and how long it actually held. The
# runway is a ceiling; a peer's COMMIT, a tip-extend or an abort ends the hold
# sooner, and entries alone once graded a stage "present" that never ran.
LOGS=(); for r in "${ORDER[@]}"; do LOGS+=("$OUT/$r.log"); done
th_n=$(count_all 'tip-hold for')
th_stats=$(cat "${LOGS[@]}" 2>/dev/null \
    | sed -n 's/.*RB tip-hold ended .* held=\([0-9]*\) ticks.*/\1/p' \
    | awk '{s+=$1; n++; if ($1>m) m=$1} END {if (n) printf "%d %.1f %d", n, s/n, m; else print "0 0 0"}')
set -- $th_stats
echo "tip-hold   entries=$th_n ended=$1 mean_held=$2 max_held=$3 ticks"
cat "${LOGS[@]}" 2>/dev/null \
    | sed -n 's/.*RB tip-hold ended .* ticks (runway [0-9]*) — \(.*\)$/\1/p' \
    | sort | uniq -c | sort -rn | sed 's/^/           /'

# The stop. Graded before the ledger, because the ledger means nothing unless
# every peer drained: "RB quiesced" on each, no timeout, nobody killed.
qt=$(count_all 'RB quiesce TIMED OUT')
unopened=$(count_all 'RB drain: correction not opened')
drain_ok=1
qline=""
for r in "${ORDER[@]}"; do
    q=$(count "$r" 'RB quiesced'); qline="$qline $r=$q"
    [ "$q" -eq 1 ] || drain_ok=0
done
if [ "${KILL_AT:-0}" -gt 0 ] 2>/dev/null; then
    drain_ok=1   # the disconnect cell grades survival, not a drain
else
    echo "drain     $qline timed_out=$qt not_opened=$unopened took=${drain_took:-?}s${hard_killed:+ killed:$hard_killed}"
    [ "$qt" -eq 0 ] && [ -z "${hard_killed:-}" ] || drain_ok=0
fi

# The ledger, per ordered pair (initiator seat -> answering seat), by epoch.
# Each line: role seat kind epoch, kind = I (opened here), F (followed), R
# (refused). An epoch's initiator seat is its low three bits.
epochs() { # role seat
    sed -n -e 's/.*RESIM episode .* initiator .* epoch=\([0-9]*\).*/I \1/p' \
           -e 's/.*RESIM episode .* follower .* epoch=\([0-9]*\).*/F \1/p' \
           -e 's/.*RB follow refused epoch=\([0-9]*\).*/R \1/p' "$OUT/$1.log" 2>/dev/null \
        | sed "s/^/$2 /"
}
EP=$(for ((s = 0; s < SEATS; s++)); do epochs "${ROLES[$s]}" "$s"; done)
pairs=$(echo "$EP" | awk -v N="$SEATS" '
    NF == 3 {
        seat = $1; kind = $2; e = $3 + 0; init = e % 8
        if (kind == "I") { opened[seat, e] = 1; nopen[seat]++ }
        else { ans[seat, e]++; if (kind == "F") nf[seat]++; else nr[seat]++ }
        if (kind != "I") { key = init SUBSEP seat; answered_ep[init, seat, e] = 1 }
    }
    END {
        for (i = 0; i < N; i++) for (j = 0; j < N; j++) {
            if (i == j) continue
            miss = 0; dup = 0; extra = 0; n = 0
            for (k in opened) {
                split(k, a, SUBSEP); if (a[1] != i) continue
                n++; c = ans[j, a[2]] + 0
                if (c == 0) miss++; else if (c > 1) dup += c - 1
            }
            for (k in answered_ep) {
                split(k, a, SUBSEP); if (a[1] != i || a[2] != j) continue
                if (!((i, a[3]) in opened)) extra++
            }
            printf "pair %d->%d opened=%d missing=%d duplicate=%d unopened=%d\n", i, j, n, miss, dup, extra
            tot_exp += n; tot_bad += miss + dup + extra
        }
        for (s = 0; s < N; s++) { F += nf[s]; R += nr[s] }
        printf "SUM %d %d %d %d\n", tot_exp, F, R, tot_bad
    }')
echo
# "pair", not "ledger": rb_sweep.sh reads the first line starting "ledger"
# for the residual, and that must stay the summary line below.
echo "$pairs" | grep '^pair' | sed 's/^pair /pair       /'
set -- $(echo "$pairs" | grep '^SUM')
ep_expect=$2; ep_f=$3; nack_i=$4; resid=$5
fk_all=$(count_all 'FORK')
timeout_i=$(count_all 'timed out waiting')
ep_i=$(count_all 'RESIM episode.*initiator')
# initiated = answers owed (each opened episode, once per other seat); with
# two seats that is the number of episodes opened, as before.
echo "ledger     initiated=$ep_expect followed=$ep_f refused=$nack_i watchdog=$timeout_i residual=$resid"
# Screenshots: what each peer last presented (the verdict is not taken from
# them, but a claim about what was on screen is -- look at them).
# Screenshots. The *.exit.png files are each peer's LAST frame: peers stop
# at different ticks during the drain, so those are NOT comparable. The
# comparison uses tick-aligned captures (GENESIS_SCREENSHOT_AT_TICK,
# RB_LOOPBACK_SHOT_TICKS): at tick T every peer writes <role>.exit.png.tT.png
# and "SHOT t=T live=<digest of the state it shows>"; the confirmed
# "TIMELINE t=T d=<digest>" says whether that frame showed the agreed state.
# Graded: at every T where each peer's live digest equals the confirmed one,
# every peer's PNG must be byte-identical. A frame that showed a prediction
# (live != confirmed) is reported, not graded -- rollback corrects it later.
shot_fail=0
for t in $(echo "${RB_LOOPBACK_SHOT_TICKS:-600,900,1200}" | tr ',' ' '); do
    have=0; agreed=1; md5s=""
    for r in "${ORDER[@]}"; do
        f="$OUT/$r.exit.png.t$t.png"
        [ -f "$f" ] || continue
        have=$((have + 1))
        live=$(grep -ao "SHOT t=$t live=[0-9a-f]*" "$OUT/$r.log" | head -1 | sed 's/.*live=//')
        conf=$(grep -ao "^TIMELINE t=$t d=[0-9a-f]*" "$OUT/$r.log" | head -1 | sed 's/.*d=//')
        [ -n "$live" ] && [ "$live" = "$conf" ] || agreed=0
        md5s="$md5s $(md5sum < "$f" | cut -c1-8)"
    done
    [ "$have" -eq 0 ] && { echo "screenshot t=$t: not reached"; continue; }
    distinct=$(echo $md5s | tr ' ' '\n' | sort -u | wc -l)
    if [ "$have" -lt "${#ORDER[@]}" ]; then
        echo "screenshot t=$t: only $have of ${#ORDER[@]} peers reached it"
    elif [ "$agreed" -eq 1 ] && [ "$distinct" -eq 1 ]; then
        echo "screenshot t=$t: $have peers, confirmed state, byte-identical ($md5s )"
    elif [ "$agreed" -eq 1 ] && [ -n "${RB_LOOPBACK_PRESENTATION_MAY_DIFFER:-}" ]; then
        echo "screenshot t=$t: $have peers showed the CONFIRMED state; PNGs differ as the run intends ($md5s ) -- $RB_LOOPBACK_PRESENTATION_MAY_DIFFER"
    elif [ "$agreed" -eq 1 ]; then
        echo "screenshot t=$t: $have peers showed the CONFIRMED state but the PNGs differ ($md5s ) -- presentation divergence"
        shot_fail=1
    else
        echo "screenshot t=$t: a peer showed a predicted frame (live != confirmed); not graded ($md5s )"
    fi
done
for r in "${ORDER[@]}"; do
    [ -f "$OUT/$r.exit.png" ] && echo "screenshot $r (end of run, NOT tick-aligned): $OUT/$r.exit.png"
done
# Dispatch misses after every run (project law): each peer's
# dispatch_misses.toml must list no extra function.
miss_total=0
for ((s = 0; s < SEATS; s++)); do
    r=${ROLES[$s]}; f="$OUT/$r.run/dispatch_misses.toml"
    n=0
    [ -f "$f" ] && n=$(python3 -c "import re,sys;t=open(sys.argv[1]).read();m=re.search(r'extra\s*=\s*\[(.*?)\]',t,re.S);print(0 if not m else len([x for x in re.split(r'[,\n]',m[1]) if x.strip() and not x.strip().startswith('#')]))" "$f")
    miss_total=$((miss_total + n))
done
echo "dispatch   misses (extra functions, all peers) = $miss_total"
refused_boot=$(count_all 'BOOT DIGEST MISMATCH\|MOD SETS DIFFER\|MOD SET NOT AGREED')
if [ "$refused_boot" -gt 0 ] && [ "$ep_i" -eq 0 ]; then
    why=$(grep -ohE 'BOOT DIGEST MISMATCH|MOD SETS DIFFER|MOD SET NOT AGREED' "${LOGS[@]}" \
          2>/dev/null | head -1)
    echo "PASS (refused): ${why:-a pre-match check} stopped the match before it" \
         "started — 0 episodes, which is the point"
    rc=0
elif [ "$KILL_AT" -gt 0 ] 2>/dev/null; then
    # The ledger cannot balance against a peer that stopped answering, so the
    # question becomes survival: did the initiator notice the follower was
    # gone, say so, and leave by itself rather than freeze?
    gone=$(count initiator 'peer gone')
    fk_i=$(count initiator 'FORK')
    if [ "$(count initiator 'RESIM episode')" -eq 0 ]; then
        echo "FAIL: initiator opened no episodes before the disconnect"; rc=1
    elif [ "$fk_i" -ne 0 ]; then
        echo "FAIL: initiator forked ($fk_i)"; rc=1
    elif [ -n "$hard_killed" ] || [ "$gone" -eq 0 ]; then
        echo "FAIL: the initiator did not notice the disconnect and leave by itself" \
             "(peer-gone lines $gone${hard_killed:+, killed:$hard_killed})"; rc=1
    else
        echo "PASS (disconnect): initiator ran $(count initiator 'RESIM episode') episodes," \
             "noticed the follower going away and left by itself (exit $surv_rc)"
        rc=0
    fi
elif [ "$miss_total" -ne 0 ]; then
    echo "FAIL: $miss_total dispatch miss(es) -- resolve them before anything else"; rc=1
elif [ "$shot_fail" -ne 0 ]; then
    echo "FAIL: tick-aligned screenshots of the confirmed state differ between peers"; rc=1
elif [ "$fk_all" -ne 0 ]; then
    echo "FAIL: $fk_all fork(s) — the peers disagreed on state"; rc=1
elif [ "$drain_ok" -ne 1 ]; then
    echo "FAIL: the coordinated stop did not complete (quiesced$qline," \
         "timed out $qt${hard_killed:+, killed:$hard_killed}) —" \
         "the ledger cannot be graded"; rc=1
elif [ "$resid" -gt 0 ] && [ "$resid" -le "$timeout_i" ]; then
    echo "PASS (explained): $ep_i episodes, no forks, $resid unaccounted but" \
         "covered by $timeout_i timeout(s) — a lost BEGIN is never answered"
elif [ "$resid" -ne 0 ]; then
    echo "FAIL: $resid episode answer(s) unaccounted for (owed $ep_expect, followed" \
         "$ep_f, refused $nack_i, timeouts $timeout_i) — see the ledger pairs"; rc=1
elif [ "$rc" -ne 0 ]; then
    echo "FAIL: a peer opened no episodes — nothing was exercised"
else
    echo "PASS: $ep_i episodes, no forks, every episode accounted for"
fi
echo "logs: ${LOGS[*]}"
exit $rc
