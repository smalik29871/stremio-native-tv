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
export FAKE_PUT_LOG="$OUT/datastore_put.log" FAKE_LIBRARY="$ROOT/tests/fixtures/library.json"   # the fake Stremio account
rm -f "$FAKE_PUT_LOG"
# P2P: the app's engine (tools/torrentd.sh builds it next to the app) and a local seeder of a test video
SEEDER=""
if [ -x "$BUILD/torrentd" ]; then
  "$BUILD/torrentd" -seed "$ROOT/tests/fixtures/media/seek_h264.mkv" -seed-port 6990 > "$OUT/seeder.out" 2>/dev/null & SEEDER=$!
  for i in $(seq 1 50); do [ -s "$OUT/seeder.out" ] && break; sleep 0.1; done
  export FAKE_TORRENT_HASH="$(head -1 "$OUT/seeder.out")" SNTV_TORRENT_PEERS=127.0.0.1:6990
  [ -n "$FAKE_TORRENT_HASH" ] || { echo "the test seeder did not start (port 6990 busy?)"; exit 1; }
else
  echo "skip: P2P runs (build the engine: tools/torrentd.sh host $BUILD/torrentd)"
fi
python3 -I "$ROOT/tests/fake_addon.py" $PORT & SRV=$!
trap 'kill $SRV ${SEEDER:-} 2>/dev/null || true' EXIT
sleep 0.5
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1
export SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json"
export SNTV_LINK_API="http://127.0.0.1:$PORT/link"
export SNTV_INTRODB_API="http://127.0.0.1:$PORT/introdb"
export SNTV_YT_BASE="http://127.0.0.1:$PORT/yt"   # a fake YouTube: TV config, player, formats, ranged media
export SNTV_YT_SOLVER_URL="http://127.0.0.1:$PORT/yt/ejs"   # the "newest solver" download: refused, so the bundled one is all there is
export SNTV_NO_TRANSITIONS=1   # screenshots land mid-animation otherwise; transition_push turns them back on
export HOME="$OUT/home"; mkdir -p "$HOME"
# preferred subtitles = English so the player auto-loads the fake addon's SRT
mkdir -p "$HOME/.local/share/stremio/nativetv"
# subtitles always on (the fake audio is English too), first-run setup already done
echo '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"pipEnabled":1}' > "$HOME/.local/share/stremio/nativetv/settings.json"
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
# About, below the fold, on a profile with no Continue Watching: the first card is a Drama film
ABOUT_HOME='{"prefSubs":"eng","subsAlways":1,"setupDone":1}'
run detail_about 260 "wait,ok,down" HOME="$(home_with about "$ABOUT_HOME")"
grep -q "detail: [0-9]* more like tt" "$OUT/detail_about.log" || { echo "no More like this on the title page"; grep detail "$OUT/detail_about.log"; exit 1; }
run detail_cast 300 "wait,ok,down,down,right,ok" HOME="$(home_with about_cast "$ABOUT_HOME")"
grep -q 'search: "Morgan Freeman" from a title page' "$OUT/detail_cast.log" || { echo "a cast member did not open a search"; grep search "$OUT/detail_cast.log"; exit 1; }
run detail_more_like 320 "wait,ok,down,down,down,ok" HOME="$(home_with about_more "$ABOUT_HOME")"
grep -q "detail: more like this -> tt" "$OUT/detail_more_like.log" || { echo "More like this did not open the title"; grep detail "$OUT/detail_more_like.log"; exit 1; }
run detail_about_series 300 "right,ok,down,down,down"
run detail_trailer_app 320 "wait,ok,right,ok,wait,wait,wait,wait" HOME="$(home_with trailer_app "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9
grep -q "video(null): open app youtube.leanback.v4" "$OUT/detail_trailer_app.log" || { echo "a refused trailer did not open the YouTube app"; grep -E "detail|youtube" "$OUT/detail_trailer_app.log"; exit 1; }
run settings_find 420 "left,down,down,down,down,down,ok,up,ok,t:subtitle,down,down,down,down,left,ok"
grep -q 'settings: "subtitle" matches [2-8]' "$OUT/settings_find.log" || { echo "Find a setting did not list the subtitle settings"; grep settings "$OUT/settings_find.log"; exit 1; }
run settings_find_one 340 "left,down,down,down,down,down,ok,up,ok,t:oled,down,down,down,down,left,ok,right"
grep -q "settings: found Pure black (OLED) in Interface" "$OUT/settings_find_one.log" || { echo "a single match did not open its row"; grep settings "$OUT/settings_find_one.log"; exit 1; }
run intro 70 "" SNTV_INTRO=1 SNTV_FRAME_DT=0.03 SNTV_NO_TRANSITIONS=
grep -q "intro: done" "$OUT/intro.log" || { echo "the launch animation did not finish"; exit 1; }
run detail_trailer 320 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,back" HOME="$(home_with trailer "$ABOUT_HOME")"
grep -q "youtube: dQw4w9WgXcQ as DASH, 2 video qualities" "$OUT/detail_trailer.log" && grep -q "detail: trailer for tt0111161 streaming" "$OUT/detail_trailer.log" && grep -q "player: candidate 1/3 Trailer - best @0s -> http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ.mpd" "$OUT/detail_trailer.log" \
  || { echo "the trailer did not play"; grep -E "detail|youtube|player" "$OUT/detail_trailer.log"; exit 1; }
