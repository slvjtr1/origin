#!/bin/bash
# The sources the Android build needs beside the repository's own, into ports/android/deps (not committed),
# and SDL's Java activity into the app. Run once, then build with Android Studio or ./gradlew assembleDebug.
set -euo pipefail
H="$(cd "$(dirname "$0")" && pwd)"; D="$H/deps"; mkdir -p "$D"
get() { # name url
  [ -d "$D/$1" ] && return; echo "== $1"; curl -fsSL "$2" -o "$D/$1.tgz"; mkdir -p "$D/$1"; tar -xzf "$D/$1.tgz" -C "$D/$1" --strip-components=1; rm "$D/$1.tgz"; }
get SDL     https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
[ -d "$D/mbedtls" ] || { echo "== mbedtls"; curl -fsSL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2 -o "$D/m.tbz"; mkdir -p "$D/mbedtls"; tar -xjf "$D/m.tbz" -C "$D/mbedtls" --strip-components=1; rm "$D/m.tbz"; } # (the release archive: it carries its framework/ submodule)
get curl    https://github.com/curl/curl/releases/download/curl-8_11_1/curl-8.11.1.tar.gz
J="$H/app/src/main/java/org/libsdl/app"; mkdir -p "$J"; cp "$D"/SDL/android-project/app/src/main/java/org/libsdl/app/*.java "$J/"
echo "deps ready in $D"
