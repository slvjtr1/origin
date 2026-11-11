#!/bin/bash
# Builds the torrent engine library the app loads when a torrent is first played:
#   tools/build-engine.sh webos  -> build/webos/libsbengine.so     (ARMv7 soft-float, glibc, libc++ inside)
#   tools/build-engine.sh mac    -> build/mac/libsbengine.dylib    (the Mac preview)
#   tools/build-engine.sh android-arm64 | android-armv7 -> build/android/<abi>/libsbengine.so (Android TV 9 and later, NDK in $ANDROID_NDK)
# Engine: github.com/NuvioMedia/nuvio-engine (GPL-3.0) at a pinned commit, libtorrent 2.0.12, Boost
# 1.86, plus tools/engine/engine.patch (streaming window that frees far pieces from flash, live rate
# limits, verified pieces served from disk in 256 KB reads, no UPnP/NAT-PMP, a window that scales down to
# 24 MB and can change while playing, libtorrent settings by name, payload deleted with its torrent). No OpenSSL: peer encryption works,
# HTTPS trackers do not. Sources and build folders live in $ORIGIN_BUILD/engine.
set -euo pipefail
WMAP=""; if command -v cygpath >/dev/null 2>&1; then WMAP=" -ffile-prefix-map=$(cygpath -m "$HOME")=/src -ffile-prefix-map=$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")=/app"; fi   # (Windows: the compiler sees C:/... paths)
SCRUB() { local py; for py in python python3; do "$py" -c 1 >/dev/null 2>&1 && break; done; "$py" "$(dirname "$0")/scrub_paths.py" "$1" "$HOME" "$(command -v cygpath >/dev/null 2>&1 && cygpath -m "$HOME")"; }   # whatever path still came through (headers outside the maps)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ORIGIN_BUILD="${ORIGIN_BUILD:-$HOME/origin-build}"   # the private build folder outside the repo: toolchain, SDK, sources, keys
TARGET="${1:-webos}"
W="${ENGINE_WORK:-$ORIGIN_BUILD/engine}"; mkdir -p "$W"
ZIG="${ZIG:-$ORIGIN_BUILD/zig-aarch64-macos-0.17.0/zig}"
COMMIT=99a1860
SRC="$W/src"
if [ ! -d "$SRC/.git" ]; then git clone -q https://github.com/NuvioMedia/nuvio-engine "$SRC"; fi
git -C "$SRC" checkout -q "$COMMIT"; git -C "$SRC" reset -q --hard "$COMMIT"; git -C "$SRC" clean -qfd -- src include tests cmake
git -C "$SRC" apply --whitespace=nowarn "$ROOT/tools/engine/engine.patch"
DEPS="${ENGINE_DEPS:-$W/deps}"; mkdir -p "$DEPS"
if [ ! -f "$DEPS/nuvio_boost-src/boost/version.hpp" ]; then
  curl -fsSL -o "$DEPS/boost.tar.bz2" https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2
  echo "1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b  $DEPS/boost.tar.bz2" | { command -v shasum >/dev/null && shasum -a 256 -c - || sha256sum -c -; }
  rm -rf "$DEPS/nuvio_boost-src"; mkdir -p "$DEPS/nuvio_boost-src"; tar -xjf "$DEPS/boost.tar.bz2" -C "$DEPS/nuvio_boost-src" --strip-components 1; rm -f "$DEPS/boost.tar.bz2"
fi
if [ ! -d "$DEPS/nuvio_libtorrent-src/.git" ]; then
  git clone -q https://github.com/arvidn/libtorrent.git "$DEPS/nuvio_libtorrent-src"
  git -C "$DEPS/nuvio_libtorrent-src" checkout -q 740a0b9aeabe00e762cc0efe4a0f27593db2550b
  git -C "$DEPS/nuvio_libtorrent-src" submodule update -q --init deps/try_signal
