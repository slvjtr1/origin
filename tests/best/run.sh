#!/bin/bash
# tests/best/run.sh <web app dir> [-v] : builds the scoring test (Mac) and compares its picks with the web app's
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cc -std=c11 -D_GNU_SOURCE -DSB_BEST_PURE -Wall -Wextra -Wno-unused-parameter -O1 -g ${SB_ASAN:+-fsanitize=address} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/best/best_test.c" "$ROOT/src/core/best.c" "$ROOT/third_party/cjson/cJSON.c" -lm -o "$ROOT/build/best_test"
node "$ROOT/tests/best/compare.js" "$@"
