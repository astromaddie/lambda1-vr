#!/usr/bin/env bash
# Host tests for the Steam Frame code that needs no headset.
#   tools/steam_frame/tests/run_tests.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
vr="$root/Projects/Android/jni/src/Lambda1VR"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# binding tables and action names against the profiles' component lists
python3 "$here/check_bindings.py"

# vr-diag.ini parser and the BMP writer
cc="${CC:-clang}"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$vr" "$here/test_diag_core.c" "$vr/VrDiagCore.c" -lm -o "$out/test_diag_core"
"$out/test_diag_core"

# eye offsets, cant, the HUD shift against a projected point, the layer pose
"$cc" -std=c11 -Wall -Wextra -Werror -I"$vr" "$here/test_eye_math.c" "$vr/VrEyeMath.c" -lm -o "$out/test_eye_math"
"$out/test_eye_math"

# deploy.sh: syntax, and it must refuse to install or launch without the booking
bash -n "$root/tools/steam_frame/deploy.sh"
fake="$out/frame-lock.sh"
printf '#!/bin/sh\necho "HELD by someone-else for 5m0s more: testing (since 10:00)"\n' > "$fake"
chmod +x "$fake"
if FRAME_LOCK="$fake" "$root/tools/steam_frame/deploy.sh" --lock-holder me --apk /nonexistent.apk >/dev/null 2>&1; then
    echo "deploy.sh went on without holding the Frame booking"; exit 1
fi
if FRAME_LOCK="$fake" "$root/tools/steam_frame/deploy.sh" --apk /nonexistent.apk >/dev/null 2>&1; then
    echo "deploy.sh went on without --lock-holder"; exit 1
fi
echo "deploy.sh lock check: ok"
