#!/usr/bin/env bash
# Windows build, cross-compiled on Linux with MinGW-w64: a folder (and a zip) with Cuelight.exe,
# the DLLs it needs and its assets, run as is (no installer).
#   tools/windows.sh [out-dir]      default build-win/
# Needs: gcc-mingw-w64-x86-64-posix, cmake, make, nasm, zstd, zip, curl (and Go for the P2P engine). Fetches:
#   - SDL2 and ANGLE (OpenGL ES over Direct3D) prebuilt by MSYS2, with the GCC runtime,
#     winpthreads and zlib DLLs they load (zlib licence, BSD, GCC runtime exception, MIT),
#     and Khronos's OpenGL ES 2 headers (MIT);
#   - curl's source (built here with Windows' own TLS, Schannel, as libcurl.dll);
#   - FFmpeg (tools/ffmpeg-player.sh win64: LGPL, linked statically; this repository has
#     everything needed to rebuild against a changed FFmpeg).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mkdir -p "${1:-$ROOT/build-win}" && cd "${1:-$ROOT/build-win}" && pwd)"
DEPS="$OUT/deps"; mkdir -p "$DEPS/pkgs"
MSYS=https://repo.msys2.org/mingw/mingw64
PKGS=(mingw-w64-x86_64-SDL2-2.32.10-1-any.pkg.tar.zst
      mingw-w64-x86_64-angleproject-2.1.r25748.890b5d8f-6-any.pkg.tar.zst
      mingw-w64-x86_64-libgcc-16.2.0-6-any.pkg.tar.zst
      mingw-w64-x86_64-libstdc%2B%2B-16.2.0-6-any.pkg.tar.zst
      mingw-w64-x86_64-libwinpthread-14.0.0.r532.gdbbc8e99d-1-any.pkg.tar.zst
      mingw-w64-x86_64-zlib-1.3.2-3-any.pkg.tar.zst)
for p in "${PKGS[@]}"; do
  [ -f "$DEPS/pkgs/$p" ] || curl -sSfL -o "$DEPS/pkgs/$p" "$MSYS/$p"
  [ -f "$DEPS/pkgs/$p.done" ] || { tar --zstd -xf "$DEPS/pkgs/$p" -C "$DEPS"; touch "$DEPS/pkgs/$p.done"; }
done
W="$DEPS/mingw64"
KHR=https://raw.githubusercontent.com/KhronosGroup   # OpenGL ES 2 headers (MIT): ANGLE's package has only its extensions
mkdir -p "$W/include/GLES2" "$W/include/KHR"
[ -f "$W/include/GLES2/gl2.h" ] || curl -sSfL -o "$W/include/GLES2/gl2.h" "$KHR/OpenGL-Registry/main/api/GLES2/gl2.h"
[ -f "$W/include/GLES2/gl2platform.h" ] || curl -sSfL -o "$W/include/GLES2/gl2platform.h" "$KHR/OpenGL-Registry/main/api/GLES2/gl2platform.h"
[ -f "$W/include/KHR/khrplatform.h" ] || curl -sSfL -o "$W/include/KHR/khrplatform.h" "$KHR/EGL-Registry/main/api/KHR/khrplatform.h"
CURL_VER=8.11.0
if [ ! -d "$DEPS/curl" ]; then
  mkdir -p "$DEPS/curl"; curl -sSfL "https://curl.se/download/curl-$CURL_VER.tar.gz" | tar xz -C "$DEPS/curl" --strip-components 1
fi
FF="$OUT/ffmpeg"
bash "$ROOT/tools/ffmpeg-player.sh" win64 "$FF"
cmake -S "$ROOT" -B "$OUT/cmake" -DSNTV_PLATFORM=windows -DCMAKE_BUILD_TYPE=Release -DSNTV_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/mingw-w64.cmake" -DSNTV_WIN_DEPS="$W" -DSNTV_WIN_CURL="$DEPS/curl" \
      -DSNTV_PLAYER_FFMPEG="$FF" -DSNTV_TMDB_BUILTIN_KEY="${TMDB_API_KEY:-}" -DSNTV_TRAKT_CLIENT_ID="${TRAKT_CLIENT_ID:-}" \
      -DSNTV_TRAKT_CLIENT_SECRET="${TRAKT_CLIENT_SECRET:-}" -DSNTV_SIMKL_CLIENT_ID="${SIMKL_CLIENT_ID:-}" \
      -DSNTV_MDBLIST_KEY="${MDBLIST_KEY:-}" -DSNTV_OPENSUBTITLES_KEY="${OPENSUBTITLES_KEY:-}" >/dev/null
cmake --build "$OUT/cmake" -j"$(nproc)"
# the folder people run it from
APP="$OUT/Cuelight"; rm -rf "$APP"; mkdir -p "$APP"
cp "$OUT/cmake/stremio-tv.exe" "$APP/Cuelight.exe"
cp "$OUT/cmake/curl/lib/libcurl.dll" "$APP/" 2>/dev/null || cp "$OUT"/cmake/curl/lib/libcurl*.dll "$APP/"
for d in SDL2.dll libEGL.dll libGLESv2.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll zlib1.dll; do cp "$W/bin/$d" "$APP/"; done
cp -r "$ROOT/assets" "$APP/assets"
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$APP/"
if command -v go >/dev/null; then bash "$ROOT/tools/torrentd.sh" win64 "$APP/torrentd.exe"; else echo "no Go: the P2P fallback is left out"; fi
VER=$(sed -n 's/.*SNTV_APP_VERSION "\(.*\)"/\1/p' "$ROOT/src/platform/platform.h")
(cd "$OUT" && rm -f "Cuelight-$VER-windows-x64.zip" && zip -qr "Cuelight-$VER-windows-x64.zip" Cuelight)
echo "windows: $APP (Cuelight.exe) and $OUT/Cuelight-$VER-windows-x64.zip"