# no trailer from the addon: TMDB's, a file with no playlist, fetched in YouTube's &range= pieces
run detail_trailer_file 360 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with trailer_file '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}')" SNTV_DEFAULT_ADDONS=http://127.0.0.1:8765/notrailer/manifest.json SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
grep -q "youtube: fileOnly001 streams as a file" "$OUT/detail_trailer_file.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/yt?u=" "$OUT/detail_trailer_file.log" \
  || { echo "the TMDB trailer did not stream through the chunked proxy"; grep -E "detail|youtube|video" "$OUT/detail_trailer_file.log"; exit 1; }
LIBF="$OUT/home_trailer/.local/share/stremio/nativetv/library.json"
[ ! -f "$LIBF" ] || ! grep -q tt0111161 "$LIBF" || { echo "watching the trailer saved progress for the film"; exit 1; }
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
# OK on the card is its Play now (autoplay off, so only the key can start the next episode)
run upnext_ok 520 "right,ok,down,down,ok,ok,wait,wait,wait,wait,wait,wait,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_ok '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"autoplayNext":0}')"
# leaving a video mid-way: the island offers it back on Home, and Blue opens it
run island_resume 520 "right,ok,down,down,ok,ok,wait,wait,wait,wait,wait,back,back,back,back,wait,blue" SNTV_FAKE_START=200 HOME="$(home_with island '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "island: resume tt0903747" "$OUT/island_resume.log" || { echo "Blue did not open the title left mid-way"; grep -i "island\|player" "$OUT/island_resume.log"; exit 1; }
run island_resume_up 540 "right,ok,down,down,ok,ok,wait,wait,wait,wait,wait,back,back,back,back,wait,up,up,up,ok" SNTV_FAKE_START=200 HOME="$(home_with island_up '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "island: resume tt0903747" "$OUT/island_resume_up.log" || { echo "Up and OK did not open the resume card"; grep -i "island" "$OUT/island_resume_up.log"; exit 1; }
run island_resume_focus 500 "right,ok,down,down,ok,ok,wait,wait,wait,wait,wait,back,back,back,back,wait,up,up,up" SNTV_FAKE_START=200 HOME="$(home_with island_f '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
[ "$(grep -c 'player: candidate' "$OUT/upnext_ok.log")" -ge 2 ] || { echo "OK on the Up Next card did not start the next episode"; grep player "$OUT/upnext_ok.log"; exit 1; }
# next episode's sources still arriving (the /slow addon answers 6 s late): the Up Next
# button and the control bar's Next button fill as a loader
SLOW="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/slow/manifest.json"
run upnext_loading 420 "right,ok,down,down,ok,ok" "$SLOW" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_loading '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_next_loading 300 "right,ok,down,down,ok,ok,down,right" "$SLOW" SNTV_FAKE_START=300
run player_next_ring 300 "right,ok,down,down,ok,ok,down,right,right" "$SLOW" SNTV_FAKE_START=300
run player_skipped 300 "right,ok,down,down,ok,ok,ok"
run upnext_credits 300 "right,ok,down,down,ok,ok" SNTV_FAKE_START=2999 HOME="$(home_with fresh_upnext_credits '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run search 210 "search,t:ar"
run search_grid 260 "search,t:ar,right,right,right,right,right,right,right,right,right,right,right"
run search_keyboard 200 "search,ok" SNTV_PHONE_HOST=192.168.1.20
run settings 280 "left,down,down,down,down,down,ok,down,down,down,right,down,down,down,down,down"
run diagnostics 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,ok"
run diagnostics_filter 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,ok,left,left"
run diagnostics_qr 480 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,ok,ok"
run diagnostics_prev_qr 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,ok,blue,ok"
run settings_more 340 "left,down,down,down,down,down,ok,down,down,down,down,down,right,down"
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
run settings_subs_style 340 "left,down,down,down,down,down,ok,down,down,down,right,down,down,down,right"
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
run player_bar_style 520 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,right,right,down,right"
run player_bar_sources 380 "right,ok,down,down,ok,ok,down,right,right,right,right,right,right,ok"
run pip_focus 420 "$P,back,back,down,down,down,down,down,down,down"
run pip_focus_close 460 "$P,back,back,down,down,down,down,down,down,down,right,ok"
run player_subs_scrolled 420 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,down,down,down,down,down,down,down,down,down,down"
run player_episodes 400 "right,ok,down,down,ok,ok,down,right,right,right,right,right,right,right,ok"
run player_paused 320 "right,ok,down,down,ok,ok,ok" SNTV_FAKE_START=200
run player_rest 330 "right,ok,down,down,ok,ok,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05
grep -q "player: paused and idle, the controls step aside" "$OUT/player_rest.log" || { echo "the paused player never leaned back"; grep player "$OUT/player_rest.log"; exit 1; }
run player_rest_ok 360 "right,ok,down,down,ok,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05
grep -q "player: OK plays on from the lean-back screen" "$OUT/player_rest_ok.log" || { echo "OK did not play on from the lean-back screen"; grep player "$OUT/player_rest_ok.log"; exit 1; }
run player_episodes_season 420 "right,ok,down,down,ok,ok,down,right,right,right,right,right,right,right,ok,right"
run player_speed 400 "right,ok,down,down,ok,ok,down,right,right,right,right,ok,right,right"
run player_aspect 360 "right,ok,down,down,ok,ok,down,right,right,right,right,right,ok"
run home_showcase 200 "" HOME="$(home_with showcase '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run home_showcase_rows 240 "down,right" HOME="$(home_with showcase2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run rail_open 150 "left"
run explore 320 "left,down,ok,right,down,down,down"
# details beside a card that stays focused: on Home, in a grid, opening left at the last column, off in Settings
run home_peek 200 "right,right"
run home_peek_marquee 200 "right,right" HOME="$(home_with peek_marquee '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"theme":1}')"
run explore_peek_edge 800 "left,down,ok,right,down,down,down,right,right,right,right,right,right"
run home_peek_off 200 "right,right" HOME="$(home_with peek_off '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"cardDetails":0}')"
# a Continue Watching row sliding off the top fades out instead of leaving a sliced strip
CW_HOME="$(home_with cw_scroll '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"theme":2}')"; cp "$ROOT/tests/fixtures/library.json" "$CW_HOME/.local/share/stremio/nativetv/library.json"
run home_cw_scroll 90 "right,right,right,down" HOME="$CW_HOME"
run explore_cats 300 "left,down,ok,right,down,right"
run library 260 "left,down,down,down,ok"
run explore_moods 300 "left,down,ok,wait,up,right,right,down,right,ok"
grep -q "explore: mood Edge of your seat has [1-9]" "$OUT/explore_moods.log" || { echo "the mood list stayed empty"; grep explore "$OUT/explore_moods.log"; exit 1; }
PROFILES='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey","email":"sam.rivera@example.com","accounts":[{"email":"sam.rivera@example.com","key":"testkey"},{"email":"alex@example.com","key":"k2"}]}'
run profiles 160 "right,ok" HOME="$(home_with profiles "$PROFILES")" SNTV_API_URL="http://127.0.0.1:$PORT"
grep -q "profiles: switching" "$OUT/profiles.log" || { echo "choosing a profile did not switch accounts"; grep -E "profiles|account" "$OUT/profiles.log"; exit 1; }
run profiles_back 160 "back" HOME="$(home_with profiles_back "$PROFILES")" SNTV_API_URL="http://127.0.0.1:$PORT"
grep -q "profiles: switching" "$OUT/profiles_back.log" && { echo "Back on Who's watching switched accounts"; exit 1; }
run list_save 300 "wait,yellow,down,down,ok,ok,wait,up" HOME="$(home_with lists "$ABOUT_HOME")"
grep -q "toast: Saved to Favourites" "$OUT/list_save.log" || { echo "Save to a list did not save"; grep -E "toast|list" "$OUT/list_save.log"; exit 1; }
grep -q '"lists"' "$OUT/home_lists/.local/share/stremio/nativetv/settings.json" || { echo "the list was not stored"; exit 1; }
NO_PREFLIGHT='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}'   # the names decide (the test file itself is 180p)
LIGHTER="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/lighter/manifest.json,http://127.0.0.1:$PORT/manifest.json"   # its 2160p copy plays first
run player_lighter 700 "right,ok,down,down,ok,ok" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter "$NO_PREFLIGHT")"
# fallbacks: a source that dies mid-film is replaced at the same moment with the same subtitles;
# the next source is checked while playing; a draining buffer moves to a lighter copy first;
# Play races the top sources; when all fail the addons are asked again
FB="SNTV_FAKE_NO_TEXT_TRACKS=1"
run fallback_rescue 4000 "right,ok,down,down,ok,ok" "$LIGHTER" "$FB" HOME="$(home_with fb1 "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_DEAD_AFTER=7 SNTV_RESCUE_AFTER=2 SNTV_WARM_AFTER=1
grep -q "player: next source 2 does not answer" "$OUT/fallback_rescue.log" && grep -q "player: rescue (The source stopped sending video) -> source 3 at 6[01][0-9]s" "$OUT/fallback_rescue.log" && grep -q "player: subtitles kept across the switch" "$OUT/fallback_rescue.log" \
  || { echo "a dead source was not replaced at the same moment"; grep -E "player|video" "$OUT/fallback_rescue.log"; exit 1; }
