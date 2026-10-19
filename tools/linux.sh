#!/usr/bin/env bash
# Desktop preview build (UI + networking, no video). Needs libsdl2-dev, libgles-dev.
set -euo pipefail
source "$(dirname "$0")/env.sh"
FF="${SNTV_FFMPEG_PREFIX:-$ROOT/build/ffmpeg}"   # the seek preview decoder (built once)
bash "$ROOT/tools/ffmpeg.sh" host "$FF"
cmake -S "$ROOT" -B "$ROOT/build" -DSNTV_PLATFORM=linux -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" -DSNTV_FFMPEG_PREFIX="$FF"
cmake --build "$ROOT/build" -j"$(nproc)"
if command -v go >/dev/null; then bash "$ROOT/tools/torrentd.sh" host "$ROOT/build/torrentd"; else echo "no Go: the P2P fallback is left out"; fi
(cd "$ROOT/build" && ctest --output-on-failure)
echo "run: $ROOT/build/stremio-tv   (arrows/Enter/Esc; F3 = search)"
