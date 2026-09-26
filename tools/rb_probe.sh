#!/usr/bin/env bash
#
# rb_probe.sh -- run the rollback determinism probe (GENESIS_RB_PROBE, see
# runner/rb_probe.c) over a scripted workload and grade it.
#
#   tools/rb_probe.sh <exe> <rom> [frames] [period:depth] [scenario]
#
#   scenario: attract   no input
#             gameplay  START taps through title/menus, then RIGHT + jumps
#                       (no quickstates: a host load inside a probe pass is
#                       not an input and would be reported as a divergence)
#             party4    gameplay with players 2..4 scripted too (adapters with
#                       logical players, e.g. the Sonic 2 party)
#
# Env: RB_PROBE_OUT (default ./rb_probe_out), RB_PROBE_STATICS=1,
#      RB_PROBE_STACKSCAN=1, RB_PROBE_SELFTEST=<tick>, RB_PROBE_ARGS (extra
#      executable arguments), and GENESIS_RB_NEGCTL_SKIP=<section> passes
#      through for a negative control (the probe MUST then report divergences).
#
# Verdict: PASS when every pass is OK, no divergence, no asymmetric restore,
# the mutation self-test (if run) passed, and dispatch_misses.toml has no
# extra functions. Exit status 0 = PASS.
set -u
EXE="${1:?usage: rb_probe.sh <exe> <rom> [frames] [period:depth] [scenario]}"
ROM="${2:?usage}"
FRAMES="${3:-6000}"
PD="${4:-60:8}"
SCEN="${5:-gameplay}"
OUT="${RB_PROBE_OUT:-$PWD/rb_probe_out}"
EXE=$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$EXE" "$OUT/"; cp "$ROM" "$OUT/"
[ -f "$(dirname "$EXE")/annotations_from_disasm.csv" ] && cp "$(dirname "$EXE")/annotations_from_disasm.csv" "$OUT/"
[ -d "$(dirname "$EXE")/assets" ] && cp -r "$(dirname "$EXE")/assets" "$OUT/"
# RB_PROBE_FILES="src[:dest] ...": extra files staged beside the executable
# (e.g. a game's party/roster settings).
for f in ${RB_PROBE_FILES:-}; do cp "${f%%:*}" "$OUT/$( [ "${f#*:}" != "$f" ] && echo "${f#*:}" || basename "${f%%:*}")"; done

script="$OUT/scenario.input"
{
    echo "# rb_probe.sh scenario: $SCEN"
    case "$SCEN" in
    attract) echo "WAIT $FRAMES" ;;
    gameplay|party4)
        echo "WAIT 240"
        for _ in $(seq 12); do echo "PRESS START 4"; echo "WAIT 150"; done
        if [ "$SCEN" = party4 ]; then
            for p in 2 3 4; do echo "PLAYER $p"; echo "HOLD RIGHT"; done
            echo "PLAYER 1"
        fi
        echo "HOLD RIGHT"
        for i in $(seq 60); do
            echo "PRESS C 12"
            if [ "$SCEN" = party4 ]; then
                p=$(( (i % 3) + 2 )); echo "PLAYER $p"; echo "PRESS B 6"; echo "PLAYER 1"
            fi
            echo "WAIT 90"
        done ;;
    *) echo "rb_probe: unknown scenario $SCEN" >&2; exit 2 ;;
    esac
} > "$script"

env_args=(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GENESIS_NO_LAUNCHER=1 GENESIS_RB_PROBE="$PD")
[ -n "${RB_PROBE_STATICS:-}" ] && env_args+=(GENESIS_RB_PROBE_STATICS=1)
[ -n "${RB_PROBE_STACKSCAN:-}" ] && env_args+=(GENESIS_RB_PROBE_STACKSCAN=1)
[ -n "${RB_PROBE_SELFTEST:-}" ] && env_args+=(GENESIS_RB_SELFTEST="$RB_PROBE_SELFTEST")
[ -n "${GENESIS_RB_NEGCTL_SKIP:-}" ] && env_args+=(GENESIS_RB_NEGCTL_SKIP="$GENESIS_RB_NEGCTL_SKIP")

cd "$OUT" || exit 2
# shellcheck disable=SC2086
env -i PATH="$PATH" HOME="$HOME" "${env_args[@]}" "./$(basename "$EXE")" "$(basename "$ROM")" \
    --benchmark "$FRAMES" --input-script scenario.input ${RB_PROBE_ARGS:-} \
    > stdout.log 2> stderr.log
rc=$?

summary=$(grep -m1 'rb_probe: summary' stderr.log)
passes=$(echo "$summary" | grep -oE 'passes=[0-9]+' | cut -d= -f2)
div=$(echo "$summary" | grep -oE 'divergences=[0-9]+' | cut -d= -f2)
asym=$(echo "$summary" | grep -oE 'asymmetric=[0-9]+' | cut -d= -f2)
self=$(grep -m1 '\[rb_selftest\] \(PASS\|FAIL\)' stderr.log)
misses=0
if [ -f dispatch_misses.toml ]; then
    misses=$(python3 - <<'EOF'
import re
t=open("dispatch_misses.toml").read()
m=re.search(r"extra\s*=\s*\[(.*?)\]",t,re.S)
print(0 if not m else len([x for x in re.split(r"[,\n]",m[1]) if x.strip() and not x.strip().startswith("#")]))
EOF
)
fi
echo "exe=$(basename "$EXE") scenario=$SCEN frames=$FRAMES probe=$PD rc=$rc"
echo "summary: ${summary:-<none>}"
[ -n "$self" ] && echo "selftest: $self"
grep -m5 'rb_probe: DIVERGE\|rb_probe: ASYMMETRIC' stderr.log
grep 'rb_probe: statics pass=.*not-restored\|rb_probe: stackscan\|rb_probe: cost\|rb_probe: snapshot bytes' stderr.log | head -12
echo "dispatch_misses extra: $misses"
if [ "$rc" -eq 0 ] && [ -n "$passes" ] && [ "$passes" -gt 0 ] && [ "$div" = 0 ] && [ "$asym" = 0 ] \
   && { [ -z "$self" ] || echo "$self" | grep -q PASS; } && [ "$misses" = 0 ]; then
    echo "PASS"; exit 0
fi
echo "FAIL (logs in $OUT)"; exit 1