fi
case "$TARGET" in
  webos) ZT="-target arm-linux-gnueabi.${GLIBC:-2.17} -mcpu=cortex_a9-neon -mfloat-abi=softfp"; OUT="$ROOT/build/webos/libsbengine.so"; SYS=Linux; PROC=arm ;;   # soft-float ABI like the app; this compiler drops NEON from the assembler there, so libtorrent takes its portable paths
  mac)   ZT=""; OUT="$ROOT/build/mac/libsbengine.dylib"; SYS=Darwin; PROC=aarch64 ;;
  android-arm64|android-armv7) ABI=$([ "$TARGET" = android-arm64 ] && echo arm64-v8a || echo armeabi-v7a); OUT="$ROOT/build/android/$ABI/libsbengine.so"; SYS=Android ;;
  *) echo "unknown target $TARGET"; exit 1 ;;
esac
case "$TARGET" in android-*)   # the NDK's own toolchain: Android's libc, libc++ linked in
  NDK="${ANDROID_NDK:?set ANDROID_NDK}"; B="$W/build-$TARGET-28"; FL="-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$ROOT=/app$WMAP"
  [ -f "$B/build.ninja" ] || cmake -S "$SRC" -B "$B" -G Ninja ${CMAKE_MAKE_PROGRAM:+-DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM"} -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON -DNUVIO_ENGINE_BUILD_TESTS=OFF     -DFETCHCONTENT_SOURCE_DIR_NUVIO_BOOST="$DEPS/nuvio_boost-src" -DFETCHCONTENT_SOURCE_DIR_NUVIO_LIBTORRENT="$DEPS/nuvio_libtorrent-src"     -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_static     -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON -DCMAKE_DISABLE_FIND_PACKAGE_GnuTLS=ON -DCMAKE_DISABLE_FIND_PACKAGE_LibGcrypt=ON     -DCMAKE_C_FLAGS="$FL" -DCMAKE_CXX_FLAGS="$FL" >/dev/null
  ninja -j "${ENGINE_JOBS:-4}" -C "$B" nuvio_engine
  CXX=$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/clang++* | head -1); TGT=$([ "$ABI" = arm64-v8a ] && echo aarch64-linux-android28 || echo armv7a-linux-androideabi28)
  mkdir -p "$(dirname "$OUT")"; echo '{ global: sbe_get; local: *; };' > "$B/exports.map"
  "$CXX" --target=$TGT -std=c++20 -Os -fPIC $FL -shared -I"$SRC/include" -I"$ROOT/src" "$ROOT/tools/engine/sbengine.cpp"     "$B/libnuvio_engine.a" "$B/_deps/nuvio_libtorrent-build/libtorrent-rasterbar.a" -static-libstdc++ -Wl,--gc-sections -Wl,--version-script="$B/exports.map" -Wl,--strip-all -Wl,-soname,libsbengine.so -o "$OUT"
  SCRUB "$OUT"; ls -l "$OUT"; exit 0 ;;
esac
TC="$W/tc-$TARGET"; mkdir -p "$TC"
printf '#!/bin/sh\nexec "%s" cc %s "$@"\n' "$ZIG" "$ZT" > "$TC/cc"; printf '#!/bin/sh\nexec "%s" c++ %s "$@"\n' "$ZIG" "$ZT" > "$TC/c++"
printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$TC/ar"; printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$TC/ranlib"
chmod 755 "$TC/cc" "$TC/c++" "$TC/ar" "$TC/ranlib"
TCX="$TC"; X=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*)   # Windows (Git Bash): CMake runs no shell scripts, so .cmd wrappers for it
  ZW="$(cygpath -m "$ZIG")"; TCX="$(cygpath -m "$TC")"; X=.cmd
  printf '@"%s" cc %s %%*
' "$ZW" "$ZT" > "$TC/cc.cmd"; printf '@"%s" c++ %s %%*
' "$ZW" "$ZT" > "$TC/c++.cmd"
  printf '@"%s" ar %%*
