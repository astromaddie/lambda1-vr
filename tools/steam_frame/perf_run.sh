#!/usr/bin/env bash
# One unattended performance run: starts the game with force_render for N seconds with the given
# diag keys, then prints the [vr-perf] lines of the run and the Adreno's time at each frequency over it
# (devfreq trans_stat, so the share at the top frequency says if the GPU was saturated).
#
#   tools/steam_frame/perf_run.sh --lock-holder NAME [--seconds 70] [--load SAVE | --map MAP] [--diag K=V ...]
#
# Several runs per setting, and say what the setting was (the first [vr-perf] line has the eye size and the rate):
# a single run can be a compositor state, not the setting (lesson 67).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
frame_host="${FRAME_HOST:-steamos@192.168.1.31}"
seconds=70
holder=""
start=()
diag=()
while (($#)); do
    case "$1" in
        --lock-holder) holder="$2"; shift 2 ;;
        --seconds) seconds="$2"; shift 2 ;;
        --load) start=(--diag "load=$2"); shift 2 ;;
        --map) start=(--diag "map=$2"); shift 2 ;;
        --diag) diag+=(--diag "$2"); shift 2 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done
[[ -n "$holder" ]] || { echo "need --lock-holder" >&2; exit 1; }
gpu='/sys/class/devfreq/3d00000.gpu/trans_stat'
snap() { ssh -o BatchMode=yes "$frame_host" "cat $gpu" 2>/dev/null || true; }

"$root/tools/steam_frame/deploy.sh" --lock-holder "$holder" --no-install --diag-reset --clear-dumps \
    --diag force_render=1 --diag perf=1 "${start[@]}" "${diag[@]}" --diag "exit_seconds=$((seconds))" --no-launch >/dev/null 2>&1
before="$(snap)"
"$root/tools/steam_frame/deploy.sh" --lock-holder "$holder" --no-install --logcat $((seconds + 12)) 2>&1 | grep -E "vr-perf|\[openxr\] (refresh|eye image|frame period)|FORTIFY" || true
after="$(snap)"
python3 - "$before" "$after" <<'PY'
import sys
def rows(t):
    r = {}
    for line in t.splitlines():
        line = line.replace("*", " ")
        if ":" in line and line.strip()[0].isdigit():
            head, _, rest = line.partition(":")
            try:
                r[int(head.strip())] = int(rest.split()[-1])
            except (ValueError, IndexError):
                pass
    return r
b, a = rows(sys.argv[1]), rows(sys.argv[2])
if a and b:
    d = {k: a[k] - b.get(k, 0) for k in a}
    total = sum(d.values()) or 1
    top = max(d)
    print("gpu devfreq time at each frequency over the run (Hz: ms, share):")
    for k in sorted(d):
        if d[k]:
            print("  %d: %d ms, %.1f%%" % (k, d[k], 100.0 * d[k] / total))
    print("gpu at the top frequency %d: %.1f%%" % (top, 100.0 * d[top] / total))
else:
    print("no devfreq trans_stat")
PY
