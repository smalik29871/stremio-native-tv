#!/usr/bin/env bash
# Cross-compile for LG webOS (ARMv7) and optionally package/install.
#   tools/webos.sh            build only
#   tools/webos.sh --ipk      build + package build-webos/org.stremio.nativetv_0.1.0_arm.ipk
#   tools/webos.sh --install  build + package + install over ssh to $TV_IP (dev mode / root)
set -euo pipefail
source "$(dirname "$0")/env.sh"
if [ ! -f "$WEBOS_TOOLCHAIN_FILE" ]; then
  echo "webOS toolchain not found at $WEBOS_TOOLCHAIN (set WEBOS_TOOLCHAIN)." >&2
  echo "Download arm-webos-linux-gnueabi_sdk-buildroot-<arch>.tar.gz from openlgtv/buildroot-nc4 releases, extract, run relocate-sdk.sh" >&2
  exit 1
fi
B="$ROOT/build-webos"
FF="${SNTV_FFMPEG_PREFIX:-$B/ffmpeg}"   # seek previews decoded on the TV (built once, then reused)
WEBOS_TOOLCHAIN="$WEBOS_TOOLCHAIN" bash "$ROOT/tools/ffmpeg.sh" webos "$FF"
cmake -S "$ROOT" -B "$B" -DSNTV_PLATFORM=webos -DSNTV_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$WEBOS_TOOLCHAIN_FILE" -DSNTV_TMDB_BUILTIN_KEY="${TMDB_API_KEY:-}" -DSNTV_FFMPEG_PREFIX="$FF"
cmake --build "$B" -j"$(nproc)"
"${WEBOS_TOOLCHAIN}/bin/arm-webos-linux-gnueabi-strip" "$B/stremio-tv" || true
case "${1:-}" in
  --ipk|--install)
    STAGE="$B/ipk-stage"; rm -rf "$STAGE"; mkdir -p "$STAGE"
    cp "$B/stremio-tv" "$STAGE/"
    cp "$ROOT/deploy/webos/appinfo.json" "$STAGE/"
    cp "$ROOT/deploy/webos/"*.png "$STAGE/" 2>/dev/null || true
    cp -r "$ROOT/assets" "$STAGE/"
    bash "$ROOT/tools/package-ipk.sh" "$STAGE" "$B"
    ;;
esac
if [ "${1:-}" = "--install" ]; then
  [ -n "$TV_IP" ] || { echo "set TV_IP" >&2; exit 1; }
  IPK=$(ls "$B"/*.ipk | head -1)
  scp "$IPK" "root@$TV_IP:/tmp/"
  ssh "root@$TV_IP" "luna-send -n 1 -f luna://com.webos.appInstallService/dev/install '{\"id\":\"com.ares.defaultName\",\"ipkUrl\":\"/tmp/$(basename "$IPK")\",\"subscribe\":true}'"
  echo "launch: ssh root@$TV_IP luna-send -n 1 luna://com.webos.applicationManager/launch '{\"id\":\"org.stremio.nativetv\"}'"
fi
