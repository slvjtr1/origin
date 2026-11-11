#!/bin/bash
# Builds tools/logs-test.c on the Mac and checks the log scrubbing, crypto and sealing against published
# vectors and the web app's own code (tools/logs-check.js, needs node).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; B="$ROOT/build/logs-test.d"; mkdir -p "$B"; TP="$ROOT/third_party/crypto"
for f in sha256 aes sbcrypto ecc; do cc -O2 -w -c "$TP/$f.c" -I"$TP" -o "$B/$f.o"; done   # vendored code: its own warnings are not ours
cc -O2 -std=c11 -D_GNU_SOURCE -Wall -Wextra -I"$ROOT/src" -I"$TP" -o "$ROOT/build/logs-test" "$ROOT/tools/logs-test.c" "$ROOT/src/core/logseal.c" "$B"/*.o -lz
node "$ROOT/tools/logs-check.js" "$ROOT/build/logs-test"
