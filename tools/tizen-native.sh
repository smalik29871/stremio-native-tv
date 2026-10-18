#!/usr/bin/env bash
# Samsung Tizen native .tpk (Tizen 4.0-9). Requires Tizen Studio with the TV
# native rootstrap and a Samsung certificate profile; Apps2Samsung can also sign
# an unsigned package for the TV's DUID. SDL2/GLES2/capi-media-player come from
# the rootstrap. Unlike the WebAssembly build this has no web engine underneath.
set -euo pipefail
source "$(dirname "$0")/env.sh"
TIZEN="$TIZEN_STUDIO/tools/ide/bin/tizen"
[ -x "$TIZEN" ] || { echo "Tizen Studio CLI not found at $TIZEN" >&2; exit 1; }
B="$ROOT/build-tizen"; rm -rf "$B"; mkdir -p "$B/src" "$B/inc" "$B/res" "$B/shared/res"
# Tizen's CLI expects a project folder; mirror sources into it with a flat layout.
cp -r "$ROOT/src" "$ROOT/vendor" "$B/"
cp "$ROOT/deploy/tizen-native/tizen-manifest.xml" "$B/"
cp -r "$ROOT/assets" "$B/res/"
cat > "$B/project_def.prop" <<PROP
APPNAME = stremio-tv
type = app
profile = $TIZEN_NATIVE_PROFILE
USER_SRCS = src/*.c src/addon/*.c src/ui/*.c src/video/video_tizen.c vendor/cJSON.c
USER_DEFS = SNTV_PLATFORM_TIZEN=1
USER_INC_DIRS = src vendor
USER_CFLAGS = -std=gnu99 -O2
USER_LIBS = SDL2 GLESv2 EGL dl pthread capi-media-player
PROP
"$TIZEN" build-native -a arm -c gcc -C Release -- "$B"
"$TIZEN" package -t tpk -- "$B/Release" ${TIZEN_SIGN_PROFILE:+-s "$TIZEN_SIGN_PROFILE"}
echo "tpk in $B/Release"
