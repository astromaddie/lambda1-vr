#!/usr/bin/env bash
# Install, launch and feed Lambda1VR on a Valve Steam Frame (Android in the Lepton
# container) from a Mac or Linux host. See docs/STEAM_FRAME_VR.md.
#
#   tools/steam_frame/deploy.sh [options]
#
#   --lock-holder NAME  your name in the shared Frame booking (frame-lock.sh). The
#                       script looks at the current holder and refuses to install,
#                       launch or register the title unless it is NAME.
#   --apk FILE          the frame APK (default: the assembleFrame output)
#   --data DIR          rsync a Half-Life "valve" folder (your Steam copy) to the
#                       headset's ~/Documents/Lambda1VR/valve. No lock needed.
#   --register-title    make ~/devkit-game/Lambda1VR a persistent Steam library title
#                       (once). When the title exists every install refreshes its APK.
#   --no-install        skip adb install
#   --no-launch         skip starting the game over adb
#   --logcat SECONDS    after launching, capture SECONDS of the log into
#                       scratchpad/frame-logcat.txt and print the key lines
#   --diag KEY=VAL      set a key in ~/Documents/Lambda1VR/vr-diag.ini (repeatable).
#                       Keys: force_render, autostart, map, diag_yaw, dump_frame,
#                       exit_after, perf, oneshot (see docs/VR_ARCHITECTURE.md)
#   --diag-oneshot      shorthand for --diag oneshot=1: the game renames the file
#                       when it reads it, so the next start is clean
#   --diag-reset        delete vr-diag.ini
#   --clear-dumps       remove ~/Documents/Lambda1VR/vr-dumps before the run
#   --pull-dumps DIR    adb pull vr-dumps into DIR (PNG copies with sips on a Mac)
#   --no-adb            only the SSH steps (--data, --diag, --register-title)
#
# Environment:
#   FRAME_HOST       ssh target (default steamos@192.168.1.31, the Frame on Wi-Fi)
#   FRAME_ADB_HOST   adb host (default: the host part of FRAME_HOST); Lepton
#                    Development publishes its adbd on <host>:5555
#   ADB              adb binary
#   FRAME_LOCK       path of frame-lock.sh
#
# adb needs "Lepton Development" running (start it from the Frame's Steam library).
# It deletes its apps when it exits, the persistent title and the data in
# ~/Documents/Lambda1VR stay. A binary started by adb gets no controllers and no
# focus from SteamVR: use it for logs, and launch the Steam title to play.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
package="com.drbeef.lambda1vr.frame"
activity="com.drbeef.lambda1vr.GLES3JNIActivity"
title="Lambda1VR"                   # Steam devkit titles must be alphanumeric
title_apk="Lambda1VR.apk"           # fixed name: the title's argv stays valid across builds
data_dir_remote="Documents/Lambda1VR"

frame_host="${FRAME_HOST:-steamos@192.168.1.31}"
adb_host="${FRAME_ADB_HOST:-${frame_host#*@}}"
adb_bin="${ADB:-${ANDROID_HOME:-$HOME/Library/Android/sdk}/platform-tools/adb}"
[[ -x "$adb_bin" ]] || adb_bin="$(command -v adb || true)"
frame_lock="${FRAME_LOCK:-$HOME/.claude/skills/steam-frame-vr-port/scripts/frame-lock.sh}"
apk="$root/Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk"
lock_holder=""
data=""
register=0
install=1
launch=1
use_adb=1
logcat_seconds=0
diag=()
diag_reset=0
clear_dumps=0
pull_dumps=""

die() { echo "deploy.sh: $*" >&2; exit 1; }

