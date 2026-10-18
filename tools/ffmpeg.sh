#!/usr/bin/env bash
# Builds the small FFmpeg the app links for seek previews decoded on the TV: libavcodec
# with only the H.264 and HEVC decoders, plus libavutil. LGPL 2.1 (no GPL parts enabled);
# the app is open source, so it can be rebuilt against a changed FFmpeg.
#   tools/ffmpeg.sh host  <prefix>    desktop build (tests, smoke runs)
#   tools/ffmpeg.sh webos <prefix>    ARMv7 + NEON with $WEBOS_TOOLCHAIN
# The source is a shallow clone of $FFMPEG_TAG kept in $FFMPEG_SRC (default: next to the prefix).
set -euo pipefail
TARGET="$1"; PREFIX="$(mkdir -p "$2" && cd "$2" && pwd)"
: "${FFMPEG_TAG:=n7.1.1}"
: "${FFMPEG_SRC:=$PREFIX/../ffmpeg-src-$FFMPEG_TAG}"
if [ -f "$PREFIX/lib/libavcodec.a" ] && [ "$(cat "$PREFIX/.tag" 2>/dev/null)" = "$FFMPEG_TAG-$TARGET" ]; then
  echo "ffmpeg: $PREFIX is up to date ($FFMPEG_TAG, $TARGET)"; exit 0
fi
[ -d "$FFMPEG_SRC" ] || git clone -q --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg "$FFMPEG_SRC"

COMMON=(--prefix="$PREFIX" --enable-static --disable-shared --disable-programs --disable-doc --disable-debug
        --disable-everything --disable-autodetect --disable-network --disable-avdevice --disable-avformat
        --disable-avfilter --disable-swresample --disable-swscale --enable-decoder=h264 --enable-decoder=hevc
        --enable-pthreads)
case "$TARGET" in
  host)
    EXTRA=()
    command -v nasm >/dev/null || EXTRA+=(--disable-x86asm)
    ;;
  webos)
    : "${WEBOS_TOOLCHAIN:?set WEBOS_TOOLCHAIN}"
    EXTRA=(--enable-cross-compile --arch=arm --cpu=cortex-a9 --target-os=linux --enable-neon
           --cross-prefix="$WEBOS_TOOLCHAIN/bin/arm-webos-linux-gnueabi-"
           --sysroot="$WEBOS_TOOLCHAIN/arm-webos-linux-gnueabi/sysroot")
    ;;
  *) echo "usage: $0 host|webos <prefix>" >&2; exit 1 ;;
esac
B="$PREFIX/.build"; rm -rf "$B"; mkdir -p "$B"
(cd "$B" && "$FFMPEG_SRC/configure" "${COMMON[@]}" "${EXTRA[@]}" >configure.log 2>&1) || { tail -30 "$B/configure.log"; exit 1; }
make -C "$B" -j"$(nproc)" >"$B/make.log" 2>&1 || { tail -40 "$B/make.log"; exit 1; }
make -C "$B" install >>"$B/make.log" 2>&1
echo "$FFMPEG_TAG-$TARGET" > "$PREFIX/.tag"
rm -rf "$B"
echo "ffmpeg: built $FFMPEG_TAG for $TARGET in $PREFIX ($(du -ch "$PREFIX"/lib/*.a | tail -1 | cut -f1))"
