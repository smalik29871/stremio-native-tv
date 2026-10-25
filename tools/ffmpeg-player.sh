#!/usr/bin/env bash
# Builds the FFmpeg the desktop player links (src/video/video_ffmpeg.c): demuxers, the HTTP(S)
# and HLS protocols, the common video, audio and text-subtitle decoders, swresample and
# swscale. LGPL 2.1 (no GPL or nonfree parts enabled); linked statically, and this repository
# has everything needed to rebuild the app against a changed FFmpeg.
#   tools/ffmpeg-player.sh host  <prefix>   Linux desktop (TLS from OpenSSL, as the system's)
#   tools/ffmpeg-player.sh win64 <prefix>   Windows x64 with MinGW-w64 (TLS from Schannel, decoding on the
#                                           graphics card through D3D11/DXVA2, AV1 in software by dav1d)
# The source is a shallow clone of $FFMPEG_TAG kept in $FFMPEG_SRC (default: next to the prefix).
set -euo pipefail
TARGET="$1"; PREFIX="$(mkdir -p "$2" && cd "$2" && pwd)"
: "${FFMPEG_TAG:=n7.1.1}"
: "${FFMPEG_SRC:=$PREFIX/../ffmpeg-src-$FFMPEG_TAG}"
: "${DAV1D_PKG:=1.5.4-1}"   # the MSYS2 package (win64)
STAMP="$FFMPEG_TAG-player-$TARGET-4"
if [ -f "$PREFIX/lib/libavformat.a" ] && [ "$(cat "$PREFIX/.tag" 2>/dev/null)" = "$STAMP" ]; then
  echo "ffmpeg: $PREFIX is up to date ($STAMP)"; exit 0
fi
[ -d "$FFMPEG_SRC" ] || git clone -q --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg "$FFMPEG_SRC"

DEMUX=matroska,mov,mpegts,hls,dash,flv,avi,ogg,wav,aac,mp3,flac,webvtt,srt,ass,h264,hevc
VDEC=h264,hevc,vp8,vp9,mpeg4,mpeg2video,mpeg1video,vc1,wmv3,av1,msmpeg4v3,mjpeg
ADEC=aac,aac_latm,ac3,eac3,dca,truehd,mlp,mp2,mp3,opus,vorbis,flac,alac,wmav2,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_s16be,pcm_bluray,pcm_dvd
SDEC=subrip,srt,ass,ssa,webvtt,mov_text,text
PARSE=h264,hevc,vp8,vp9,av1,mpeg4video,mpegvideo,vc1,aac,aac_latm,ac3,dca,mlp,mpegaudio,opus,vorbis,flac
COMMON=(--prefix="$PREFIX" --enable-static --disable-shared --disable-programs --disable-doc --disable-debug
        --disable-everything --disable-autodetect --disable-avdevice --disable-avfilter
        --enable-avformat --enable-avcodec --enable-swresample --enable-swscale --enable-network
        --enable-protocol=http,https,tcp,tls,file,crypto,hls,data
        --enable-demuxer=$DEMUX --enable-decoder=$VDEC,$ADEC,$SDEC --enable-parser=$PARSE
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,vp9_superframe_split,extract_extradata
        --enable-zlib --enable-pthreads)
case "$TARGET" in
  host)
    EXTRA=(--enable-openssl)
    command -v nasm >/dev/null || EXTRA+=(--disable-x86asm)
    pkg-config --exists dav1d 2>/dev/null && EXTRA+=(--enable-libdav1d --enable-decoder=libdav1d)   # AV1 in software, when the system has it
    ;;
  win64)
    if [ ! -f "$PREFIX/lib/libz.a" ]; then   # zlib (compressed Matroska tracks, gzip over HTTP), built for MinGW
      Z="$PREFIX/.zlib"; rm -rf "$Z"; mkdir -p "$Z"
      curl -sSL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz | tar xz -C "$Z" --strip-components 1
      make -C "$Z" -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- libz.a >/dev/null
      mkdir -p "$PREFIX/lib" "$PREFIX/include"; cp "$Z/libz.a" "$PREFIX/lib/"; cp "$Z/zlib.h" "$Z/zconf.h" "$PREFIX/include/"; rm -rf "$Z"
    fi
    if [ ! -f "$PREFIX/lib/libdav1d.a" ]; then   # dav1d (VideoLAN, BSD-2): AV1 in software, where the graphics card has no AV1; MSYS2's static build
      D="$PREFIX/.dav1d"; rm -rf "$D"; mkdir -p "$D"
      curl -sSfL https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-dav1d-$DAV1D_PKG-any.pkg.tar.zst | tar --zstd -x -C "$D"
      mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/include"; cp "$D/mingw64/lib/libdav1d.a" "$PREFIX/lib/"; cp -r "$D/mingw64/include/dav1d" "$PREFIX/include/"
      sed -e "s|^prefix=.*|prefix=$PREFIX|" "$D/mingw64/lib/pkgconfig/dav1d.pc" > "$PREFIX/lib/pkgconfig/dav1d.pc"; rm -rf "$D"
    fi
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
    EXTRA=(--pkg-config=pkg-config --pkg-config-flags=--static --enable-libdav1d --enable-decoder=libdav1d
           --extra-cflags=-I"$PREFIX/include" --extra-ldflags=-L"$PREFIX/lib" --enable-cross-compile --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32-
           --enable-schannel --enable-d3d11va --enable-dxva2 --enable-hwaccel=h264_d3d11va,hevc_d3d11va,vp9_d3d11va,av1_d3d11va,h264_dxva2,hevc_dxva2
           --enable-w32threads --disable-pthreads)
    ;;
  *) echo "usage: $0 host|win64 <prefix>" >&2; exit 1 ;;
esac
B="$PREFIX/.build"; rm -rf "$B"; mkdir -p "$B"
(cd "$B" && "$FFMPEG_SRC/configure" "${COMMON[@]}" "${EXTRA[@]}" >configure.log 2>&1) || { tail -30 "$B/configure.log"; exit 1; }
make -C "$B" -j"$(nproc)" >"$B/make.log" 2>&1 || { tail -40 "$B/make.log"; exit 1; }
make -C "$B" install >>"$B/make.log" 2>&1
echo "$STAMP" > "$PREFIX/.tag"
rm -rf "$B"
echo "ffmpeg: built $STAMP in $PREFIX ($(du -ch "$PREFIX"/lib/*.a | tail -1 | cut -f1))"