while (($#)); do
    case "$1" in
        --lock-holder) lock_holder="$2"; shift 2 ;;
        --apk) apk="$2"; shift 2 ;;
        --data) data="$2"; shift 2 ;;
        --register-title) register=1; shift ;;
        --no-install) install=0; shift ;;
        --no-launch) launch=0; shift ;;
        --logcat) logcat_seconds="$2"; shift 2 ;;
        --diag) diag+=("$2"); shift 2 ;;
        --diag-oneshot) diag+=("oneshot=1"); shift ;;
        --diag-reset) diag_reset=1; shift ;;
        --clear-dumps) clear_dumps=1; shift ;;
        --pull-dumps) pull_dumps="$2"; shift 2 ;;
        --no-adb) use_adb=0; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d'; exit 0 ;;
        *) die "unknown option $1" ;;
    esac
done

ssh_frame() { ssh -o ConnectTimeout=10 -o BatchMode=yes "$frame_host" "$@"; }

# ---- The Frame is shared: only the booking holder may install or launch ------------
needs_lock=0
((use_adb && (install || launch))) && needs_lock=1
((register)) && needs_lock=1
if ((needs_lock)); then
    [[ -x "$frame_lock" ]] || die "no frame-lock.sh at $frame_lock (set FRAME_LOCK)"
    status="$("$frame_lock" status | head -n 1)"
    echo "frame-lock: $status"
    if [[ -z "$lock_holder" ]]; then
        die "installing, launching and registering need the Frame booking: claim it with frame-lock.sh and pass --lock-holder <name>"
    fi
    if [[ "$status" =~ ^HELD\ by\ ([^ ]+)\  ]]; then
        holder="${BASH_REMATCH[1]}"
        [[ "$holder" == "$lock_holder" ]] || die "the Frame is booked by '$holder', not '$lock_holder'"
    else
        die "nobody holds the Frame booking ($status); claim it first with frame-lock.sh claim $lock_holder"
    fi
fi

# ---- Game data: persistent storage on the Frame host ----------------------------------
if [[ -n "$data" ]]; then
    [[ -d "$data" ]] || die "no such folder: $data"
    [[ -e "$data/liblist.gam" || -e "$data/pak0.pak" || -e "$data/halflife.wad" ]] \
        || echo "warning: $data doesn't look like a Half-Life valve folder (no liblist.gam, pak0.pak or halflife.wad)" >&2
    ssh_frame "mkdir -p ~/$data_dir_remote/valve && chmod 777 ~/$data_dir_remote ~/$data_dir_remote/valve"
    rsync -a --info=progress2 "${data%/}/" "$frame_host:$data_dir_remote/valve/"
    ssh_frame "chmod -R a+rwX ~/$data_dir_remote/valve"
    echo "game data in place: ~/$data_dir_remote/valve (/sdcard/$data_dir_remote/valve inside Lepton)"
fi

# ---- Unattended-test switches (files in the data folder) ---------------------------
if ((diag_reset)); then
    ssh_frame "rm -f ~/$data_dir_remote/vr-diag.ini ~/$data_dir_remote/vr-diag.ini.last"
    echo "vr-diag.ini removed"
