#!/bin/bash
# tools/shot.sh "<frame:key,...>" <frame> <out.png> : runs the Mac build with scripted keys and saves a screenshot
cd "$(dirname "$0")/.."
SB_ASSETS=assets SB_SKIP_LOGIN=1 SB_STATS=1 SB_SCRIPT="$1" SB_SHOT="$3" SB_SHOT_FRAME="$2" SB_SHOT_EXIT=1 SB_ALARM=60 build/mac/stremioorigin 2>&1 | grep -v "^\[net\]" | grep -v "^\[img\] undecodable" | tail -${4:-4}
