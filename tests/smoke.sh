#!/usr/bin/env bash
# Headless UI smoke test: runs the desktop build under Xvfb against the fake
# addon, drives a few keys, and writes screenshots to tests/out/.
# Usage: bash tests/smoke.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="$ROOT/tests/out"
mkdir -p "$OUT"
PORT=8765
python3 -I "$ROOT/tests/fake_addon.py" $PORT & SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
sleep 0.5
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1
export SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json"
export SNTV_LINK_API="http://127.0.0.1:$PORT/link"
export SNTV_INTRODB_API="http://127.0.0.1:$PORT/introdb"
export HOME="$OUT/home"; mkdir -p "$HOME"
# preferred subtitles = English so the player auto-loads the fake addon's SRT
mkdir -p "$HOME/.local/share/stremio/nativetv"
# subtitles always on (the fake audio is English too), first-run setup already done
echo '{"prefSubs":"eng","subsAlways":1,"setupDone":1}' > "$HOME/.local/share/stremio/nativetv/settings.json"
home_with() { # name json: a separate profile for runs that need other settings
  local h="$OUT/home_$1"; rm -rf "$h"; mkdir -p "$h/.local/share/stremio/nativetv"
  echo "$2" > "$h/.local/share/stremio/nativetv/settings.json"; echo "$h"
}
run() { # name frames keys [extra env assignments...]
  local name="$1" frames="$2" keys="$3"; shift 3
  env "$@" SNTV_SCREENSHOT="$OUT/$name.ppm" SNTV_SCREENSHOT_FRAMES="$frames" SNTV_KEYS="$keys" \
    xvfb-run -a -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" >"$OUT/$name.log" 2>&1 || { echo "run $name failed"; tail -20 "$OUT/$name.log"; exit 1; }
  echo "ok: $name"
}
run home 150 "down,right,right"
run detail_movie 170 "down,right,right,ok"
run detail_series 170 "down,right,ok"
run episodes 220 "down,right,ok,down,down,right"
run streams 240 "down,right,ok,down,down,ok"
run player 280 "down,right,ok,down,down,ok,ok"
run player_subs 300 "down,right,ok,down,down,ok,ok,down"
run player_audio 300 "down,right,ok,down,down,ok,ok,up"
run player_cue 300 "down,right,ok,down,down,ok,ok" SNTV_FAKE_NO_TEXT_TRACKS=1
run player_seek 268 "down,right,ok,down,down,ok,ok,right,right,right"
run upnext_card 300 "down,right,ok,down,down,ok,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_card '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run upnext_auto 380 "down,right,ok,down,down,ok,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 HOME="$(home_with fresh_upnext_auto '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_skipped 300 "down,right,ok,down,down,ok,ok,ok"
run upnext_credits 300 "down,right,ok,down,down,ok,ok" SNTV_FAKE_START=2999 HOME="$(home_with fresh_upnext_credits '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run search 210 "search,t:ar"
run search_grid 260 "search,t:ar,right,right,right,right,right,right,right,right,right,right,right"
run settings 160 "right,ok,down,down,down,down,down"
run diagnostics 300 "right,ok,down,down,down,down,down,down,down,down,down,down,down,down,ok"
run diagnostics_filter 320 "right,ok,down,down,down,down,down,down,down,down,down,down,down,down,ok,left,left"
run diagnostics_qr 320 "right,ok,down,down,down,down,down,down,down,down,down,down,down,down,ok,ok"
run diagnostics_prev_qr 340 "right,ok,down,down,down,down,down,down,down,down,down,down,down,down,ok,blue,ok"
run settings_more 200 "right,ok,down,down,down,down,down,down,down,down,down,down,down"
# first-run setup on a fresh profile: first step, then a later step after a few answers
run welcome 90 "" HOME="$(home_with fresh '{}')"
run welcome_subs_when 170 "down,ok,down,ok" HOME="$(home_with fresh2 '{}')"
# subtitles stay off when the audio already is the subtitle language
run player_native_audio 300 "down,right,ok,down,down,ok,ok" HOME="$(home_with native '{"prefSubs":"eng","setupDone":1}')"
# pick Spanish audio and Portuguese subtitles, then the next episode starts with them
MEM="$(home_with memory '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_pick_tracks 340 "down,right,ok,down,down,ok,ok,up,down,ok,down,down,down,ok" HOME="$MEM"
run player_remembered 300 "down,right,ok,down,down,ok,ok" HOME="$MEM"
run home_oled 150 "down,right,right" HOME="$(home_with oled '{"oled":1,"setupDone":1}')"
run player_stats 300 "down,right,ok,down,down,ok,ok,red"
# screensaver after one quiet second (no keys at all)
run ambient 200 "" SNTV_AMBIENT_SECONDS=1
run ambient_paused 300 "down,right,ok,down,down,ok,ok,ok" SNTV_AMBIENT_SECONDS=1 SNTV_FAKE_START=200 HOME="$(home_with fresh_ambient_paused '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run login_qr 160 "right,right,ok"
run login_pw 180 "right,right,ok,ok"
python3 -I - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
print("screenshots:", sorted(os.path.basename(x) for x in glob.glob(os.path.join(sys.argv[1], "*.png"))))
PY
