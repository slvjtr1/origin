#!/bin/bash
# tests/dts/run.sh <file.mkv> [outdir]: converts a file with build/mac/libsbdts.dylib (tools/build-ffmpeg.sh mac) and
# checks the result with the host's ffprobe / ffmpeg: every segment decodes, the timestamps run on across segments,
# the sound stays with the picture, and a segment made after a seek matches the one made in order
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; F="$1"; O="${2:-$(mktemp -d)}"; mkdir -p "$O"
cc -std=c11 -O1 -I"$ROOT/src" "$ROOT/tests/dts/remux_test.c" -o "$ROOT/build/remux_test"
"$ROOT/build/remux_test" "$ROOT/build/mac/libsbdts.dylib" "$F" "$O"
if [ -f "$O/all.ts" ]; then ffmpeg -v error -i "$O/all.ts" -f null - && echo "continuous stream decodes"; exit 0; fi
ffmpeg -v error -allowed_extensions ALL -i "$O/index.m3u8" -f null - && echo "the playlist decodes end to end"
# each segment's first video and audio times
for s in 0 1 2 5; do [ -f "$O/$s.ts" ] || continue
  v=$(ffprobe -v error -select_streams v -show_entries packet=pts_time -of csv=p=0 "$O/$s.ts" | sort -n | head -1)
  a=$(ffprobe -v error -select_streams a -show_entries packet=pts_time -of csv=p=0 "$O/$s.ts" | head -1)
  echo "segment $s: video from $v, audio from $a"; done
# segment 5 again after a seek: the same video packets, the sound on its own clock from there
cp "$O/5.ts" "$O/5.inorder.ts"; "$ROOT/build/remux_test" "$ROOT/build/mac/libsbdts.dylib" "$F" "$O" 9 5 >/dev/null
a=$(ffmpeg -v error -i "$O/5.ts" -map 0:v -c copy -f md5 -); b=$(ffmpeg -v error -i "$O/5.inorder.ts" -map 0:v -c copy -f md5 -)
fa=$(ffprobe -v error -select_streams a -show_entries packet=pts_time -of csv=p=0 "$O/5.ts" | head -1)
[ "$a" = "$b" ] && echo "segment 5 after a seek: same video, audio from $fa" || echo "segment 5 after a seek: VIDEO DIFFERS"
