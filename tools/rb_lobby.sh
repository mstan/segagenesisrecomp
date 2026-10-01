#!/usr/bin/env bash
#
# rb_lobby.sh — N processes, one lobby: create/join a room, launch a rollback
# match from it, drain, soft-return to the waiting room, and (rounds > 1)
# rematch from there -- headless, through the SAME callback table recomp-ui's
# launcher drives (runner/netplay/genesis_host_lobby.c, GENESIS_LOBBY_SELFTEST).
# Ported from n64lle's tools/rb_lobby.sh; N players and the Genesis knobs are
# the additions.
#
#   tools/rb_lobby.sh <exe> <rom> <online|lan> [rounds] [frames] [-- <args for every peer>]
#
#   online  a local recomp-net-server (RB_LOBBY_SERVER=<binary>, required): the
#           host creates a room of RB_LOBBY_PLAYERS seats (2..4), the guests
#           find it in the list and join, all ready, the host starts, the
#           server launches everyone over its UDP input relay.
#   lan     a LAN / Direct IP room (recomp-ui's rnet_lan_* modules, no server):
#           two seats, the delay settled and nothing else.
#   rounds  matches to play (default 1). Every match drains at `frames` ticks
#           (GENESIS_LOBBY_SELFTEST_FRAMES, default 1200), every peer goes back
#           to the room, the room prints what it would show, and the next round
#           readies and starts again: a REMATCH, which boots the machine cold in
#           the same process (genesis_session_reset) with the fresh session id
#           the server hands out.
#
# ROLES. The lobby HOST is session slot 0 (recomp-ui HOST_FIRST) and drives
# controller port 1. The validation injector (GENESIS_RB_FORCE_MISPREDICT,
# RB_LOBBY_MISPREDICT, default 45) runs on GUEST 1, whose corruptions land on
# the host's rows. Every other peer has every validation knob pinned OFF.
#
# Knobs:
#   RB_LOBBY_OUT=dir            logs, per-peer exe copies, screenshots (default ./rb_lobby)
#   RB_LOBBY_SERVER=path        recomp-net-server binary (online)
#   RB_LOBBY_PORTS=ws,relay     the server's WebSocket and input-relay ports (18865,18877)
#   RB_LOBBY_PLAYERS=n          seats (online; default 2)
#   RB_LOBBY_SPECTATORS=n       extra processes that join once the seats are
#                               full: the room's gallery (relay-enforced
#                               read-only); graded like players (same session,
#                               no fork, drained, same confirmed timeline)
#   RB_LOBBY_LAN_PORT=n         the LAN room's port (17790; the guest binds n+1)
#   RB_LOBBY_MISPREDICT=n       guest 1's injector interval (0 = off)
#   RB_LOBBY_GUEST_ENV="K=V .." extra environment for GUEST 1 only (e.g. a
#                               forced boot fork: GENESIS_RB_FORCE_BOOT_FORK=1)
#   RB_LOBBY_EXPECT_REFUSAL=code  grade a refused match instead (e.g.
#                               boot_digest_mismatch): every peer must refuse
#                               with that code and show it in the room
#   RB_LOBBY_FILES="src[:dest]" staged beside every peer's exe (party roster)
#   RB_LOBBY_HOST_ARGS="..."    extra arguments for the host only (e.g. a
#                               --widescreen mode the guests must adopt)
#   RB_LOBBY_THEN_OFFLINE=1     after the last match every peer takes the room's
#                               Offline Play: one more cold boot in the same
#                               process, no netplay, `frames` ticks -- and its
#                               RUN_DONE must equal a fresh process's own run
#   RB_LOBBY_TIMELINE=n         print the confirmed timeline every n ticks (300)
#   RB_LOBBY_WALL=secs          kill everything after this long
#
# Each peer runs from its OWN copy of the executable, so nothing written beside
# an executable (the LAN registry, the name store, saves) is shared.
#
# Verdict per match: every peer launched with the same session id, the boot
# digest agreed, 0 forks, every peer drained ("RB quiesced"), episodes > 0
# when the injector ran, every peer went back to a room. Across matches: a
# fresh session id each time, and every match's tick-0 boot parts and
# confirmed TIMELINE lines equal to the first match's. Exit 0 = all of that.
set -u

