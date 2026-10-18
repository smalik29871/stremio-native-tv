#!/usr/bin/env bash
# Desktop preview build (UI + networking, no video). Needs libsdl2-dev, libgles-dev.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cmake -S "$ROOT" -B "$ROOT/build" -DSNTV_PLATFORM=linux -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
cmake --build "$ROOT/build" -j"$(nproc)"
(cd "$ROOT/build" && ctest --output-on-failure)
echo "run: $ROOT/build/stremio-tv   (arrows/Enter/Esc; F3 = search)"