fi
if ((${#diag[@]})); then
    quoted="$(printf '%q ' "${diag[@]}")"
    ssh_frame "mkdir -p ~/$data_dir_remote && python3 - ~/$data_dir_remote/vr-diag.ini $quoted" <<'PY'
import os, sys
path, pairs = sys.argv[1], sys.argv[2:]
lines = open(path).read().splitlines() if os.path.exists(path) else []
for pair in pairs:
    key, _, val = pair.partition("=")
    for i, line in enumerate(lines):
        if line.split("=")[0].strip() == key:
            lines[i] = "%s = %s" % (key, val)
            break
    else:
        lines.append("%s = %s" % (key, val))
open(path, "w").write("\n".join(lines) + "\n")
os.chmod(path, 0o666)
print("vr-diag.ini:", " ".join(lines))
PY
fi
if ((clear_dumps)); then
    ssh_frame "rm -rf ~/$data_dir_remote/vr-dumps"
fi

[[ -f "$apk" ]] || { ((install || register)) && die "no APK at $apk (build it: cd Projects/Android && sh ./gradlew assembleFrame)"; }

# ---- Persistent Steam title ------------------------------------------------------------
# An existing title always gets the new APK; --register-title creates it.
title_exists=0
ssh_frame "test -d ~/devkit-game/$title" 2>/dev/null && title_exists=1
if ((register || (title_exists && install && use_adb))); then
    ssh_frame "mkdir -p ~/devkit-game/$title"
    scp -q "$apk" "$frame_host:devkit-game/$title/$title_apk"
    echo "copied the APK into ~/devkit-game/$title/$title_apk"
fi
if ((register)); then
    parms="{\"gameid\": \"$title\", \"directory\": \"/home/steamos/devkit-game/$title\", \"force_appid\": \"\", \"argv\": [\"$title_apk\"], \"env\": {}, \"settings\": {\"steam_play\": \"1\", \"compat_tool\": \"lepton\"}, \"lepton_args\": \"\"}"
    # needs the Steam client running on the Frame; it answers with success or an error
    ssh_frame "cd ~/devkit-utils && python3 steam-client-create-shortcut --parms '$parms'"
fi

((use_adb)) || exit 0
[[ -n "$adb_bin" && -x "$adb_bin" ]] || die "adb not found; set ADB"

# ---- Lepton Development over network adb -----------------------------------------------
"$adb_bin" start-server >/dev/null
for attempt in 1 2 3 4 5; do
    "$adb_bin" connect "$adb_host:5555" >/dev/null 2>&1 || true
    if "$adb_bin" -s "$adb_host:5555" shell true 2>/dev/null; then break; fi
    sleep $((attempt * 2))
    if ((attempt == 5)); then
        die 'cannot reach Lepton Development at '"$adb_host"':5555. Start "Lepton Development" from the Steam library on the Frame.'
    fi
done
device="$adb_host:5555"
adb() { "$adb_bin" -s "$device" "$@"; }

if ((install)); then
    adb install -r -g "$apk" | grep -i -E 'success|fail|error' || true
    # Lepton doesn't enforce all-files access for Documents, but grant it if it's asked for
    adb shell appops set "$package" MANAGE_EXTERNAL_STORAGE allow >/dev/null 2>&1 || true
fi

if ((launch)); then
    # A running game of ours is restarted; nothing else is touched.
    adb shell am force-stop "$package"
    adb logcat -c
    adb shell am start -n "$package/$activity" >/dev/null
    echo "started $package; follow it with: $adb_bin -s $device logcat -s TBXR Lambda1VR"
fi

if ((logcat_seconds > 0)); then
    out="$root/scratchpad/frame-logcat.txt"
    mkdir -p "$(dirname "$out")"
    timeout_bin="$(command -v timeout || command -v gtimeout || true)"
    if [[ -n "$timeout_bin" ]]; then
        "$timeout_bin" "$logcat_seconds" "$adb_bin" -s "$device" logcat -v time >"$out" || true
    else
        ("$adb_bin" -s "$device" logcat -v time >"$out" & pid=$!; sleep "$logcat_seconds"; kill "$pid") 2>/dev/null || true
    fi
    echo "log: $out"
    grep -E "\[openxr\]|\[vr-perf\]|\[vr-dump\]|\[vr-diag\]|Lambda1VR|AndroidRuntime|FATAL" "$out" | head -120 || true
fi

if [[ -n "$pull_dumps" ]]; then
    mkdir -p "$pull_dumps"
    "$adb_bin" -s "$device" pull "/sdcard/$data_dir_remote/vr-dumps/." "$pull_dumps" >/dev/null
    if command -v sips >/dev/null; then
        for bmp in "$pull_dumps"/*.bmp; do
            [[ -f "$bmp" ]] && sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null
        done
    fi
    echo "dumps in $pull_dumps: $(ls "$pull_dumps" | tr '\n' ' ')"
fi