EXE="${1:?usage: rb_lobby.sh <exe> <rom> <online|lan> [rounds] [frames] [-- args]}"
ROM="${2:?usage}"
MODE="${3:?usage: mode is online or lan}"
shift 3
ROUNDS=1; FRAMES=1200
[ $# -gt 0 ] && [ "$1" != "--" ] && { ROUNDS="$1"; shift; }
[ $# -gt 0 ] && [ "$1" != "--" ] && { FRAMES="$1"; shift; }
[ $# -gt 0 ] && [ "$1" = "--" ] && shift
EXTRA=("$@")
OUT="${RB_LOBBY_OUT:-$PWD/rb_lobby}"
PLAYERS="${RB_LOBBY_PLAYERS:-2}"
case "$MODE" in online|lan) ;; *) echo "rb_lobby: mode must be online or lan" >&2; exit 2;; esac
[ "$MODE" = lan ] && PLAYERS=2
[ -x "$EXE" ] || { echo "rb_lobby: $EXE is not executable" >&2; exit 2; }
[ -f "$ROM" ] || { echo "rb_lobby: no ROM at $ROM" >&2; exit 2; }
EXE=$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
rm -rf "$OUT"/host "$OUT"/guest* "$OUT"/server "$OUT"/fresh "$OUT"/*.log "$OUT"/*.png
IFS=, read -r WS_PORT RELAY_PORT <<<"${RB_LOBBY_PORTS:-18965,18977}"   # not n64lle's 18865/18877
MISPREDICT="${RB_LOBBY_MISPREDICT:-45}"
WALL="${RB_LOBBY_WALL:-$(( 90 + 60 * ROUNDS + FRAMES * ROUNDS / 20 ))}"

SPECTATORS="${RB_LOBBY_SPECTATORS:-0}"
ROLES=(host)
for ((g = 1; g < PLAYERS; g++)); do ROLES+=("guest$g"); done
for ((g = 1; g <= SPECTATORS; g++)); do ROLES+=("spect$g"); done
stage() { # dir
    mkdir -p "$1"
    cp "$EXE" "$ROM" "$1/"
    [ -f "$(dirname "$EXE")/annotations_from_disasm.csv" ] && cp "$(dirname "$EXE")/annotations_from_disasm.csv" "$1/"
    [ -d "$(dirname "$EXE")/assets" ] && cp -r "$(dirname "$EXE")/assets" "$1/"
    for f in ${RB_LOBBY_FILES:-}; do
        cp "${f%%:*}" "$1/$( [ "${f#*:}" != "$f" ] && echo "${f#*:}" || basename "${f%%:*}")"
    done
}
for role in "${ROLES[@]}"; do stage "$OUT/$role"; done

server_pid=""
common=(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GENESIS_NO_LAUNCHER=1
        GENESIS_NET_TIMELINE_EVERY="${RB_LOBBY_TIMELINE:-300}"
        GENESIS_LOBBY_SELFTEST_ROUNDS="$ROUNDS"
        GENESIS_LOBBY_SELFTEST_FRAMES="$FRAMES"
        GENESIS_LOBBY_SELFTEST_PLAYERS="$PLAYERS"
        GENESIS_LOBBY_SELFTEST_SPECTATORS="$SPECTATORS"
        GENESIS_LOBBY_SELFTEST_LOBBY="genesis-rb-lobby-$$"
        GENESIS_RUN_DONE=1)
[ "${RB_LOBBY_THEN_OFFLINE:-0}" = 1 ] && common+=(GENESIS_LOBBY_SELFTEST_THEN_OFFLINE=1)
if [ "$MODE" = online ]; then
    SERVER="${RB_LOBBY_SERVER:?rb_lobby: online needs RB_LOBBY_SERVER=<recomp-net-server binary>}"
    mkdir -p "$OUT/server"
    (cd "$OUT/server" && exec env BIND_ADDR=127.0.0.1:$WS_PORT \
        INPUT_RELAY_BIND=127.0.0.1:$RELAY_PORT INPUT_RELAY_ADVERTISE_HOST=127.0.0.1 \
        INPUT_RELAY_ALLOW_LOOPBACK=1 RUST_LOG=info "$SERVER") >"$OUT/server.log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 100); do
        (exec 3<>/dev/tcp/127.0.0.1/$WS_PORT) 2>/dev/null && break
        sleep 0.1
    done
    common+=(RNET_LOBBY_URL=ws://127.0.0.1:$WS_PORT)
else
    common+=(GENESIS_LOBBY_SELFTEST_LAN=1 GENESIS_LOBBY_SELFTEST_LAN_PORT="${RB_LOBBY_LAN_PORT:-17890}")
fi

off=(GENESIS_RB_FORCE_MISPREDICT=0 GENESIS_RB_FORCE_FORK=0 GENESIS_RB_FORCE_BOOT_FORK=0
     GENESIS_RB_FORCE_MOD_MISMATCH=0 GENESIS_RB_FORCE_MODSET=)
read -r -a guest_env <<<"${RB_LOBBY_GUEST_ENV:-}"

t0=$SECONDS
declare -A PID
for role in "${ROLES[@]}"; do
    case "$role" in
    host)   knobs=("${off[@]}" GENESIS_LOBBY_SELFTEST=host) ;;
    guest1) knobs=(GENESIS_RB_FORCE_MISPREDICT="$MISPREDICT" ${guest_env[@]+"${guest_env[@]}"}
                   GENESIS_LOBBY_SELFTEST=guest) ;;
    *)      knobs=("${off[@]}" GENESIS_LOBBY_SELFTEST=guest) ;;
    esac
    ra=()
    [ "$role" = host ] && [ -n "${RB_LOBBY_HOST_ARGS:-}" ] && read -r -a ra <<<"$RB_LOBBY_HOST_ARGS"
    (cd "$OUT/$role" && exec env "${common[@]}" "${knobs[@]}" \
        GENESIS_LOBBY_SELFTEST_NAME="$role" GENESIS_SCREENSHOT_AT_EXIT="$OUT/$role.png" \
        "./$(basename "$EXE")" "$(basename "$ROM")" "${EXTRA[@]}" ${ra[@]+"${ra[@]}"}) >"$OUT/$role.log" 2>&1 &
    PID[$role]=$!
    [ "$role" = host ] && sleep 1
    # Spectators join after every player seat is taken, so the room puts
    # them in the gallery.
    [ "$role" = "guest$((PLAYERS - 1))" ] && [ "$SPECTATORS" -gt 0 ] && sleep 4
done

killed=""
alive() { local r; for r in "${ROLES[@]}"; do kill -0 "${PID[$r]}" 2>/dev/null && return 0; done; return 1; }
while alive; do
    if [ $((SECONDS - t0)) -ge "$WALL" ]; then
        for r in "${ROLES[@]}"; do kill -0 "${PID[$r]}" 2>/dev/null && killed="$killed $r"; done
        for r in "${ROLES[@]}"; do kill -9 "${PID[$r]}" 2>/dev/null; done
        break
    fi
    sleep 0.5
done
declare -A RC
for r in "${ROLES[@]}"; do wait "${PID[$r]}"; RC[$r]=$?; done
took=$((SECONDS - t0))
[ -n "$server_pid" ] && { kill "$server_pid" 2>/dev/null; wait "$server_pid" 2>/dev/null; }

per_match() { # role pattern -> count per match
    awk -v pat="$2" '/genesis_netplay: started transport/ {m++} m>0 && $0 ~ pat {c[m]++}
        END {for (i=1;i<=m;i++) printf "%d ", c[i]+0}' "$OUT/$1.log"
}
field() { grep -a "$2" "$OUT/$1.log" | grep -oE "(^| )$3=[^ ]*" | sed "s/^ //; s/^$3=//"; }
rc=0
fail() { echo "FAIL: $*"; rc=1; }
exits=""; for r in "${ROLES[@]}"; do exits="$exits $r=${RC[$r]}"; done
echo "mode=$MODE players=$PLAYERS spectators=$SPECTATORS rounds=$ROUNDS frames=$FRAMES took=${took}s exit$exits${killed:+ KILLED:$killed}"
[ -n "$killed" ] && fail "still running after ${WALL}s:$killed"
for role in "${ROLES[@]}"; do
    echo "== $role"
    grep -a '\[lobby-selftest\]\|genesis_netplay: started transport\|match over\|launch refused\|refusing\|identity build' \
        "$OUT/$role.log" | sed 's/^/   /' | cut -c1-220
    grep -a '^NETPLAY_DRIVER' "$OUT/$role.log" | sed 's/^/   /'
    printf '   per match: episodes=[%s] forks=[%s] quiesced=[%s] boot-mismatch=[%s]\n' \
        "$(per_match "$role" 'RESIM episode')" "$(per_match "$role" 'FORK')" \
        "$(per_match "$role" 'RB quiesced')" "$(per_match "$role" 'BOOT DIGEST MISMATCH')"
done

sids=""
for role in "${ROLES[@]}"; do
    n=$(grep -ac 'genesis_netplay: started transport' "$OUT/$role.log")
    s=$(field "$role" 'started transport' session | tr '\n' ' ')
    echo "== $role matches=$n session ids=[$s]"
    sids="$sids|$s"
done
if [ -n "${RB_LOBBY_EXPECT_REFUSAL:-}" ]; then
    code="$RB_LOBBY_EXPECT_REFUSAL"
    for role in "${ROLES[@]}"; do
        grep -aq "match over ($code)" "$OUT/$role.log" || fail "$role did not refuse with $code"
        grep -aq "back in the room: in_lobby=1 .* last_error=\"$code\"" "$OUT/$role.log" \
            || fail "$role's waiting room does not show last_error=\"$code\""
    done
    [ $rc -eq 0 ] && echo "PASS (refused): every peer refused ($code), soft-returned, and the room shows it"
    echo "logs: $OUT/*.log"
    exit $rc
fi
first=""
for role in "${ROLES[@]}"; do
    n=$(grep -ac 'genesis_netplay: started transport' "$OUT/$role.log")
    [ "$n" -eq "$ROUNDS" ] || fail "$role played $n match(es), expected $ROUNDS"
    s=$(field "$role" 'started transport' session | tr '\n' ' ')
    [ -z "$first" ] && first="$s"
    [ "$s" = "$first" ] || fail "$role launched with different session ids ([$s] vs [$first])"
    for f in $(per_match "$role" 'FORK'); do [ "$f" -eq 0 ] || fail "$role forked"; done
    for q in $(per_match "$role" 'RB quiesced'); do [ "$q" -ge 1 ] || fail "$role did not drain a match"; done
    case "$role" in spect*)
        grep -aq 'SPECTATOR' "$OUT/$role.log" || fail "$role did not launch as a spectator" ;;
    esac
    if [ "$role" = guest1 ] && [ "$MISPREDICT" -gt 0 ] 2>/dev/null; then
        for e in $(per_match "$role" 'RESIM episode'); do [ "$e" -gt 0 ] || fail "$role ran a match with no episodes"; done
    fi
    grep -aq 'match over (' "$OUT/$role.log" && grep -a 'match over (' "$OUT/$role.log" | grep -qv 'peer left or local request' \
        && fail "$role refused a match: $(grep -a 'match over (' "$OUT/$role.log" | grep -v 'peer left' | head -1)"
done
[ "$(echo "$first" | tr ' ' '\n' | sed '/^$/d' | sort | uniq -d | wc -l)" -eq 0 ] \
    || fail "a session id repeated across matches"
boots=$(for r in "${ROLES[@]}"; do grep -a 'RB boot parts' "$OUT/$r.log"; done | sed 's/.*RB boot parts //' | sort -u | wc -l)
echo "== boot parts: $boots distinct line(s) across all match-starts"
grep -a 'RB boot parts' "$OUT/host.log" | head -1 | sed 's/.*RB boot parts /   /' | cut -c1-200
[ "$boots" -eq 1 ] || fail "the tick-0 digests differ between peers or between matches"
tl() { awk '/genesis_netplay: started transport/ {m++} /TIMELINE t=/ {sub(/.*TIMELINE /,""); print m" "$0}' "$OUT/$1.log"; }
for role in "${ROLES[@]}"; do tl "$role" > "$OUT/timeline.$role"; done
for role in "${ROLES[@]}"; do
    [ "$role" = host ] && continue
    join <(awk '{print $1"/"$2" "$3}' "$OUT/timeline.host" | sort) \
         <(awk '{print $1"/"$2" "$3}' "$OUT/timeline.$role" | sort) \
        | awk '$2 != $3 {bad++} END {exit bad>0}' || fail "host and $role confirmed timelines differ"
done
common_ticks=$(awk '{print $2}' "$OUT/timeline.host" | sort | uniq -c | awk -v r="$ROUNDS" '$1 == r' | wc -l)
echo "== timeline: $(wc -l < "$OUT/timeline.host") host checkpoint(s); $common_ticks tick(s) confirmed in every match"
if [ "$ROUNDS" -gt 1 ]; then
    bad=$(awk '{print $2, $3}' "$OUT/timeline.host" | sort | uniq | awk '{print $1}' | uniq -d | wc -l)
    [ "$bad" -eq 0 ] || fail "$bad checkpoint tick(s) hold a different machine in a later match than in the first"
fi
if [ "${RB_LOBBY_THEN_OFFLINE:-0}" = 1 ]; then
    stage "$OUT/fresh"
    (cd "$OUT/fresh" && exec env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GENESIS_NO_LAUNCHER=1 GENESIS_RUN_DONE=1 \
        "./$(basename "$EXE")" "$(basename "$ROM")" --max-frames "$FRAMES" "${EXTRA[@]}") >"$OUT/fresh.log" 2>&1
    ref=$(grep -a '^RUN_DONE' "$OUT/fresh.log" | tail -1)
    echo "== offline after the room vs a fresh process: $ref"
    for role in "${ROLES[@]}"; do
        got=$(grep -a '^RUN_DONE' "$OUT/$role.log" | tail -1)
        grep -aq 'offline Play after' "$OUT/$role.log" || fail "$role never took Offline Play"
        if [ -n "$ref" ] && [ "$got" = "$ref" ]; then echo "   $role: same"
        else echo "   $role: $got"; fail "$role's offline pass after the lobby differs from a fresh process"; fi
    done
fi
miss_total=0
for d in "${ROLES[@]}"; do
    f="$OUT/$d/dispatch_misses.toml"; n=0
    [ -f "$f" ] && n=$(python3 -c "import re,sys;t=open(sys.argv[1]).read();m=re.search(r'extra\s*=\s*\[(.*?)\]',t,re.S);print(0 if not m else len([x for x in re.split(r'[,\n]',m[1]) if x.strip() and not x.strip().startswith('#')]))" "$f")
    miss_total=$((miss_total + n))
done
echo "== dispatch misses (extra functions, all peers) = $miss_total"
[ "$miss_total" -eq 0 ] || fail "$miss_total dispatch miss(es)"
for f in "$OUT"/*.png; do [ -f "$f" ] && echo "screenshot: $f"; done
[ $rc -eq 0 ] && echo "PASS: $ROUNDS match(es), $PLAYERS players, same session id on every peer, fresh each match, one boot digest, 0 forks, every match drained and soft-returned"
echo "logs: $OUT/*.log"
exit $rc
