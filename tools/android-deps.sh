#!/usr/bin/env bash
# Fetches what the Android build needs from source (pinned, checksummed) into
# deploy/android/third_party: SDL2 (its Java activity and the library itself), and curl
# with mbedTLS (Android has no libcurl to load, so the APK ships one).
#   tools/android-deps.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/deploy/android/third_party"
SDL_VER=2.30.9
SDL_SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
mkdir -p "$TP"
if [ ! -f "$TP/SDL2/.version" ] || [ "$(cat "$TP/SDL2/.version")" != "$SDL_VER" ]; then
  tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
  curl -sSL -o "$tmp/sdl.tgz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
  echo "$SDL_SHA256  $tmp/sdl.tgz" | sha256sum -c - >/dev/null || { echo "SDL2 download does not match its checksum" >&2; exit 1; }
  rm -rf "$TP/SDL2"; mkdir -p "$TP/SDL2"
  tar xzf "$tmp/sdl.tgz" -C "$TP/SDL2" --strip-components=1
  echo "$SDL_VER" > "$TP/SDL2/.version"
fi
echo "SDL2 $SDL_VER in $TP/SDL2"

fetch() {   # name version url sha256 strip
  local name=$1 ver=$2 url=$3 sum=$4
  if [ -f "$TP/$name/.version" ] && [ "$(cat "$TP/$name/.version")" = "$ver" ]; then return; fi
  local tmp; tmp="$(mktemp -d)"
  curl -sSL -o "$tmp/src" "$url"
  echo "$sum  $tmp/src" | sha256sum -c - >/dev/null || { echo "$name download does not match its checksum" >&2; rm -rf "$tmp"; exit 1; }
  rm -rf "$TP/$name"; mkdir -p "$TP/$name"
  tar xf "$tmp/src" -C "$TP/$name" --strip-components=1
  echo "$ver" > "$TP/$name/.version"
  rm -rf "$tmp"
  echo "$name $ver in $TP/$name"
}
fetch mbedtls 3.6.2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2 \
  8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
fetch curl 8.11.0 https://curl.se/download/curl-8.11.0.tar.gz \
  264537d90e58d2b09dddc50944baf3c38e7089151c8986715e2aaeaaf2b8118f