run fallback_drain 4000 "right,ok,down,down,ok,ok" "$LIGHTER" "$FB" HOME="$(home_with fb2 "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_DRAIN=1 SNTV_DRAIN_EVERY=1 SNTV_WARM_AFTER=1
grep -q "player: buffer draining .* switching to source [23] ([0-9]*p) before it stops" "$OUT/fallback_drain.log" \
  || { echo "the draining buffer did not move to a working lighter source"; grep -E "player" "$OUT/fallback_drain.log"; exit 1; }
run fallback_race 2000 "right,ok,down,down,ok,wait,wait,ok" "$LIGHTER" HOME="$(home_with fb3 "$NO_PREFLIGHT")"
grep -q "streams: source 3 answered first" "$OUT/fallback_race.log" && grep -q "player: candidate 1/3 Fake Addon - Example" "$OUT/fallback_race.log" \
  || { echo "Play did not start the source that answered"; grep -E "streams|player" "$OUT/fallback_race.log"; exit 1; }
run fallback_reask 5000 "right,ok,down,down,ok,wait,wait,ok" "$LIGHTER" HOME="$(home_with fb4 "$NO_PREFLIGHT")" SNTV_FAKE_FAIL_LOADS=3 SNTV_REASK_AFTER=1 SNTV_NO_RACE=1
grep -q "player: asked the addons again: 3 sources" "$OUT/fallback_reask.log" || { echo "the addons were not asked again"; grep -E "player" "$OUT/fallback_reask.log"; exit 1; }
grep -q "player: [0-9]* stalls; offering source [0-9] ([0-9]*p)" "$OUT/player_lighter.log" || { echo "repeated buffering offered no lighter source"; grep player "$OUT/player_lighter.log"; exit 1; }
run player_lighter_ok 820 "right,ok,down,down,ok,ok$(printf ',wait%.0s' $(seq 1 28)),ok" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter_ok "$NO_PREFLIGHT")"
grep -q "player: switching to the lighter source [0-9]" "$OUT/player_lighter_ok.log" || { echo "OK did not take the lighter source"; grep player "$OUT/player_lighter_ok.log"; exit 1; }
run player_lighter_seek 700 "right,ok,down,down,ok,ok,right,right,right,right,right,left,left,left,right,right,right" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter_seek "$NO_PREFLIGHT")"
grep -q "offering source" "$OUT/player_lighter_seek.log" && { echo "seeking counted as buffering trouble"; exit 1; }
run library_stats 280 "left,down,down,down,ok,up,right,right,right,right,ok"
run calendar 300 "left,down,down,down,down,ok"
run settings_rows 380 "left,down,down,down,down,down,ok,down,down,down,down,down,down,right,ok,down,right" HOME="$(home_with rows '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run settings_addons 360 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down"
run player_subs_second 480 "right,ok,down,down,ok,ok,down,right,right,right,ok,ok,down,down,down,down,down,down,down,down,down,ok" SNTV_FAKE_START=200
run player_seek_thumb 238 "right,ok,down,down,ok,ok,right,right,right,right,right,right" HOME="$(home_with thumbs '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:8765/thumbs"}')"
# no helper: the TV decodes the keyframe itself (bundled FFmpeg) through its local /frame
if grep -q "^SNTV_FFMPEG_PREFIX:PATH=." "$BUILD/CMakeCache.txt" 2>/dev/null; then
  run player_seek_local 238 "right,ok,down,down,ok,ok,right,right,right,right,right,right"
  grep -q "frames: [0-9]* keyframes indexed" "$OUT/player_seek_local.log" && grep -q "frames: [0-9]* s of 127.0.0.1 in" "$OUT/player_seek_local.log" || { echo "seek preview not decoded on the TV"; grep frames "$OUT/player_seek_local.log"; exit 1; }
