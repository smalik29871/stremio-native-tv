#!/usr/bin/env bash
# Fetches what the Android build needs from source (pinned, checksummed) into
# deploy/android/third_party: SDL2 (its Java activity and the library itself).
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