' "$ZW" > "$TC/ar.cmd"; printf '@"%s" ranlib %%*
' "$ZW" > "$TC/ranlib.cmd" ;;
esac
cat > "$TC/toolchain.cmake" <<T
set(CMAKE_SYSTEM_NAME $SYS)
set(CMAKE_SYSTEM_PROCESSOR $PROC)
set(CMAKE_C_COMPILER $TCX/cc$X)
set(CMAKE_CXX_COMPILER $TCX/c++$X)
set(CMAKE_AR $TCX/ar$X)
set(CMAKE_RANLIB $TCX/ranlib$X)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
T
B="$W/build-$TARGET"
SSL=(-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=ON); SSLLIB=()
if [ "$TARGET" = mac ]; then OSSL="$(brew --prefix openssl@3)"; SSL=(-DOPENSSL_ROOT_DIR="$OSSL"); SSLLIB=("$OSSL/lib/libssl.a" "$OSSL/lib/libcrypto.a"); export PKG_CONFIG_PATH="$OSSL/lib/pkgconfig"; fi   # the engine needs it on macOS (system trust store)
FL="-ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$ROOT=/app$WMAP"   # no local paths in the library (source names in asserts)
if [ "$TARGET" = mac ]; then mkdir -p "$W/ossl3"; ln -sfn "$OSSL/include/openssl" "$W/ossl3/openssl"; FL="$FL -I$W/ossl3"; fi   # OpenSSL 3 ahead of the compiler's own /opt/homebrew/include (OpenSSL 4 there does not build the pinned libtorrent; the same folder given twice is dropped, hence the link)
[ "$TARGET" = mac ] && [ -n "${ENGINE_MAC_PREBUILT:-}" ] || cmake -S "$SRC" -B "$B" -G Ninja -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON -DNUVIO_ENGINE_BUILD_TESTS=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NUVIO_BOOST="$DEPS/nuvio_boost-src" -DFETCHCONTENT_SOURCE_DIR_NUVIO_LIBTORRENT="$DEPS/nuvio_libtorrent-src" \
  -DCMAKE_TOOLCHAIN_FILE="$TC/toolchain.cmake" -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  "${SSL[@]}" -DCMAKE_DISABLE_FIND_PACKAGE_GnuTLS=ON -DCMAKE_DISABLE_FIND_PACKAGE_LibGcrypt=ON \
  -DCMAKE_C_FLAGS="$FL" -DCMAKE_CXX_FLAGS="$FL" >/dev/null
if [ "$TARGET" = mac ] && [ -n "${ENGINE_MAC_PREBUILT:-}" ]; then B="$ENGINE_MAC_PREBUILT"   # the Mac preview only: archives built earlier (Homebrew's newer OpenSSL no longer compiles the pinned libtorrent)
else ninja -j "${ENGINE_JOBS:-4}" -C "$B" nuvio_engine; fi   # a few jobs: the Mac stays usable
mkdir -p "$(dirname "$OUT")"
LT="$B/_deps/nuvio_libtorrent-build/libtorrent-rasterbar.a"
if [ "$TARGET" = webos ]; then
  echo '{ global: sbe_get; local: *; };' > "$TC/exports.map"
  "$TC/c++" -std=c++20 -Os -fPIC $FL -shared -I"$SRC/include" -I"$ROOT/src" "$ROOT/tools/engine/sbengine.cpp" \
    "$B/libnuvio_engine.a" "$LT" -Wl,--gc-sections -Wl,--version-script="$TC/exports.map" -Wl,--strip-all -Wl,-soname,libsbengine.so -lpthread -o "$OUT"
else
  "$TC/c++" -std=c++20 -Os $FL -shared -I"$SRC/include" -I"$ROOT/src" "$ROOT/tools/engine/sbengine.cpp" \
    "$B/libnuvio_engine.a" "$LT" "${SSLLIB[@]}" -framework Security -framework CoreFoundation -framework SystemConfiguration \
    -Wl,-dead_strip -o "$OUT"
fi
SCRUB "$OUT"; ls -l "$OUT"
