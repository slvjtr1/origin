#!/bin/bash
# A release in one step:  tools/release.sh VERSION
# Sets the version (pkg/appinfo.json and the What's new card, which must match), builds the IPK
# (tools/package.sh) with its checksum (dist/<id>_<version>_arm.ipk and .sha256: the assets of the GitHub
# release v<version>), and writes the site's Homebrew Channel repository into docs/ (GitHub Pages):
#   icon.png, apps.json + api/apps.json (the list), api/apps/<id>/manifest.json (+ releases/latest.json,
#   full_description.html). The app checks the GitHub releases first and this manifest second (src/core/update.c);
#   the manifest's notes are the What's new card's items.
# With ORIGIN_SIGNING set (a properties file kept outside the repository, ports/androidtv/NOTES.md), the Android TV
# APK too: dist/app.stremioorigin.tv_<version>.apk and .sha256, the name the Android TV app's updater looks for.
# No git, no push: review, then commit, push and make the release yourself. The version must go up every time.
# BETA=1: a beta, for LG webOS only: the IPK alone (publish it as a GitHub pre-release); the site's Homebrew listing
# and the Android TV app are left as they are, so only those who turned on Settings › About › Beta updates get it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:?usage: tools/release.sh VERSION}"
ID=tv.stremioorigin.app
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must look like 1.0.1"; exit 1; }

PY=; for c in python3 python; do if "$c" -c 1 >/dev/null 2>&1; then PY=$c; break; fi; done; [ -n "$PY" ] || { echo "python needed"; exit 1; }
"$PY" - "$ROOT" "$VER" <<'EOF'
import re, sys
root, ver = sys.argv[1], sys.argv[2]
for f in ('pkg/appinfo.json', 'assets/whatsnew.json'): # the version alone, the files as they are written
    p = root + '/' + f; t = open(p, encoding='utf-8').read(); n = re.subn(r'("version"\s*:\s*")[^"]*(")', r'\g<1>' + ver + r'\g<2>', t, count=1)
    if n[1] != 1: sys.exit('no version in ' + f)
    open(p, 'w', encoding='utf-8', newline='\n').write(n[0])
EOF

