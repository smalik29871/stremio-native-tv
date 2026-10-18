#!/usr/bin/env bash
# Samsung Tizen 5.5+ WebAssembly .wgt via Emscripten. Networking uses the
# browser (fetch) so libcurl is not involved; video goes through <video>.
set -euo pipefail
source "$(dirname "$0")/env.sh"
[ -f "$EMSDK/emsdk_env.sh" ] || { echo "emsdk not found at $EMSDK" >&2; exit 1; }
# shellcheck disable=SC1091
source "$EMSDK/emsdk_env.sh" >/dev/null
B="$ROOT/build-wasm"
emcmake cmake -S "$ROOT" -B "$B" -DSNTV_PLATFORM=wasm -DSNTV_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$B" -j"$(nproc)"
WGT="$B/wgt"; rm -rf "$WGT"; mkdir -p "$WGT"
cp "$ROOT/deploy/tizen-wasm/config.xml" "$ROOT/deploy/tizen-wasm/index.html" "$WGT/"
cp "$ROOT/deploy/webos/icon.png" "$WGT/icon.png" 2>/dev/null || true
cp "$B"/stremio-tv.{js,wasm,data} "$WGT/"
(cd "$WGT" && zip -qr "../stremio-tv.wgt" .)
echo "unsigned widget: $B/stremio-tv.wgt (sign with Tizen Studio or install in developer mode)"