else echo "skip: seek previews decoded on the TV (build FFmpeg: tools/ffmpeg.sh)"; fi
run settings_parental 400 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right"
run pin_pad 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,right,ok,ok,right,ok"
run pin_locked 300 "left,down,down,down,down,down,ok" HOME="$(home_with pinned '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"parentalPin":"1234","pinSettings":1}')"
run settings_account 300 "left,down,down,down,down,down,ok,right" HOME="$(home_with accts '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"accounts":[{"email":"other@example.com","key":"k"}]}')"
TM='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}'
run detail_tmdb 200 "right,right,ok" HOME="$(home_with tmdb "$TM")" SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
run player_paused_tmdb 320 "right,ok,down,down,ok,ok,ok" SNTV_FAKE_START=200 HOME="$(home_with tmdb2 "$TM")" SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
run detail_franchise 420 "right,right,ok,down,down,down,right,ok" HOME="$(home_with tmdb3 "$TM")" SNTV_TMDB_API=http://127.0.0.1:8765/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:8765/tmdbimg
grep -q 'detail: franchise -> tt1002' "$OUT/detail_franchise.log" || { echo "the franchise row did not open the next film"; grep -E "detail|tmdb" "$OUT/detail_franchise.log"; exit 1; }
run settings_keys 340 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right"
run settings_rules 380 "left,down,down,down,down,down,ok,up,ok,t:largest,down,down,down,down,left,ok,right" HOME="$(home_with rules '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"srcExclude":"CAM, 3D"}')"
run streams_rules 240 "right,ok,down,down,ok" HOME="$(home_with rules2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"srcExclude":"web-dl","preflight":0}')"
# a YouTube stream from an addon (YouTubio, trailer addons) plays here, streamed as Nuvio does
run streams_youtube 360 "right,ok,down,down,ok,down,down,ok,wait,wait,wait" HOME="$(home_with ytstream '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "streams: YouTube video for tt0903747:1:1 streaming" "$OUT/streams_youtube.log" || { echo "an addon's YouTube stream did not play"; grep -E "streams|youtube" "$OUT/streams_youtube.log"; exit 1; }
run settings_tvcap 380 "left,down,down,down,down,down,ok,up,ok,t:this tv can,down,down,down,down,left,ok,ok" HOME="$(home_with tvcap '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tvCaps":{"dts":[0,2],"hevc":[5,0]}}')"
run settings_backup 420 "left,down,down,down,down,down,ok,up,ok,t:back up,down,down,down,down,left,ok,ok"
grep -q "settings: found Back up or restore settings" "$OUT/settings_backup.log" || { echo "Back up or restore is not in Settings"; grep settings "$OUT/settings_backup.log"; exit 1; }
TK='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid","traktClientSecret":"sec"}'
run trakt_connect 1200 "left,down,down,down,down,down,ok,up,ok,t:trakt,down,down,down,down,left,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with trakt "$TK")" SNTV_TRAKT_API=http://127.0.0.1:8765/trakt
grep -q "tracking: Trakt connected" "$OUT/trakt_connect.log" && grep -q "tracking: Trakt watchlist has 2 titles" "$OUT/trakt_connect.log" || { echo "Trakt did not connect"; grep tracking "$OUT/trakt_connect.log"; exit 1; }
TK2='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid","traktClientSecret":"sec","traktToken":"tok1","simklClientId":"scid","simklToken":"stok"}'
run trakt_scrobble 420 "right,ok,down,down,ok,ok,wait,wait,back,back,back" HOME="$(home_with trakt2 "$TK2")" SNTV_FAKE_START=6600 SNTV_TRAKT_API=http://127.0.0.1:8765/trakt SNTV_SIMKL_API=http://127.0.0.1:8765/simkl
grep -q "tracking: Trakt start tt0903747" "$OUT/trakt_scrobble.log" && grep -q "tracking: Trakt stop tt0903747" "$OUT/trakt_scrobble.log" && grep -q "tracking: SimKL watched tt0903747" "$OUT/trakt_scrobble.log" \
  || { echo "playback was not scrobbled"; grep tracking "$OUT/trakt_scrobble.log"; exit 1; }