"$ROOT/tools/build.sh" webos # always built now: the version just set is the one inside, and its debug build is kept below
rm -f "$ROOT/dist/${ID}_${VER}_arm.ipk"
"$ROOT/tools/package.sh" >/dev/null
IPK="$ROOT/dist/${ID}_${VER}_arm.ipk"
[ -f "$IPK" ] || { echo "no $IPK"; exit 1; }
SHA=$("$PY" -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$IPK")
( cd "$ROOT/dist" && printf '%s *%s\n' "$SHA" "$(basename "$IPK")" > "$(basename "$IPK").sha256" )
mkdir -p "$ROOT/dist/dbg/$VER" && { cp "$ROOT/build/webos/stremioorigin.dbg" "$ROOT/dist/dbg/$VER/" 2>/dev/null || true; } # kept per version: a shared crash report names its code from it (getlogs)
# ---- the site: the Homebrew Channel repository and the update manifest ----------------------------------------
if [ -z "${BETA:-}" ]; then
SITE="$ROOT/docs"; mkdir -p "$SITE"
cp "$ROOT/pkg/largeIcon.png" "$SITE/icon.png"
"$PY" - "$SITE" "$ID" "$VER" "$SHA" "$ROOT/assets/whatsnew.json" <<'EOF'
import json, os, sys
site, pid, ver, sha, wn = sys.argv[1:]
base = 'https://slvjtr1.github.io/origin'; src = 'https://github.com/slvjtr1/origin'
items = json.load(open(wn, encoding='utf-8')).get('items', [])
notes = [('%s: %s' % (i['title'], i['text'])) if isinstance(i, dict) else str(i) for i in items][:5]
manifest = {
  'id': pid, 'version': ver, 'type': 'native', 'title': 'Origin',
  'appDescription': 'Your own Stremio account and addons on your LG TV, in English or Arabic: artwork and full-screen trailers, the best stream in one press, styled subtitles, clean search. Unofficial.',
  'iconUri': base + '/icon.png', 'sourceUrl': src, 'rootRequired': False,
  'ipkUrl': '%s/releases/download/v%s/%s_%s_arm.ipk' % (src, ver, pid, ver), 'ipkHash': {'sha256': sha}, 'notes': notes}
d = os.path.join(site, 'api', 'apps', pid); os.makedirs(os.path.join(d, 'releases'), exist_ok=True)
for f in (os.path.join(d, 'manifest.json'), os.path.join(d, 'releases', 'latest.json')):
    open(f, 'w', encoding='utf-8', newline='\n').write(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
listing = {'paging': {'page': 1, 'count': 1, 'maxPage': 1, 'itemsTotal': 1}, 'packages': [{
  'id': pid, 'title': 'Origin', 'iconUri': base + '/icon.png', 'pool': 'main',
  'manifestUrl': '%s/api/apps/%s/manifest.json' % (base, pid), 'shortDescription': 'Native app for Stremio addons on LG webOS TVs. Unofficial.',
  'fullDescriptionUrl': 'apps/%s/full_description.html' % pid, 'manifest': manifest}]}
open(os.path.join(site, 'api', 'apps.json'), 'w', encoding='utf-8', newline='\n').write(json.dumps(listing, indent=2, ensure_ascii=False) + '\n')
listing['packages'][0]['fullDescriptionUrl'] = 'api/' + listing['packages'][0]['fullDescriptionUrl'] # the same list at the root: paths from there
open(os.path.join(site, 'apps.json'), 'w', encoding='utf-8', newline='\n').write(json.dumps(listing, indent=2, ensure_ascii=False) + '\n')
open(os.path.join(d, 'full_description.html'), 'w', encoding='utf-8', newline='\n').write(
  '<p><b>Origin</b>: a native app for Stremio addons on LG webOS TVs (ARM), in English or Arabic.</p>\n'
  '<p>Bring your own Stremio account and addons: the app ships empty and plays only the links your addons return.</p>\n'
  '<ul>' + ''.join('<li>%s</li>' % n.replace('&', '&amp;').replace('<', '&lt;') for n in notes) + '</ul>\n'
  '<p>Unofficial: not affiliated with Stremio. Updates install from inside the app (Settings › About).</p>\n'
  '<p>Site: <a href="%s/">%s</a> · Source: <a href="%s">%s</a></p>\n' % (base, base, src, src))
print('site updated:', pid, ver)
EOF
else echo "== beta: the site's Homebrew listing left as it is"; fi
ls -l "$IPK"; cat "$IPK.sha256"
# ---- Android TV ---------------------------------------------------------------------------------
if [ -z "${BETA:-}" ] && [ -n "${ORIGIN_SIGNING:-}" ] && [ -x "$ROOT/ports/androidtv/gradlew" ]; then
  APK="$ROOT/dist/app.stremioorigin.tv_${VER}.apk"; rm -f "$APK" "$APK.sha256"
  for a in arm64 armv7; do abi=$([ $a = arm64 ] && echo arm64-v8a || echo armeabi-v7a); [ -f "$ROOT/build/android/$abi/libsbengine.so" ] || "$ROOT/tools/build-engine.sh" android-$a; done
  ( cd "$ROOT/ports/androidtv" && [ -d deps/SDL ] || ./fetch-deps.sh )
  ( cd "$ROOT/ports/androidtv" && ./gradlew -q assembleRelease --console=plain )
  cp "$ROOT/ports/androidtv/app/build/outputs/apk/release/app-release.apk" "$APK"
  SHA=$("$PY" -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$APK")
  ( cd "$ROOT/dist" && printf '%s *%s
' "$SHA" "$(basename "$APK")" > "$(basename "$APK").sha256" )
  cp "$APK" "$ROOT/dist/Origin-AndroidTV.apk"   # the same file under one name for every version: releases/latest/download/Origin-AndroidTV.apk (the Downloader code points there)
  ls -l "$APK"; cat "$APK.sha256"
else echo "== Android TV app skipped (ORIGIN_SIGNING not set)"; fi
