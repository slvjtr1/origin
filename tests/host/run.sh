#!/bin/bash
# tests/host/run.sh : builds and runs the host tests of net.c (a stand-in libcurl, allocations made to fail),
# usbcheck.c, p2pdirs.c and memctl.c. CC (default cc) and HOST_CFLAGS (extra flags: include paths for SDL2 and
# curl headers, a compatibility header on a non-Linux host) come from the environment; SB_ASAN=1 / SB_UBSAN=1 add
# the sanitizers where the compiler has them. These run on the computer, not the TV.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; T="$ROOT/tests/host"; B="$ROOT/build/hosttest"; mkdir -p "$B"
CC="${CC:-cc}"; EXE="${EXE:-}"
SAN="${SB_ASAN:+-fsanitize=address} ${SB_UBSAN:+-fsanitize=undefined -fno-sanitize-recover=undefined}"
F="-std=c11 -D_GNU_SOURCE -DSDL_MAIN_HANDLED -O1 -g -Wall -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations -Wno-pointer-sign -I$ROOT/src -I$T -I$ROOT/third_party/cjson ${HOST_CFLAGS:-} $SAN"
LIBS="${HOST_LIBS:--lpthread}"
build() { local out="$1"; shift; echo "== build $out"; $CC $F "$@" -o "$B/$out$EXE" $LIBS; }
# net.c: its allocations through failalloc.h; curl linked (the stand-in), as on the preview builds
$CC $F -DSB_MAC=1 -DCURL_STATICLIB -include "$T/failalloc.h" -c "$ROOT/src/core/net.c" -o "$B/net.o"
build net_test -DCURL_STATICLIB "$T/net_test.c" "$B/net.o" "$ROOT/third_party/cjson/cJSON.c"
build usbcheck_test "$T/usbcheck_test.c" "$ROOT/src/core/usbcheck.c"
build p2pdirs_test "$T/p2pdirs_test.c" "$ROOT/src/core/p2pdirs.c"
build memctl_test -DSB_MEMCTL_TEST "$T/memctl_test.c" "$ROOT/src/core/memctl.c"
W="$B/work"; rm -rf "$W"; mkdir -p "$W"; fail=0
for t in net_test usbcheck_test p2pdirs_test memctl_test; do echo "== run $t"; "$B/$t$EXE" "$W" 2> "$B/$t.log" || { fail=1; echo "   (its log: $B/$t.log)"; }; done
rm -rf "$W"; exit $fail