run simkl_connect 2500 "left,down,down,down,down,down,ok,up,ok,t:simkl,down,down,down,down,left,ok,ok" HOME="$(home_with simkl '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"simklClientId":"scid"}')" SNTV_SIMKL_API=http://127.0.0.1:8765/simkl
grep -q "tracking: SimKL connected" "$OUT/simkl_connect.log" || { echo "SimKL did not connect"; grep tracking "$OUT/simkl_connect.log"; exit 1; }
run detail_mdblist 260 "wait,ok,down" HOME="$(home_with mdb '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"mdblistKey":"k"}')" SNTV_MDBLIST_API=http://127.0.0.1:8765/mdblist
run transition_push 32 "right,ok" SNTV_NO_TRANSITIONS=
# OK on a card: the title page grows out of it (fixed frame time so the capture lands mid-way)
run transition_zoom 34 "right,ok" SNTV_NO_TRANSITIONS= SNTV_FRAME_DT=0.016
run home_mem_tight 600 "right,right" SNTV_FAKE_MEM=3000000,90000
run home_lowmem 150 "right,right" HOME="$(home_with lowmem '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"memoryMode":1}')"
BR='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":723,"breakLead":5,"breaksEverywhere":1}'
run break_banner 150 "right" HOME="$(home_with brk "$BR")" SNTV_NOW=12:01
run break_screen 320 "left,down,down,down,down,down,ok,down,down,right,ok"
run break_player 300 "right,ok,down,down,ok,ok" HOME="$(home_with brk2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720}')" SNTV_NOW=12:00
run break_sound_file 520 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,down,down,down" HOME="$(home_with brk4 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakSound":1,"breakAudio":"sntv-sound://start.mp3"}')"
# no cached copy: the TV plays the torrent itself, then goes back to TorBox once it has the title
if [ -n "${FAKE_TORRENT_HASH:-}" ]; then
run p2p_playing 420 "right,ok,down,down,ok,ok" HOME="$(home_with p2p1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/p2ponly/manifest.json" SNTV_P2P_RECHECK=1
grep -q "still no cached copy; staying on P2P" "$OUT/p2p_playing.log" || { echo "P2P did not keep playing without a cached copy"; grep -E "player|p2p" "$OUT/p2p_playing.log"; exit 1; }
run settings_p2p 400 "left,down,down,down,down,down,ok,down,down,down,down,right,down,down,down,down,down,down" HOME="$(home_with p2pset '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run p2p_switch 700 "right,ok,down,down,ok,ok" HOME="$(home_with p2p '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/p2p/manifest.json" SNTV_P2P_RECHECK=2
grep -q "candidate 1/.* -> http://127.0.0.1:11490/$FAKE_TORRENT_HASH/0" "$OUT/p2p_switch.log" && grep -q "player: file is H.264 180p, 0 chapters" "$OUT/p2p_switch.log" \
  && grep -q "TorBox has it again" "$OUT/p2p_switch.log" || { echo "P2P fallback or the switch back failed"; grep -E "player|p2p|torrent|probe" "$OUT/p2p_switch.log"; exit 1; }
fi
# progress reaches the account as the app closes mid-film, dated the way stremio-core reads it
run progress_sync 400 "right,ok,down,down,ok,ok" HOME="$(home_with sync '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey"}')" SNTV_API_URL="http://127.0.0.1:$PORT"
python3 -I - "$FAKE_PUT_LOG" <<'PY' || { echo "progress did not reach the account"; exit 1; }
import json, re, sys
puts = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
item = puts[-1]["changes"][0]
iso = re.compile(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$")
assert iso.match(item["_mtime"]) and iso.match(item["state"]["lastWatched"]), item
print("account got", item["_id"], "at", item["state"]["timeOffset"], "ms")
PY
# a position the account never got (the TV went off) beats the account's older one, and is sent again
H="$(home_with sync2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey"}')"
sed 's/"_mtime": 1714557600000/"_mtime": "2026-10-18T20:00:00.000Z"/; s/"timeOffset": 1500000/"timeOffset": 2000000/' "$ROOT/tests/fixtures/library.json" > "$H/.local/share/stremio/nativetv/library.json"
run progress_merge 200 "right" HOME="$H" SNTV_API_URL="http://127.0.0.1:$PORT"
grep -q '"_id":"tt0903747".*"timeOffset":2000000' "$FAKE_PUT_LOG" || { echo "newer local progress was not kept and resent"; exit 1; }
run break_video 300 "right,ok,down,down,ok,ok" HOME="$(home_with brk5 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":1,"breakAudio":"https://www.youtube.com/watch?v=dQw4w9WgXcQ","thumbServer":"http://127.0.0.1:8765"}')" SNTV_NOW=12:00
# a YouTube link typed into Settings is saved by the TV itself (QuickJS runs the solver), then plays at the break with no helper
run break_yt_save 900 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,down,down,down,ok,t:youtu.be/dQw4w9WgXcQ,down,down,down,down,left,ok" HOME="$(home_with brkyt '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0}')" SNTV_NOW=03:00
grep -q "itag 140 from the VISIONOS client" "$OUT/break_yt_save.log" && grep -q "youtube: dQw4w9WgXcQ ready as yt-dQw4w9WgXcQ-audio.m4a" "$OUT/break_yt_save.log" || { echo "youtube save failed"; grep youtube "$OUT/break_yt_save.log"; exit 1; }
run break_yt_play 300 "right,ok,down,down,ok,ok" HOME="$OUT/home_brkyt" SNTV_NOW=12:00
grep -q "break: playing http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ-audio.m4a" "$OUT/break_yt_play.log" || { echo "saved YouTube recording not played"; grep break "$OUT/break_yt_play.log"; exit 1; }
# YouTube's bot check on the visionOS client: the TV client, with the solver, takes over
run break_yt_tv 400 "right,ok,down,down,ok,ok" HOME="$(home_with brkyt2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0,"breakAudio":"https://youtu.be/tvOnlyVid01"}')" SNTV_NOW=12:00
grep -q "itag 140 from the TVHTML5 client" "$OUT/break_yt_tv.log" && grep -q "ready as yt-tvOnlyVid01-audio.m4a" "$OUT/break_yt_tv.log" || { echo "TV client fallback failed"; grep youtube "$OUT/break_yt_tv.log"; exit 1; }
# hold OK: a short press still opens the title; the hint fills, then the release opens the sheet
HOLD="wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait"
run options_sheet 400 "right,right,hold,$HOLD,release"
run hold_hint 200 "right,ok,down,down,right,hold,wait,wait"
run episode_options 400 "right,ok,down,down,right,hold,$HOLD,release"
# timing for a subtitle track inside the file (the TV draws it; the player sends the offset)
run player_subs_timing_embedded 420 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,right,right,right" SNTV_FAKE_START=300
run episode_marked 470 "right,ok,down,down,right,hold,$HOLD,release,wait,wait,ok" HOME="$(home_with ep_marked '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run episode_upto 520 "right,ok,down,down,right,right,right,hold,$HOLD,release,wait,wait,down,ok" HOME="$(home_with ep_upto '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run options_yellow 200 "right,right,right,yellow"
run search_recent 200 "search,down,down" HOME="$(home_with recent '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"recentSearches":["breaking bad","midnight harbor","something about prison and hope"]}')"
run offline_update 220 "right" SNTV_FAKE_OFFLINE=1 SNTV_UPDATE_URL=http://127.0.0.1:8765/repo.json
# a new release: at a quiet moment the app asks; Update now hands it to the Homebrew Channel
run update_prompt 600 "" HOME="$(home_with upd1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:8765/repo.json
W=$(printf 'wait,%.0s' $(seq 1 40))
run update_install 1600 "${W}ok" HOME="$(home_with upd2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:8765/repo.json SNTV_FAKE_INSTALL=1
grep -q "update: installing" "$OUT/update_prompt.log" && { echo "it installed without being asked"; exit 1; }
[ "$(grep -c "update: 9.9.9 available" "$OUT/update_install.log")" = 1 ] && grep -q "update: the viewer said update now" "$OUT/update_install.log" && grep -q "update: installing 9.9.9 from http://127.0.0.1:8765/org.stremio.nativetv_9.9.9_arm.ipk" "$OUT/update_install.log" && grep -q "update: 9.9.9 installed" "$OUT/update_install.log" || { echo "the update did not install"; grep update "$OUT/update_install.log"; exit 1; }
ASR='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":0,"breakMethod":1,"breakAsr":1,"breakLat":"43.676","breakLon":"-79.485","breakCity":"Toronto","breakLead":5,"breaksEverywhere":1}'
run island_update 900 "$(printf 'wait,%.0s' $(seq 1 35))back" HOME="$(home_with islu '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:8765/repo.json
run update_refused 1700 "${W}ok,wait,wait,wait,ok" HOME="$(home_with upd3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:8765/repo.json
grep -q "video(null): open app org.webosbrew.hbchannel" "$OUT/update_refused.log" || { echo "a refused update did not offer the Homebrew Channel"; grep update "$OUT/update_refused.log"; exit 1; }
run break_asr_change 520 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,right,back,back" HOME="$(home_with asr "$ASR")" TZ=America/Toronto SNTV_NOW=15:58
run break_media_length 230 "right,ok,down,down,ok,ok" HOME="$(home_with bml '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakLen":-1,"breakVideo":0,"breakAudio":"http://127.0.0.1:8765/video/sample_h264_aac.mp4"}')" SNTV_NOW=12:00 SNTV_FAKE_DURATION=6
run break_settings 480 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,ok" SNTV_IPAPI=http://127.0.0.1:8765/ipapi HOME="$(home_with brk3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1}')"
run settings_phone 400 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right,ok" SNTV_PHONE_HOST=192.168.1.20
run player_back_stops 220 "$P" HOME="$(home_with nopip '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_autosync 520 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,down,down,down,down,down,down,down,down,ok,ok" SNTV_FAKE_START=200
# two English files, no file-matched one: the one that runs to the end of the video wins
run player_subs_bestfit 420 "right,ok,down,down,ok,ok,wait,wait,wait,wait,wait,wait,down,down,ok" SNTV_DEFAULT_ADDONS=http://127.0.0.1:8765/bestfit/manifest.json HOME="$(home_with bestfit '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_FAKE_START=200 SNTV_FAKE_NO_TEXT_TRACKS=1
grep -q "subs: switching to file 1, it fits better" "$OUT/player_subs_bestfit.log" || { echo "the better-fitting subtitle was not picked"; grep subs "$OUT/player_subs_bestfit.log"; exit 1; }
# OpenSubtitles.com built in: the file made for this video (its hash) is the one downloaded
run player_subs_opensubs 420 "right,ok,down,down,ok,ok,wait,wait,wait,wait,down,down,ok" SNTV_DEFAULT_ADDONS=http://127.0.0.1:8765/bestfit/manifest.json HOME="$(home_with os '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"osApiKey":"oskey"}')" SNTV_FAKE_START=200 SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_OPENSUBS_API=http://127.0.0.1:8765/os
grep -q "subs: 2 from OpenSubtitles.com, one made for this file" "$OUT/player_subs_opensubs.log" && grep -q "opensubs: file 222, 4 downloads left today" "$OUT/player_subs_opensubs.log" \
  && ! grep -q "opensubs: file 111" "$OUT/player_subs_opensubs.log" || { echo "OpenSubtitles.com did not pick the file made for this video"; grep -E "subs|opensubs" "$OUT/player_subs_opensubs.log"; exit 1; }
AI='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}'
run detail_ai 340 "right,right,ok,hold,down,down,ok" HOME="$(home_with ai "$AI")" SNTV_AI_API=http://127.0.0.1:8765/ai/v1
run detail_ai_model 400 "right,right,ok,hold,down,down,ok" HOME="$(home_with aim '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key","aiModel":"missing-model"}')" SNTV_AI_API=http://127.0.0.1:8765/ai/v1
run search_ai 360 "search,t:something about prison and hope" HOME="$(home_with ai2 "$AI")" SNTV_AI_API=http://127.0.0.1:8765/ai/v1
run player_translate 600 "right,ok,down,down,ok,ok,down,right,right,right,ok,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,ok" SNTV_FAKE_START=6 HOME="$(home_with ai3 '{"prefSubs":"fra","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}')" SNTV_AI_API=http://127.0.0.1:8765/ai/v1
run settings_ai 340 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right,down" HOME="$(home_with ai4 "$AI")"
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
