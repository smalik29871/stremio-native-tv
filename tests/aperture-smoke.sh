#!/usr/bin/env bash
# Native journey and visual states against local fixtures; no account needed.
# Run after tools/linux.sh: bash tests/aperture-smoke.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build}" && pwd)"
OUT="$ROOT/tests/out/aperture"
mkdir -p "$OUT"
PROFILE="$(mktemp -d "$OUT/profile.XXXXXX")"
PORT=8876
python3 -I "$ROOT/tests/fake_addon.py" "$PORT" >"$OUT/addon.log" 2>&1 & SRV=$!
trap 'kill "$SRV" 2>/dev/null || true' EXIT
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1
export SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json"
export SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql"
export SNTV_INTRODB_API="http://127.0.0.1:$PORT/introdb"
export SNTV_FAKE_START=200 SNTV_FAKE_DURATION=7200 SNTV_FRAME_DT=0.016
export SNTV_NO_TRANSITIONS=1
sleep 0.5
run() {
    local name="$1" screen="$2" frames="$3" keys="$4" extra="${5:-}"
    local home="$PROFILE/$name" video=""
    if [ "$screen" = player ]; then
        video=playing
        case "$name" in pause|pause_rest) video=paused ;; esac
    fi
    mkdir -p "$home/.local/share/stremio/nativetv"
    printf '{"theme":7,"lookStreaming":1,"themeAccent":0,"themeBg":0,"setupDone":1,"autoTrailers":0,"prefSubs":"eng","subsAlways":1,"qualityMode":1,"skipMode":2,%s"apertureFixture":1}\n' "$extra" >"$home/.local/share/stremio/nativetv/settings.json"
    HOME="$home" SNTV_EXPECT_SCREEN="$screen" SNTV_EXPECT_VIDEO="$video" SNTV_SCREENSHOT="$OUT/$name.ppm" SNTV_SCREENSHOT_FRAMES="$frames" SNTV_KEYS="$keys" \
        xvfb-run -a -s '-screen 0 1920x1080x24' "$BUILD/stremio-tv" >"$OUT/$name.log" 2>&1
    grep -q "UI check: screen=$screen theme=Aperture" "$OUT/$name.log"
    test -s "$OUT/$name.ppm"
    echo "ok: $name"
}
HOME_KEYS="wait,wait,wait"
DETAIL="$HOME_KEYS,ok,wait,wait,wait"
SOURCES="$DETAIL,ok,wait,wait,wait"
PLAYER="$SOURCES,ok,wait,wait,wait"
run home home 160 "$HOME_KEYS"
run details detail 240 "$DETAIL"
run sources streams 320 "$SOURCES"
run recommended streams 340 "$SOURCES,up"
run player player 400 "$PLAYER"
run pause player 460 "$PLAYER,play"
run pause_rest player 900 "$PLAYER,play"
grep -q 'paused and idle, the controls step aside' "$OUT/pause_rest.log"
run subtitles player 520 "$PLAYER,down,right,right,ok"
grep -q 'UI check: player drawer=2' "$OUT/subtitles.log"
run subtitles_back player 560 "$PLAYER,down,right,right,ok,back"
run audio player 540 "$PLAYER,down,right,right,right,ok"
grep -q 'UI check: player drawer=1' "$OUT/audio.log"
# Seeking commits after 0.35 s: capture immediately after the final key.
run seek player 372 "$PLAYER,right,right,right,right"
run seek_cancel player 440 "$PLAYER,right,back"
grep -q 'Seek cancelled' "$OUT/seek_cancel.log"
run back_sources streams 460 "$PLAYER,back"
run back_details detail 500 "$PLAYER,back,back"
run back_home home 540 "$PLAYER,back,back,back"
SNTV_NO_TRANSITIONS= run transition detail 240 "$DETAIL"
run light_player player 400 "$PLAYER" '"fx":2,"memoryMode":1,'
run light_seek player 372 "$PLAYER,right,right,right,right" '"fx":2,"memoryMode":1,'
python3 -I - "$OUT" <<'PY'
from pathlib import Path
from PIL import Image
import sys
for path in Path(sys.argv[1]).glob('*.ppm'):
    Image.open(path).save(path.with_suffix('.png'))
PY
echo "Aperture native captures: $OUT"
