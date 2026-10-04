#!/usr/bin/env bash
# Fill in the assets the repo doesn't track (*.mdl, *.wav, assets/background and
# friends are git-ignored) from Team Beef's own release APK, so a build from source
# gets the same hands, weapons, sounds and loading screens as the Quest release.
#
#   tools/steam_frame/fetch_release_assets.sh
#
# Only files missing from assets/ are written; tracked files are never touched.
# Everything it adds stays git-ignored.
set -euo pipefail

tag="v1.7.4"
apk_name="lambda1vr-v1.7.4.apk"
apk_sha256="23e0d2ecb230ca49a4e23bead213735c3ac6097cf46aa741fac76b184adece48"
url="https://github.com/Team-Beef-Studios/Lambda1VR/releases/download/$tag/$apk_name"

root="$(cd "$(dirname "$0")/../.." && pwd)"
cache="$root/Projects/Android/build/release-assets"
apk="$cache/$apk_name"
mkdir -p "$cache"

if [ ! -f "$apk" ] || ! echo "$apk_sha256  $apk" | shasum -a 256 -c - >/dev/null 2>&1; then
    echo "downloading $url"
    curl -fL --retry 3 -o "$apk.part" "$url"
    echo "$apk_sha256  $apk.part" | shasum -a 256 -c - >/dev/null || { echo "SHA-256 mismatch for $apk_name"; rm -f "$apk.part"; exit 1; }
    mv "$apk.part" "$apk"
fi

stage="$cache/unpacked"
rm -rf "$stage"
mkdir -p "$stage"
unzip -q "$apk" 'assets/*' -d "$stage"

added=0
while IFS= read -r -d '' f; do
    rel="${f#"$stage"/}"
    dest="$root/$rel"
    [ -e "$dest" ] && continue
    if ! git -C "$root" check-ignore -q "$rel"; then
        echo "skipping $rel (not git-ignored, so it should come from the repo)"
        continue
    fi
    mkdir -p "$(dirname "$dest")"
    cp "$f" "$dest"
    added=$((added + 1))
done < <(find "$stage/assets" -type f -print0)
rm -rf "$stage"

echo "added $added files from $apk_name to assets/"
