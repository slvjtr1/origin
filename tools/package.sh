#!/bin/bash
# Builds the installable IPK of the native app into dist/ (tools/build.sh webos first when the binary is missing).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPID="${APPID:-tv.stremioorigin.app}"; TITLE="${TITLE:-ORIGIN}"   # APPID=tv.stremioorigin.app TITLE="Origin C" tools/package.sh: a second app beside the original
BIN="$ROOT/build/webos/stremioorigin"
APPID="$APPID" "$ROOT/tools/build.sh" webos
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/app/assets"
sed -e "s/\"id\": \"tv.stremioorigin.app\"/\"id\": \"$APPID\"/" -e "s/\"title\": \"ORIGIN\"/\"title\": \"$TITLE\"/" "$ROOT/pkg/appinfo.json" > "$STAGE/app/appinfo.json"
cp "$BIN" "$STAGE/app/stremioorigin"; chmod 755 "$STAGE/app/stremioorigin"
cp "$ROOT/build/webos/libsbengine.so" "$ROOT/build/webos/libsbdts.so" "$STAGE/app/"
if ! grep -aq sbd_prev_get "$STAGE/app/libsbdts.so" && [ "${SBD_PREVIEW:-1}" != 0 ]; then
  echo "Rebuild libsbdts.so with seek previews using tools/build-ffmpeg.sh webos" >&2
  exit 1
fi
cp "$ROOT"/assets/{icon.png,icon_classic.png,mark_diamond.png,mark_play.png,logo.png,splashicon.png,splashbg.jpg,cacert.pem,whatsnew.json} "$ROOT"/assets/icons*.json "$STAGE/app/assets/"
cp "$ROOT"/pkg/icon.png "$ROOT"/pkg/largeIcon.png "$ROOT"/pkg/bgImage.png "$ROOT"/pkg/imageForRecents.png "$STAGE/app/" # (bgImage / imageForRecents: the launcher's enlarged and recent tiles; made by tools/launcher_art.py)
cp -R "$ROOT/assets/fonts" "$ROOT/assets/sounds" "$ROOT/assets/guide" "$STAGE/app/assets/"
[ -d "$ROOT/third_party/licenses" ] && cp -R "$ROOT/third_party/licenses" "$STAGE/app/licenses" || true
# nothing personal may travel in the package (build paths are not kept: the binary is stripped)
if grep -rl -e "$HOME" -e "$(basename "$HOME")@" "$STAGE" >/dev/null 2>&1; then echo "personal path still in the package:"; grep -rl -e "$HOME" "$STAGE"; exit 1; fi
mkdir -p "$ROOT/dist"
ares-package -n -o "$ROOT/dist" "$STAGE/app"
IPK="$ROOT/dist/${APPID}_$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$STAGE/app/appinfo.json")_arm.ipk"
python3 "$ROOT/tools/scrub_ipk.py" "$IPK" # the archives would record the build machine's user and group
ls -l "$ROOT"/dist/*.ipk
