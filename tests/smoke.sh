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
export SNTV_NO_TRANSITIONS=1   # screenshots land mid-animation otherwise; transition_push turns them back on
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
run home 150 "right,right"
run detail_movie 170 "right,right,ok"
run detail_series 170 "right,ok"
run episodes 220 "right,ok,down,down,right"
run streams 240 "right,ok,down,down,ok"
run streams_best 240 "right,ok,down,down,ok,up" HOME="$(home_with best '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"qualityMode":1}')"
run player 280 "right,ok,down,down,ok,ok"
run player_subs 360 "right,ok,down,down,ok,ok,down,right,right,right,ok"
run player_audio 400 "right,ok,down,down,ok,ok,down,right,right,right,right,ok,down"
run player_cue 300 "right,ok,down,down,ok,ok" SNTV_FAKE_NO_TEXT_TRACKS=1
run player_seek 268 "right,ok,down,down,ok,ok,right,right,right" SNTV_FAKE_DURATION=1
run upnext_card 300 "right,ok,down,down,ok,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_card '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run upnext_auto 380 "right,ok,down,down,ok,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 HOME="$(home_with fresh_upnext_auto '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_skipped 300 "right,ok,down,down,ok,ok,ok"
run upnext_credits 300 "right,ok,down,down,ok,ok" SNTV_FAKE_START=2999 HOME="$(home_with fresh_upnext_credits '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run search 210 "search,t:ar"
run search_grid 260 "search,t:ar,right,right,right,right,right,right,right,right,right,right,right"
run settings 260 "left,down,down,down,down,down,ok,down,down,right,down,down,down"
run diagnostics 440 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right,ok"
run diagnostics_filter 480 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right,ok,left,left"
run diagnostics_qr 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right,ok,ok"
run diagnostics_prev_qr 480 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right,ok,blue,ok"
run settings_more 320 "left,down,down,down,down,down,ok,down,down,down,down,right,down"
# first-run setup on a fresh profile: first step, then a later step after a few answers
run welcome 90 "" HOME="$(home_with fresh '{}')"
run welcome_subs_when 170 "ok,down,ok" HOME="$(home_with fresh2 '{}')"
# subtitles stay off when the audio already is the subtitle language
run player_native_audio 300 "right,ok,down,down,ok,ok" HOME="$(home_with native '{"prefSubs":"eng","setupDone":1}')"
# pick Spanish audio and Portuguese subtitles, then the next episode starts with them
MEM="$(home_with memory '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_pick_tracks 520 "right,ok,down,down,ok,ok,down,right,right,right,right,ok,down,down,ok,left,ok,down,down,down,down,ok" HOME="$MEM"
run player_remembered 300 "right,ok,down,down,ok,ok" HOME="$MEM"
run home_oled 150 "right,right" HOME="$(home_with oled '{"oled":1,"setupDone":1}')"
run settings_subs_style 320 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,right"
run player_cue_yellow 300 "right,ok,down,down,ok,ok" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with yellow '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"subsStyle":2,"subsSize":2}')"
run player_cue_boxed 300 "right,ok,down,down,ok,ok" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with boxed '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"subsStyle":3}')"
P='right,ok,down,down,ok,ok,back'
run pip_detail 320 "$P"
run pip_home 360 "$P,back,back"
run pip_restore 380 "$P,back,back,play"
run pip_claim 340 "$P,ok"
run pip_stop 340 "$P,back,stop"
run player_stats 300 "right,ok,down,down,ok,ok,red"
run player_bar 300 "right,ok,down,down,ok,ok,down"
run player_bar_style 500 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,down,down,down,down,down,down,down,ok"
run player_bar_sources 380 "right,ok,down,down,ok,ok,down,right,right,right,right,right,right,ok"
run pip_focus 420 "$P,back,back,down,down,down,down,down,down,down"
run pip_focus_close 460 "$P,back,back,down,down,down,down,down,down,down,right,ok"
run player_subs_scrolled 420 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,down,down,down,down"
run player_episodes 400 "right,ok,down,down,ok,ok,down,right,right,right,right,right,right,right,ok"
run player_paused 320 "right,ok,down,down,ok,ok,ok" SNTV_FAKE_START=200
run player_speed 400 "right,ok,down,down,ok,ok,down,right,right,right,right,ok,right,right"
run player_aspect 360 "right,ok,down,down,ok,ok,down,right,right,right,right,right,ok"
run home_showcase 200 "" HOME="$(home_with showcase '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run home_showcase_rows 240 "down,right" HOME="$(home_with showcase2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run rail_open 150 "left"
run explore 320 "left,down,ok,right,down,down,down"
run explore_cats 300 "left,down,ok,right,down,right"
run library 260 "left,down,down,down,ok"
run calendar 300 "left,down,down,down,down,ok"
run settings_rows 360 "left,down,down,down,down,down,ok,down,down,down,down,down,right,ok,down,right" HOME="$(home_with rows '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run settings_addons 340 "left,down,down,down,down,down,ok,down,down,down,down,down,down,right,down"
run player_subs_second 480 "right,ok,down,down,ok,ok,down,right,right,right,ok,ok,down,down,down,down,ok" SNTV_FAKE_START=200
run player_seek_thumb 238 "right,ok,down,down,ok,ok,right,right,right,right,right,right" HOME="$(home_with thumbs '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:8765/thumbs"}')"
run settings_parental 380 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right"
run pin_pad 440 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right,ok,ok,right,ok"
run pin_locked 300 "left,down,down,down,down,down,ok" HOME="$(home_with pinned '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"parentalPin":"1234","pinSettings":1}')"
run settings_account 300 "left,down,down,down,down,down,ok,right" HOME="$(home_with accts '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"accounts":[{"email":"other@example.com","key":"k"}]}')"
TM='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}'
run detail_tmdb 200 "right,right,ok" HOME="$(home_with tmdb "$TM")" SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
run player_paused_tmdb 320 "right,ok,down,down,ok,ok,ok" SNTV_FAKE_START=200 HOME="$(home_with tmdb2 "$TM")" SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
run settings_keys 320 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right"
run transition_push 32 "right,ok" SNTV_NO_TRANSITIONS=
run home_lowmem 150 "right,right" HOME="$(home_with lowmem '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"memoryMode":1}')"
run home_compact 200 "right,right" HOME="$(home_with compact '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":2}')"
run pip_focus_ok 440 "$P,back,back,down,down,down,down,down,down,down,ok"
# screensaver after one quiet second (no keys at all)
run ambient 200 "" SNTV_AMBIENT_SECONDS=1
run ambient_paused 300 "right,ok,down,down,ok,ok,ok" SNTV_AMBIENT_SECONDS=1 SNTV_FAKE_START=200 HOME="$(home_with fresh_ambient_paused '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run login_qr 260 "left,down,down,down,down,down,down,ok"
run login_pw 280 "left,down,down,down,down,down,down,ok,ok"
python3 -I - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
print("screenshots:", sorted(os.path.basename(x) for x in glob.glob(os.path.join(sys.argv[1], "*.png"))))
PY
