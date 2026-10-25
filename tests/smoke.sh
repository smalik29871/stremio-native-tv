#!/usr/bin/env bash
# Headless UI smoke test: runs the desktop build under Xvfb against the fake
# addon, drives a few keys, and writes screenshots to tests/out/.
# Usage: bash tests/smoke.sh [build-dir]   (SMOKE_PART=K/N runs part K of N: tests/shard.py;
# SMOKE_LANES=L runs that part as L smaller ones side by side, each with its own ports and folder: about 1.5x
# faster on a 4-core machine, but runs that wait on timing (breaks, the screensaver's handoff) can fail there)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="${SMOKE_OUT:-$ROOT/tests/out}"
mkdir -p "$OUT"
LANES="${SMOKE_LANES:-1}"
if [ "$LANES" -gt 1 ] && [ -z "${SMOKE_PART_OF:-}" ]; then
  PART="${SMOKE_PART:-1/1}"; K="${PART%/*}"; N="${PART#*/}"
  pids=()
  for l in $(seq 1 "$LANES"); do   # part K of N is parts (K-1)*L+1 .. K*L of N*L
    mkdir -p "$OUT/lane$l"
    SMOKE_LANES=1 SMOKE_PART="$(( (K - 1) * LANES + l ))/$(( N * LANES ))" SMOKE_OUT="$OUT/lane$l" SMOKE_PORT=$(( 8765 + 10 * l )) \
      SMOKE_SEED_PORT=$(( 6990 + l )) SNTV_PHONE_PORT=$(( 12000 + 20 * l )) SNTV_ENGINE_PORT=$(( 12500 + l )) SMOKE_XVFB_N=$(( 100 + 30 * l )) \
      LP_NUM_THREADS=2 bash "$0" "$BUILD" > "$OUT/lane$l.txt" 2>&1 & pids+=($!)
  done
  failed=0
  for i in "${!pids[@]}"; do
    l=$(( i + 1 ))
    if wait "${pids[$i]}"; then grep -c "^ok:" "$OUT/lane$l.txt" | sed "s/^/lane $l: /; s/$/ runs ok/"
    else failed=1; echo "lane $l failed:"; tail -25 "$OUT/lane$l.txt"; fi
  done
  for l in $(seq 1 "$LANES"); do cp -f "$OUT/lane$l"/*.log "$OUT/lane$l"/*.png "$OUT/" 2>/dev/null || true; done   # where CI collects them
  exit $failed
fi
if [ -n "${SMOKE_PART:-}" ] && [ -z "${SMOKE_PART_OF:-}" ]; then   # SMOKE_PART=2/4: one part of the runs (CI spreads them over machines)
  python3 -I "$ROOT/tests/shard.py" "$ROOT/tests/smoke.sh" "${SMOKE_PART%/*}" "${SMOKE_PART#*/}" > "$OUT/part.sh"
  SMOKE_PART_OF=1 exec bash "$OUT/part.sh" "$BUILD"
fi
PORT=${SMOKE_PORT:-8765}
export FAKE_PUT_LOG="$OUT/datastore_put.log" FAKE_LIBRARY="$ROOT/tests/fixtures/library.json"   # the fake Stremio account
export FAKE_TRACK_LOG="$OUT/fake_track.log"   # what the fake Trakt and SimKL were sent
rm -f "$FAKE_PUT_LOG"
# P2P: the app's engine (tools/torrentd.sh builds it next to the app) and a local seeder of a test video
SEEDER=""
if [ -x "$BUILD/torrentd" ]; then
  "$BUILD/torrentd" -seed "$ROOT/tests/fixtures/media/seek_h264.mkv" -seed-port "${SMOKE_SEED_PORT:-6990}" > "$OUT/seeder.out" 2>/dev/null & SEEDER=$!
  for i in $(seq 1 50); do [ -s "$OUT/seeder.out" ] && break; sleep 0.1; done
  export FAKE_TORRENT_HASH="$(head -1 "$OUT/seeder.out")" SNTV_TORRENT_PEERS=127.0.0.1:${SMOKE_SEED_PORT:-6990}
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
export SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql"   # a fake IMDb with no trailers: YouTube's are used
export SNTV_YT_BASE="http://127.0.0.1:$PORT/yt"   # a fake YouTube: TV config, player, formats, ranged media
export SNTV_YT_SOLVER_URL="http://127.0.0.1:$PORT/yt/ejs"   # the "newest solver" download: refused, so the bundled one is all there is
export SNTV_DEPTH_URL="http://127.0.0.1:$PORT/nodepth"   # no depth maps but in the runs about them: the real list (on GitHub) would move Home in every run
export SNTV_NO_TRANSITIONS=1   # screenshots land mid-animation otherwise; transition_push turns them back on
export SNTV_WEATHER_API=http://127.0.0.1:$PORT/weather SNTV_IPAPI=http://127.0.0.1:$PORT/ipapi   # the weather clock asks these, never the real ones
export SNTV_ITUNES_API=http://127.0.0.1:$PORT/noitunes   # no soundtracks, and nothing asked of the real store; the music runs set /itunes
export SNTV_NO_WHATSNEW=1   # every profile here is "set up", which reads as just updated; the whatsnew run turns it on
export SNTV_NO_BUFFER=1   # the runs below test what they did before the buffer ahead; the buffer_ runs turn it on
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
    xvfb-run -a -n "${SMOKE_XVFB_N:-99}" -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" >"$OUT/$name.log" 2>&1 || { echo "run $name failed"; tail -20 "$OUT/$name.log"; exit 1; }
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
grep -q 'search: the films of "Morgan Freeman" from a title page' "$OUT/detail_cast.log" && grep -q "search: no filmography from TMDB, searching the name" "$OUT/detail_cast.log" \
  || { echo "a cast member without TMDB did not fall back to a search"; grep search "$OUT/detail_cast.log"; exit 1; }
# with TMDB: their own films and series, most popular first; talk shows and cameos left out
run detail_cast_tmdb 900 "wait,ok,down,down,right,ok,wait,wait" HOME="$(home_with cast_tmdb '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}')" SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
grep -q "search: 2 titles with Aaron Paul" "$OUT/detail_cast_tmdb.log" || { echo "a cast member did not open their films"; grep -E "search|tmdb" "$OUT/detail_cast_tmdb.log"; exit 1; }
run detail_more_like 320 "wait,ok,down,down,down,ok" HOME="$(home_with about_more "$ABOUT_HOME")"
grep -q "detail: more like this -> tt" "$OUT/detail_more_like.log" || { echo "More like this did not open the title"; grep detail "$OUT/detail_more_like.log"; exit 1; }
run detail_about_series 300 "right,ok,down,down,down"
run detail_trailer_app 320 "wait,ok,right,ok,wait,wait,wait,wait" HOME="$(home_with trailer_app "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9
grep -q "detail: trailer for .* failed" "$OUT/detail_trailer_app.log" && ! grep -q "open app" "$OUT/detail_trailer_app.log" || { echo "a refused trailer did not say so"; grep -E "detail|youtube" "$OUT/detail_trailer_app.log"; exit 1; }
# where the device has a YouTube app (a phone, an LG), a trailer YouTube refuses is offered to it
run trailer_youtube_app_offer 360 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with trailer_app2 "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_BROWSER=1
grep -q "UI check: screen=options" "$OUT/trailer_youtube_app_offer.log" || { echo "a refused trailer did not offer the YouTube app"; exit 1; }
run trailer_youtube_app 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,ok,wait" HOME="$(home_with trailer_app3 "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_BROWSER=1
grep -q "youtube: handed [A-Za-z0-9_-]* to the YouTube app" "$OUT/trailer_youtube_app.log" && grep -q "video: the YouTube app opened" "$OUT/trailer_youtube_app.log" \
  || { echo "a refused trailer was not handed to the YouTube app"; grep -E "detail|youtube|video:|UI check" "$OUT/trailer_youtube_app.log"; exit 1; }
# on a phone, a trailer YouTube refuses plays in YouTube's own player inside the app
run trailer_youtube_embed 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with trailer_emb "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_EMBED=1
grep -q "YouTube's own player plays it" "$OUT/trailer_youtube_embed.log" && grep -q "video(null): load youtube-embed:" "$OUT/trailer_youtube_embed.log" \
  || { echo "a refused trailer did not play in YouTube's own player"; grep -E "detail|youtube|video" "$OUT/trailer_youtube_embed.log"; exit 1; }
# an LG: Cuelight YouTube (YouTube's own TV client, a companion app) plays what YouTube refuses;
# not installed, it is offered, installed from the releases list and opened on the video
run ytweb_trailer 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with ytw1 "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=1
grep -q "video: Cuelight YouTube opened [A-Za-z0-9_-]* at 0s (play)" "$OUT/ytweb_trailer.log" || { echo "a refused trailer did not open in Cuelight YouTube"; grep -E "detail|youtube|video|ytweb" "$OUT/ytweb_trailer.log"; exit 1; }
# an addon's trailer id that tries to close the TV's launch request and name another app: never launched
run ytweb_bad_id 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with ytwbad "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=1 SNTV_FAKE_BROWSER=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/badyt/manifest.json
! grep -q "video: Cuelight YouTube opened\|video: the YouTube app opened" "$OUT/ytweb_bad_id.log" || { echo "an addon's made-up YouTube id was handed to a launch"; grep -E "youtube|YouTube|video:" "$OUT/ytweb_bad_id.log"; exit 1; }
run ytweb_offer 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with ytw2 "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=0 SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo-yt.json
grep -q "UI check: screen=options" "$OUT/ytweb_offer.log" || { echo "Cuelight YouTube was not offered for a refused trailer"; exit 1; }
run ytweb_install 700 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with ytw3 "$ABOUT_HOME")" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=0 SNTV_FAKE_INSTALL=1 SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo-yt.json
grep -q "ytweb: installing Cuelight YouTube" "$OUT/ytweb_install.log" && grep -q "ytweb: installed" "$OUT/ytweb_install.log" && grep -q "video: Cuelight YouTube opened" "$OUT/ytweb_install.log" \
  || { echo "Cuelight YouTube was not installed and opened"; grep -E "ytweb|youtube|video|UI check" "$OUT/ytweb_install.log"; exit 1; }
# it skips sponsor segments unless turned off, and leaves adverts out only when asked (Settings > Interface)
grep -q "video: Cuelight YouTube opened [A-Za-z0-9_-]* at 0s (play), skipping sponsors$" "$OUT/ytweb_trailer.log" || { echo "Cuelight YouTube was not told to skip sponsor segments by default"; grep "Cuelight YouTube" "$OUT/ytweb_trailer.log"; exit 1; }
run ytweb_flags 400 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with ytwfl '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ytSkipSponsors":0,"ytNoAdverts":1}')" SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=1
grep -q "video: Cuelight YouTube opened [A-Za-z0-9_-]* at 0s (play), without adverts$" "$OUT/ytweb_flags.log" || { echo "Cuelight YouTube was not given the viewer's advert and sponsor settings"; grep "Cuelight YouTube" "$OUT/ytweb_flags.log"; exit 1; }
run ytweb_adverts_setting 600 "left,down,down,down,down,down,ok,up,ok,t:block youtube adverts,down,down,down,down,left,ok,ok,wait,wait" HOME="$(home_with ytwad "$ABOUT_HOME")" SNTV_FAKE_YTWEB=1
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get('ytNoAdverts')==1 and d.get('ytSkipSponsors',1)==1 else 1)" "$OUT/home_ytwad/.local/share/stremio/nativetv/settings.json" \
  || { echo "Block YouTube adverts did not switch on from Settings"; grep -E "settings" "$OUT/ytweb_adverts_setting.log"; exit 1; }
run settings_find 420 "left,down,down,down,down,down,ok,up,ok,t:subtitle,down,down,down,down,left,ok"
grep -q 'settings: "subtitle" matches [2-8]' "$OUT/settings_find.log" || { echo "Find a setting did not list the subtitle settings"; grep settings "$OUT/settings_find.log"; exit 1; }
run settings_find_one 340 "left,down,down,down,down,down,ok,up,ok,t:oled,down,down,down,down,left,ok,right"
grep -q "settings: found Pure black (OLED) in Interface" "$OUT/settings_find_one.log" || { echo "a single match did not open its row"; grep settings "$OUT/settings_find_one.log"; exit 1; }
# recommended addons are found by name, though the rest of Addons (the viewer's own) is not searched
run settings_find_addon 340 "left,down,down,down,down,down,ok,up,ok,t:public domain,down,down,down,down,left,ok"
grep -q "settings: found Public Domain Movies in Addons" "$OUT/settings_find_addon.log" || { echo "Find a setting did not find a recommended addon"; grep settings "$OUT/settings_find_addon.log"; exit 1; }
# Help for this device, opened on Get Cuelight on other devices: OK on a card opens its page, or says to scan it
run help_get 460 "left,down,down,down,down,down,ok,up,ok,t:get cuelight,down,down,down,down,left,ok,ok,wait,right,ok,wait"
grep -q "help: 7 topics for Computer" "$OUT/help_get.log" && grep -qE "help: opened the page for LG TV|toast: No browser here" "$OUT/help_get.log" \
  || { echo "Help did not open on Get Cuelight, or its card did nothing"; grep -E "help|toast|settings" "$OUT/help_get.log"; exit 1; }
# the screensaver, as it is set up, from Settings
run ambient_preview 600 "left,down,down,down,down,down,ok,up,ok,t:preview the screensaver,down,down,down,down,left,ok,ok,wait,wait,wait,wait" HOME="$(home_with ambprev '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":2}')"
grep -q "settings: previewing the screensaver" "$OUT/ambient_preview.log" && grep -q "UI check: screen=ambient" "$OUT/ambient_preview.log" \
  || { echo "the screensaver could not be previewed from Settings"; grep -E "settings|ambient|UI check" "$OUT/ambient_preview.log"; exit 1; }
run intro 70 "" SNTV_INTRO=1 SNTV_FRAME_DT=0.03 SNTV_NO_TRANSITIONS=
grep -q "intro: done" "$OUT/intro.log" || { echo "the launch animation did not finish"; exit 1; }
run detail_trailer 320 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,back" HOME="$(home_with trailer "$ABOUT_HOME")"
grep -q "youtube: dQw4w9WgXcQ as a byte-range playlist at 1080p (10 pieces)" "$OUT/detail_trailer.log" && grep -q "youtube: dQw4w9WgXcQ as DASH at 1080p (1 video quality, pieces listed)" "$OUT/detail_trailer.log" && grep -q "detail: trailer for tt0111161 streaming" "$OUT/detail_trailer.log" && grep -q "player: candidate 1/4 Trailer - best @0s -> http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ.m3u8" "$OUT/detail_trailer.log" \
  || { echo "the trailer did not play"; grep -E "detail|youtube|player" "$OUT/detail_trailer.log"; exit 1; }
# IMDb has the trailer: its 1080p MP4 plays first, its playlist and 480p behind it, no YouTube asked
run detail_trailer_imdb 400 "wait,ok,right,ok,wait,wait,wait,wait" HOME="$(home_with trailer_imdb "$ABOUT_HOME")" SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql/with"
grep -q "detail: trailer for tt0111161 from IMDb (3 qualities, best 1080p)" "$OUT/detail_trailer_imdb.log" && grep -q "player: candidate 1/3 IMDb - 1080p" "$OUT/detail_trailer_imdb.log" \
  && ! grep -q "youtube: asking to stream" "$OUT/detail_trailer_imdb.log" \
  || { echo "the IMDb trailer did not play first"; grep -E "detail|youtube|player: candidate" "$OUT/detail_trailer_imdb.log"; exit 1; }
# resting on a title page: its IMDb trailer plays muted behind the artwork, and gives way to the Trailer button's player
run bg_trailer 1300 "wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,right,ok,wait,wait,wait" HOME="$(home_with bg_trailer "$ABOUT_HOME")" SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql/with"
grep -q "trailer: tt0111161 plays behind the artwork (1080p, muted)" "$OUT/bg_trailer.log" && grep -q "video(null): muted" "$OUT/bg_trailer.log" \
  && grep -q "trailer: tt0111161 gives way to another video" "$OUT/bg_trailer.log" && grep -q "player: candidate 1/3 IMDb - 1080p" "$OUT/bg_trailer.log" \
  || { echo "the trailer did not play behind the title page"; grep -E "trailer|video|player: candidate" "$OUT/bg_trailer.log"; exit 1; }
# on Home the hero's trailer plays too; turned off, nothing does
run bg_trailer_home 1300 "wait" HOME="$(home_with bg_trailer_home "$ABOUT_HOME")" SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql/with"
grep -q "trailer: tt[0-9]* plays behind the artwork" "$OUT/bg_trailer_home.log" || { echo "the Home hero's trailer did not play"; grep -E "trailer|video" "$OUT/bg_trailer_home.log"; exit 1; }
run bg_trailer_off 1300 "wait,ok" HOME="$(home_with bg_trailer_off '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"autoTrailers":0}')" SNTV_IMDB_API="http://127.0.0.1:$PORT/imdbgql/with"
! grep -q "plays behind the artwork" "$OUT/bg_trailer_off.log" || { echo "a trailer played with the setting off"; exit 1; }
# YouTube wants a token for the visionOS client's files: the TV app's are solved and tested instead
run detail_trailer_tv 1500 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with trailer_tv "$ABOUT_HOME")" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/pot/manifest.json"
grep -q "youtube: potPOT00001 refused the VISIONOS client's separate video and audio (403)" "$OUT/detail_trailer_tv.log" && grep -q "youtube: potPOT00001 separate video and audio from the TV client" "$OUT/detail_trailer_tv.log" \
  && grep -q "youtube: potPOT00001 as a byte-range playlist at 1080p" "$OUT/detail_trailer_tv.log" \
  || { echo "refused visionOS files were not replaced by the TV app's"; grep -E "youtube|player: candidate" "$OUT/detail_trailer_tv.log"; exit 1; }
# both sharp ways failed on this TV: YouTube's own playlist goes first (the others every tenth time)
run detail_trailer_hls_first 320 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,back" HOME="$(home_with trailer_hls '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ytFailLocalHls":1,"ytFailDash":1}')"
grep -q "youtube: dQw4w9WgXcQ 2 way(s) failed here before" "$OUT/detail_trailer_hls_first.log" && grep -q "player: candidate 1/4 .* -> http://127.0.0.1:[0-9]*/yt?u=" "$OUT/detail_trailer_hls_first.log" \
  || { echo "a TV where DASH fails did not start with the playlist"; grep -E "youtube|player: candidate" "$OUT/detail_trailer_hls_first.log"; exit 1; }
# no trailer from the addon: TMDB's, a file with no playlist, fetched in YouTube's &range= pieces
run detail_trailer_file 360 "wait,ok,right,ok,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with trailer_file '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}')" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/notrailer/manifest.json SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
grep -q "youtube: fileOnly001 streams as a file" "$OUT/detail_trailer_file.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/yt?u=" "$OUT/detail_trailer_file.log" \
  || { echo "the TMDB trailer did not stream through the chunked proxy"; grep -E "detail|youtube|video" "$OUT/detail_trailer_file.log"; exit 1; }
LIBF="$OUT/home_trailer/.local/share/stremio/nativetv/library.json"
[ ! -f "$LIBF" ] || ! grep -q tt0111161 "$LIBF" || { echo "watching the trailer saved progress for the film"; exit 1; }
run episodes 220 "right,ok,down,down,right"
run streams 240 "right,ok,down,down,play"
run streams_best 320 "right,ok,down,down,yellow,down,down,down,ok,up" HOME="$(home_with best '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"qualityMode":1}')"
run player 280 "right,ok,down,down,play,wait"
run player_subs 360 "right,ok,down,down,play,wait,down,right,right,right,ok" HOME="$(home_with subs1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# the video's own track stays (always in sync) over a file matched to it, unless the setting asks for files
! grep -q "player: subtitles: the file matched to this video, before its own track" "$OUT/player_subs.log" || { echo "a matched file replaced the video's own track"; grep -E "subtitles|subs:" "$OUT/player_subs.log"; exit 1; }
grep -q "player: subtitles in" "$OUT/player_subs.log" || { echo "the Subtitles sheet did not open"; grep -E "drawer|subtitles" "$OUT/player_subs.log"; exit 1; }
run player_subs_files 360 "right,ok,down,down,play,wait" HOME="$(home_with subsfiles '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"subsPreferFiles":1}')"
grep -q "player: subtitles: the file matched to this video, before its own track" "$OUT/player_subs_files.log" || { echo "with files preferred, the video's own track was kept"; grep -E "subtitles|subs:" "$OUT/player_subs_files.log"; exit 1; }
run player_audio 400 "right,ok,down,down,play,wait,down,right,right,right,right,ok,down"
run player_cue 300 "right,ok,down,down,play,wait" SNTV_FAKE_NO_TEXT_TRACKS=1
run player_seek 268 "right,ok,down,down,play,wait,right,right,right" SNTV_FAKE_DURATION=1
# an episode watched to the end, the next one not played: the series stays in Continue Watching at the next episode
# (played to its end: a seek or a resume this close counts once it plays most of what was left)
CWH="$(home_with cw_next '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"autoplayNext":0}')"
run cw_next_episode 2400 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,back,back,back,wait,back,wait,up,wait" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=125 HOME="$CWH"
grep -q '"video_id":"tt0903747:1:2"' "$CWH/.local/share/stremio/nativetv/library.json" && grep -q '"timeOffset":1[,}]' "$CWH/.local/share/stremio/nativetv/library.json" \
  || { echo "a finished episode dropped the series from Continue Watching"; grep -o '"video_id":[^,]*\|"timeOffset":[^,]*' "$CWH/.local/share/stremio/nativetv/library.json"; exit 1; }
run upnext_card 300 "right,ok,down,down,play,wait" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_card '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run upnext_auto 380 "right,ok,down,down,play,wait" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 HOME="$(home_with fresh_upnext_auto '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# Seamless binge: the next episode opens on its still grown out of the Up Next card (no loading card), the picture under it
run upnext_bridge 560 "right,ok,down,down,play,wait" SNTV_FAKE_OPEN_S=1.5 SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 HOME="$(home_with upbridge '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: tt0903747:1:2 opens on its still, out of the Up Next card" "$OUT/upnext_bridge.log" && grep -qE "player: the picture came 1\.[0-9] s after the still" "$OUT/upnext_bridge.log" \
  && ! grep -q "player: tt0903747:1:1 opens on its still" "$OUT/upnext_bridge.log" \
  || { echo "the next episode did not open on its still"; grep -E "player: (on to|tt0903747|the picture)" "$OUT/upnext_bridge.log"; exit 1; }
# Speed: a playing picture with nothing over it, and the screensaver, are not drawn every frame
# (no screenshots here: they draw every frame; past the intro, whose Skip button is drawn; a profile of its own,
# so nothing resumes before it). Frames per 2 s: playback's fewest (its opening card and controls draw at full rate),
# the screensaver's last (steady by then).
idle_rate() { # name screen keys [env...]: the app left alone for 26 s; frames per 2 s while that screen was up
  local name="$1" screen="$2" keys="$3"; shift 3
  env "$@" SNTV_PERF_FILL=1 SNTV_KEYS="$keys" timeout 26 xvfb-run -a -n "${SMOKE_XVFB_N:-99}" -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" >"$OUT/$name.log" 2>&1 || true
  grep -o "perf: [0-9]* frames in [0-9.]* s on $screen\$" "$OUT/$name.log" | grep -o "perf: [0-9]*" | grep -o "[0-9]*"
}
r="$(idle_rate idle_playing player "right,ok,down,down,play" SNTV_FAKE_START=90 HOME="$(home_with idleplay '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" | sort -n | head -1)"; [ -n "$r" ] && [ "$r" -lt 40 ] || { echo "a bare playing picture still draws $r frames in 2 s"; grep "perf: .* frames in" "$OUT/idle_playing.log"; exit 1; }
r="$(idle_rate idle_ambient ambient "wait" SNTV_AMBIENT_SECONDS=1 | tail -1)"; [ -n "$r" ] && [ "$r" -lt 80 ] || { echo "the screensaver draws $r frames in 2 s"; grep "perf: .* frames in" "$OUT/idle_ambient.log"; exit 1; }
echo "ok: idle rates"
# OK on the card is its Play now (autoplay off, so only the key can start the next episode)
# Up Next with no bingeGroup: the next episode plays from the same release group (NTb) by itself
run upnext_chain 520 "right,ok,down,down,play,wait" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/chain/manifest.json HOME="$(home_with upnext_chain '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "streams: next episode like group NTb" "$OUT/upnext_chain.log" && grep -q "streams: next episode from the same release" "$OUT/upnext_chain.log" && grep "player: candidate 1/" "$OUT/upnext_chain.log" | sed -n 2p | grep -q "rel=ntb" \
  || { echo "the next episode did not stay on the same release"; grep -E "streams:|player: candidate" "$OUT/upnext_chain.log"; exit 1; }
run upnext_ok 520 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_ok '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"autoplayNext":0}')"
[ "$(grep -c 'player: candidate' "$OUT/upnext_ok.log")" -ge 2 ] || { echo "OK on the Up Next card did not start the next episode"; grep player "$OUT/upnext_ok.log"; exit 1; }
# leaving a video mid-way: the island offers it back on Home, and Blue opens it
run island_resume 520 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,back,back,back,back,wait,blue" SNTV_FAKE_START=200 HOME="$(home_with island '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "island: resume tt0903747" "$OUT/island_resume.log" || { echo "Blue did not open the title left mid-way"; grep -i "island\|player" "$OUT/island_resume.log"; exit 1; }
# Blue opens it from any screen (Settings here); not shown on that title's own page, whose Resume button is there
run island_resume_detail 620 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,back,back,back,wait,blue,back,left,down,down,down,down,down,ok,wait,blue" SNTV_FAKE_START=200 HOME="$(home_with island_d '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
[ "$(grep -c "island: resume tt0903747" "$OUT/island_resume_detail.log")" = 1 ] || { echo "Blue did not open the title left mid-way from Settings only"; grep -iE "island|settings" "$OUT/island_resume_detail.log"; exit 1; }
# by touch: a tap on the card opens it (a phone has no Blue key; the card shows no Blue label there)
run island_resume_tap 560 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,back,back,back,back,wait,tap:1650:88,wait" SNTV_FAKE_START=200 HOME="$(home_with island_t '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "island: resume tt0903747" "$OUT/island_resume_tap.log" || { echo "a tap on the resume card did not open the title"; grep -iE "island|UI check" "$OUT/island_resume_tap.log"; exit 1; }
run island_resume_up 540 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,back,back,back,back,wait,up,up,up,ok" SNTV_FAKE_START=200 HOME="$(home_with island_up '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "island: resume tt0903747" "$OUT/island_resume_up.log" || { echo "Up and OK did not open the resume card"; grep -i "island" "$OUT/island_resume_up.log"; exit 1; }
run island_resume_focus 500 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,back,back,back,back,wait,up,up,up" SNTV_FAKE_START=200 HOME="$(home_with island_f '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# next episode's sources still arriving (the /slow addon answers 6 s late): the Up Next
# button and the control bar's Next button fill as a loader
SLOW="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/slow/manifest.json"
# the next episode on its way after the countdown (the slow addon holds its sources 6 s) with
# the screensaver due after 1 s idle: it waits for the episode, which then starts by itself
run upnext_screensaver 1400 "right,ok,down,down,play,wait" "$SLOW" SNTV_STILL_WATCHING_AFTER=99 SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 SNTV_AMBIENT_SECONDS=1 HOME="$(home_with upsaver '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# (a fast machine plays the whole fake series in the run: the screensaver after its last one, nothing queued, is fine)
[ "$(awk '/ambient: .* screensaver/ { exit } /player: candidate/ { n++ } END { print n + 0 }' "$OUT/upnext_screensaver.log")" -ge 2 ] \
  && awk '/streams: next episode like/ { q = 1 } /player: candidate/ { q = 0 } /ambient: .* screensaver/ && q { bad = 1 } END { exit bad }' "$OUT/upnext_screensaver.log" \
  || { echo "the screensaver took the handoff to the next episode"; grep -E "ambient|streams: next|player: candidate" "$OUT/upnext_screensaver.log"; exit 1; }
# OK on the Up Next card while the next episode is still being fetched (the slow addon holds it
# 6 s): the card waits ("Starts once ready"), then the episode starts at once, no source list between
run upnext_wait_ready 2200 "right,ok,down,down,play,wait,wait,ok" "$SLOW" SNTV_FAKE_START=3002 HOME="$(home_with upwait '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: next episode asked for while it's prepared" "$OUT/upnext_wait_ready.log" && grep -q "player: the next episode is ready (asked for on the card)" "$OUT/upnext_wait_ready.log" \
  && grep -q "player: on to tt0903747:1:2, 100% of its addons answered ahead" "$OUT/upnext_wait_ready.log" && [ "$(grep -c "player: candidate" "$OUT/upnext_wait_ready.log")" -ge 2 ] \
  || { echo "the Up Next card did not wait for the next episode to be ready"; grep -E "player: (next|the next|on to|candidate)|streams: prefetch" "$OUT/upnext_wait_ready.log"; exit 1; }
# the next episode handed over with screen transitions on (as on a TV) after a raced start:
# the screen it replaces leaves after the new one starts, and must not call off its start (or
# free the list the player now has: that crashed)
run upnext_handoff_transitions 900 "right,ok,down,down,play,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/lighter/manifest.json,http://127.0.0.1:$PORT/manifest.json SNTV_NO_TRANSITIONS= SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 HOME="$(home_with handoff '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')"
! grep -q "streams: race called off" "$OUT/upnext_handoff_transitions.log" && grep -q "player: on to tt0903747:1:2" "$OUT/upnext_handoff_transitions.log" && [ "$(grep -c "player: candidate" "$OUT/upnext_handoff_transitions.log")" -ge 2 ] \
  || { echo "the screen left behind called off the next episode's start"; grep -E "streams: (race|next|racing)|player: (candidate|on to)" "$OUT/upnext_handoff_transitions.log"; exit 1; }
# Journeys from the audit. A next episode with nothing that may start by itself: the list
# says so and the screensaver may come (it spun, holding both screensavers off, all night)
run journey_nothing_plays 1500 "right,ok,down,down,play,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/ep2wrong/manifest.json SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 SNTV_AMBIENT_SECONDS=3 HOME="$(home_with jnothing '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "streams: no source here can start by itself" "$OUT/journey_nothing_plays.log" && grep -q "ambient: .* screensaver" "$OUT/journey_nothing_plays.log" \
  || { echo "a next episode with nothing playable spun forever"; grep -E "streams|ambient|player: on to" "$OUT/journey_nothing_plays.log"; exit 1; }
# nobody pressing a key: after the set number of episodes went on by themselves, "Still watching?"
run journey_still_watching 1500 "right,ok,down,down,play,wait" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=128.5 SNTV_STILL_WATCHING_AFTER=1 HOME="$(home_with jstill '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: 1 episodes went on by themselves; asking if anyone is still watching" "$OUT/journey_still_watching.log" && [ "$(grep -c "player: on to" "$OUT/journey_still_watching.log")" = 1 ] \
  || { echo "autoplay went on with nobody watching"; grep -E "player: (on to|.*asking)" "$OUT/journey_still_watching.log"; exit 1; }
# a seek to the end to peek, then Back: not watched (Continue Watching stays on the episode)
run journey_peek_end 600 "right,ok,down,down,play,wait,wait,back" SNTV_FAKE_DURATION=3000 SNTV_FAKE_START=2950 HOME="$(home_with jpeek '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q '"video_id":"tt0903747:1:1"' "$OUT/home_jpeek/.local/share/stremio/nativetv/library.json" \
  || { echo "a peek at the end marked the episode watched"; cat "$OUT/home_jpeek/.local/share/stremio/nativetv/library.json" | head -c 600; exit 1; }
# the episode ends with the Audio menu open: it closes, the next one starts
run journey_menu_at_end 1300 "right,ok,down,down,play,wait,down,right,right,right,right,ok" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=125 HOME="$(home_with jmenu '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: the episode ended with a menu open; closing it for the next one" "$OUT/journey_menu_at_end.log" && grep -q "player: on to tt0903747:1:2" "$OUT/journey_menu_at_end.log" \
  || { echo "a menu open at the end held the next episode"; grep -E "player: (on to|the episode)" "$OUT/journey_menu_at_end.log"; exit 1; }
# an update found while Play's pick is being made (the slow addon): the prompt does not cover it
run journey_update_during_start 1200 "right,ok,down,down,right,play" "$SLOW" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json HOME="$(home_with jupd '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "update: 9.9.9 available" "$OUT/journey_update_during_start.log" && grep -q "player: candidate" "$OUT/journey_update_during_start.log" \
  || { echo "the update prompt held a start"; grep -E "update|player: candidate|streams" "$OUT/journey_update_during_start.log"; exit 1; }
# Restart on a title with a saved place (S2E3 at 25 min): from 0:00
RH="$(home_with jrestart '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"; cp "$ROOT/tests/fixtures/library.json" "$RH/.local/share/stremio/nativetv/library.json"
run journey_restart 900 "wait,wait,ok,wait,wait,right,ok,wait,wait,wait,ok,wait" HOME="$RH"
grep -q "player: candidate 1/[0-9]* .* @0s" "$OUT/journey_restart.log" && grep -q "markers from TheIntroDB for tt0903747:2:3" "$OUT/journey_restart.log" \
  || { echo "Restart did not start the episode from 0:00"; grep -E "player: candidate|TheIntroDB" "$OUT/journey_restart.log"; exit 1; }
run upnext_loading 420 "right,ok,down,down,play,wait" "$SLOW" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=100 HOME="$(home_with fresh_upnext_loading '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# the next episode's sources are asked for early in this one (here after 1 s of playing,
# 30 s on a TV), not only near the end or when the control bar is open
run next_early 500 "right,ok,down,down,play,wait,wait" HOME="$(home_with nextearly '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_FAKE_DURATION=3000 SNTV_PREFETCH_AFTER=1
grep -q "streams: prefetching tt0903747:1:2" "$OUT/next_early.log" || { echo "the next episode was not prepared early"; grep -E "streams:|player: candidate" "$OUT/next_early.log"; exit 1; }
# and asked again in the last minutes when the answers have aged (here after 1 s)
run next_renewed 600 "right,ok,down,down,play,wait,wait,wait" HOME="$(home_with nextrenew '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_FAKE_DURATION=600 SNTV_FAKE_START=200 SNTV_PREFETCH_RENEW=1
grep -q "streams: asking again for tt0903747:1:2" "$OUT/next_renewed.log" || { echo "the next episode's sources were not renewed near the end"; grep -E "streams:" "$OUT/next_renewed.log"; exit 1; }
# a source named for another episode never plays by itself, however good it looks
run wrong_episode_skipped 500 "right,ok,down,down,play,wait,wait" HOME="$(home_with wrongep '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/wrongep/manifest.json" SNTV_NO_RACE=1
grep -q "streams: #1 score [0-9]*  Wrong Episode" "$OUT/wrong_episode_skipped.log" && grep -q "player: candidate 1/" "$OUT/wrong_episode_skipped.log" && ! grep "player: candidate" "$OUT/wrong_episode_skipped.log" | grep -q "?ep=4" \
  || { echo "a source named for another episode was played or queued"; grep -E "player: candidate|streams: #" "$OUT/wrong_episode_skipped.log"; exit 1; }
# Sources that ask for request headers play through the local relay (the test player
# fetches what it opens: /hdrvideo/ refuses anyone without its Referer); with the relay off
# they fail as before; a seek's mid-file bytes come through; a source the relay doesn't get
# playing is opened directly, then the next source as usual
HDR="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/hdr/manifest.json"
HDRP='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}'
run relay_plays 600 "right,ok,down,down,play,wait,wait" "$HDR" SNTV_FAKE_FETCH=1 HOME="$(home_with relay1 "$HDRP")"
grep -q "player: source 1 asks for request headers; it plays through the local relay" "$OUT/relay_plays.log" && grep -q "video(null): fetched 65536 bytes, status 206" "$OUT/relay_plays.log" \
  && grep -q "start: [0-9.]* s Play:" "$OUT/relay_plays.log" && ! grep -q "start: .*(failed)" "$OUT/relay_plays.log" && ! grep -qE "relay: .*(Referer|hdr\.example)" "$OUT/relay_plays.log" \
  || { echo "a source asking for a Referer did not play through the relay"; grep -E "relay|player: (candidate|source)|video\(null\)" "$OUT/relay_plays.log"; exit 1; }
run relay_off 600 "right,ok,down,down,play,wait,wait" "$HDR" SNTV_FAKE_FETCH=1 SNTV_NO_RELAY=1 HOME="$(home_with relay2 "$HDRP")"
grep -q "the TV's player can't send them" "$OUT/relay_off.log" && ! grep -q "relay:" "$OUT/relay_off.log" \
  || { echo "with the relay off, the header source did not open directly"; grep -E "relay|player: (candidate|source)" "$OUT/relay_off.log"; exit 1; }
run relay_seek 600 "right,ok,down,down,play,wait,wait" "$HDR" SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=131072 HOME="$(home_with relay3 "$HDRP")"
grep -q "relay: 127.0.0.1 from 131072-196607, 206" "$OUT/relay_seek.log" && grep -q "video(null): fetched 65536 bytes, status 206" "$OUT/relay_seek.log" \
  || { echo "a seek's bytes did not come through the relay"; grep -E "relay|video\(null\)" "$OUT/relay_seek.log"; exit 1; }
run relay_fallback 900 "right,ok,down,down,play,wait,wait" "$HDR" SNTV_FAKE_FETCH=1 SNTV_FAKE_FAIL_LOADS=1 HOME="$(home_with relay4 "$HDRP")"
grep -q "player: source 1 did not start through the relay; opening its link directly" "$OUT/relay_fallback.log" && grep -q "player: candidate 2/" "$OUT/relay_fallback.log" \
  || { echo "a source the relay did not get playing was not opened directly, then the next"; grep -E "relay|player: (candidate|source)" "$OUT/relay_fallback.log"; exit 1; }
# The buffer ahead: a source plays through the relay with a ring in memory; the player's
# first bytes and a seek's come out of it; with too little memory free there is no buffer
# and it still plays.
run buffer_plays 600 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_SPEED_SAMPLE_KB=64 HOME="$(home_with bufplay "$HDRP")"
grep -q "speed: this home carries about [0-9]* Mbit/s" "$OUT/buffer_plays.log" || { echo "the buffered play did not teach the home speed"; grep -E "relay|speed" "$OUT/buffer_plays.log"; exit 1; }
grep -q "relay: a buffer in memory, up to [0-9]* MB for now" "$OUT/buffer_plays.log" \
  && grep -q "player: source 1 plays through the local relay, loaded ahead" "$OUT/buffer_plays.log" && grep -q "relay: 127.0.0.1 from 0-65535, 206 (from the buffer)" "$OUT/buffer_plays.log" \
  && grep -q "video(null): fetched 65536 bytes, status 206$" "$OUT/buffer_plays.log" && grep -q "relay: buffering 127.0.0.1 ahead" "$OUT/buffer_plays.log" \
  || { echo "a source did not play out of the buffer ahead"; grep -E "relay|player: (candidate|source)|video\(null\)" "$OUT/buffer_plays.log"; exit 1; }
run buffer_seek 600 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=131072 HOME="$(home_with bufseek "$HDRP")"
grep -q "relay: 127.0.0.1 from 131072-196607, 206 (from the buffer)" "$OUT/buffer_seek.log" && grep -q "video(null): fetched 65536 bytes, status 206$" "$OUT/buffer_seek.log" \
  || { echo "a seek's bytes did not come out of the buffer"; grep -E "relay|video\(null\)" "$OUT/buffer_seek.log"; exit 1; }
# a source that drops the connection partway: the buffer picks up from that byte, and a read past it still comes
run buffer_drop 700 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=262144 SNTV_FAKE_FETCH_PATTERN=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/dropvid/manifest.json HOME="$(home_with bufdrop "$HDRP")"
grep -q "relay: 127.0.0.1 dropped at 0.1 MB; going on from there" "$OUT/buffer_drop.log" && grep -q "video(null): the bytes are the file's" "$OUT/buffer_drop.log" && grep -q "relay: 127.0.0.1 from 262144-327679, 206 (from the buffer)" "$OUT/buffer_drop.log" && grep -q "video(null): fetched 65536 bytes, status 206$" "$OUT/buffer_drop.log" && ! grep -q "did not start through the relay" "$OUT/buffer_drop.log" \
  || { echo "a dropped download was not picked up again"; grep -E "relay|video\(null\)" "$OUT/buffer_drop.log"; exit 1; }
# the bytes out of the buffer are the file's, also once its blocks have gone round (300 KB in 64 KB blocks, read at 700 KB)
run buffer_ring 700 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=700000 SNTV_FAKE_FETCH_PATTERN=1 SNTV_FAKE_BUFFER_KB=300 SNTV_FAKE_BLOCK_KB=64 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/dropvid/manifest.json HOME="$(home_with bufring "$HDRP")"
grep -q "relay: 127.0.0.1 from 700000-765535, 206 (from the buffer)" "$OUT/buffer_ring.log" && grep -q "video(null): the bytes are the file's" "$OUT/buffer_ring.log" && ! grep -q "did not start through the relay" "$OUT/buffer_ring.log" \
  || { echo "the bytes out of a ring that went round were not the file's"; grep -E "relay|video\(null\)" "$OUT/buffer_ring.log"; exit 1; }
# a seek's reads that start just behind what is in (an earlier keyframe), or at the file's start (players read its header
# again), do not throw away what is buffered ahead: those bytes come from the source, the rest still from the buffer
run buffer_keeps 900 "right,ok,down,down,play,wait,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=700000 SNTV_FAKE_FETCH_THEN=0,600000,780000 SNTV_FAKE_FETCH_PATTERN=1 SNTV_FAKE_BUFFER_KB=300 SNTV_FAKE_BLOCK_KB=64 SNTV_FAKE_HEAD_KB=64 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/dropvid/manifest.json HOME="$(home_with bufkeep "$HDRP")"
grep -q "relay: the file's start asked again; from the source, the buffer stays" "$OUT/buffer_keeps.log" && grep -q "video(null): then 65536 bytes from 600000, status 206$" "$OUT/buffer_keeps.log" \
  && grep -q "video(null): then 65536 bytes from 780000, status 206$" "$OUT/buffer_keeps.log" && ! grep -q "the buffer starts again there" "$OUT/buffer_keeps.log" \
  || { echo "a step back or a header read threw away the buffer ahead"; grep -E "relay|video\(null\)" "$OUT/buffer_keeps.log"; exit 1; }
run buffer_no_room 600 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FREE_MB=200 HOME="$(home_with bufroom "$HDRP")"
grep -q "relay: 200 MB of memory free on the TV: no buffer ahead" "$OUT/buffer_no_room.log" && grep -q "video(null): fetched 65536 bytes, status 206$" "$OUT/buffer_no_room.log" && ! grep -q "from the buffer" "$OUT/buffer_no_room.log" \
  || { echo "with too little memory free, the buffer was made or the source did not play"; grep -E "relay|video\(null\)" "$OUT/buffer_no_room.log"; exit 1; }
# the TV runs short of memory while it plays: the buffer gives its memory back and the source plays on
run buffer_shrinks 1400 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FREE_MB_LATER=100 HOME="$(home_with bufshrink "$HDRP")"
grep -q "relay: the TV is short of memory; the buffer lets go and the source plays on directly" "$OUT/buffer_shrinks.log" && grep -q "video(null): fetched 65536 bytes, status 206$" "$OUT/buffer_shrinks.log" \
  && ! grep -q "did not start through the relay\|player: candidate 2/" "$OUT/buffer_shrinks.log" \
  || { echo "the buffer did not let go when memory ran short, or playback suffered"; grep -E "relay|player: (candidate|source)|video\(null\)" "$OUT/buffer_shrinks.log"; exit 1; }
# closing the app while the source has gone quiet mid-download: it still closes at once
run buffer_quit 600 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/stallvid/manifest.json HOME="$(home_with bufquit "$HDRP")"
ms=$(sed -n 's/.*main: closed in \([0-9]*\) ms.*/\1/p' "$OUT/buffer_quit.log")
grep -q "relay: buffering 127.0.0.1 ahead\|from the buffer" "$OUT/buffer_quit.log" && [ -n "$ms" ] && [ "$ms" -lt 4000 ] \
  || { echo "the app did not close promptly with a download stuck (${ms:-no} ms)"; grep -E "relay|http:|main: closed" "$OUT/buffer_quit.log"; exit 1; }
# the source dies partway: the buffer carries on from another link to the very file (proved
# byte for byte), and the player never knows; a look-alike of the same size is not taken, and
# the player moves to the next source while the buffer still plays
run buffer_handoff 900 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_FAKE_FETCH_FROM=262144 SNTV_FAKE_FETCH_PATTERN=1 SNTV_NO_RACE=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/dievid/manifest.json HOME="$(home_with bufhand "$HDRP")"
grep -q "player: 1 other link may be the same file" "$OUT/buffer_handoff.log" && grep -q "relay: 127.0.0.1 stopped sending at [0-9.]* MB; carrying on from the same file at 127.0.0.1" "$OUT/buffer_handoff.log" \
  && grep -q "video(null): the bytes are the file's" "$OUT/buffer_handoff.log" && grep -q "relay: 127.0.0.1 from 262144-327679, 206 (from the buffer)" "$OUT/buffer_handoff.log" \
  && ! grep -q "player: candidate 2/\|player: rescue" "$OUT/buffer_handoff.log" \
  || { echo "a dying source was not carried on from its identical copy"; grep -E "relay|player: (candidate|source|rescue|[0-9] other)|video\(null\)" "$OUT/buffer_handoff.log"; exit 1; }
run buffer_switch 900 "right,ok,down,down,play,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_SWITCH_AHEAD=100000 SNTV_NO_RACE=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/dieother/manifest.json HOME="$(home_with bufswitch "$HDRP")"
grep -q "relay: 127.0.0.1 is not the same file; not carrying on from it" "$OUT/buffer_switch.log" && grep -q "relay: 127.0.0.1 stopped sending at [0-9.]* MB; what is in plays on" "$OUT/buffer_switch.log" \
  && grep -q "player: the source stopped sending; [0-9]* s of video left, switching now" "$OUT/buffer_switch.log" && grep -q "player: candidate 2/" "$OUT/buffer_switch.log" \
  || { echo "a dying source with no identical copy did not switch before the buffer ran out"; grep -E "relay|player: (candidate|source|rescue|the source)|video\(null\)" "$OUT/buffer_switch.log"; exit 1; }
# Faster starts. Resting on an episode checks the source Play would start (the /fast addon's
# cached copy, behind a redirect): Play reuses that check, runs no race, opens where the
# link led, and the start is timed
FAST="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/fast/manifest.json"
run start_checked_ahead 1200 "right,ok,down,down,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,play,wait,wait" "$FAST" HOME="$(home_with ahead '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "streams: checking the best of [0-9]* prefetched sources ahead" "$OUT/start_checked_ahead.log" && grep -q "streams: 1 source checked already" "$OUT/start_checked_ahead.log" \
  && grep -q "streams: source checked [0-9]* s ago; no race" "$OUT/start_checked_ahead.log" && grep -q "player: source 1 opens where its link led a moment ago" "$OUT/start_checked_ahead.log" \
  && grep -q "start: [0-9.]* s Play: sources" "$OUT/start_checked_ahead.log" \
  || { echo "Play did not reuse the check made ahead (or the redirect, or the timing)"; grep -E "streams|probe|player: (candidate|source)|start:" "$OUT/start_checked_ahead.log"; exit 1; }
# the address the link led to fails to load: the link itself is opened, still source 1
run start_final_expired 1200 "right,ok,down,down,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,play,wait,wait" "$FAST" SNTV_FAKE_FAIL_LOADS=1 HOME="$(home_with ahead2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: the address source 1 led to did not play; opening its link" "$OUT/start_final_expired.log" && [ "$(grep -c "player: candidate 1/" "$OUT/start_final_expired.log")" = 2 ] \
  || { echo "an expired redirect address did not fall back to the link"; grep -E "player: (candidate|source|the address)" "$OUT/start_final_expired.log"; exit 1; }
# a slow addon still answering (episode 2) while a cached copy at the quality cap is in: no wait
FASTSLOW="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/fast/manifest.json,http://127.0.0.1:$PORT/slow/manifest.json"
run start_no_wait 700 "right,ok,down,down,right,play,wait,wait" "$FASTSLOW" HOME="$(home_with nowait '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"resCap":1080,"preflight":0}')"
grep -q "streams: the best so far is ready at 1080p; not waiting for [0-9]* slower addon" "$OUT/start_no_wait.log" && grep -q "player: candidate" "$OUT/start_no_wait.log" \
  || { echo "Play waited for a slow addon with a cached copy at the cap already in"; grep -E "streams|player: candidate|start:" "$OUT/start_no_wait.log"; exit 1; }
# OK on an episode: its sources to choose from, and nothing starts by itself (the answers are quick here)
run ep_ok_list 900 "right,ok,down,down,ok" HOME="$(home_with epok '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "UI check: screen=streams" "$OUT/ep_ok_list.log" && ! grep -qE "player: candidate|racing" "$OUT/ep_ok_list.log" \
  || { echo "OK on an episode started a source by itself"; grep -E "streams|player|UI check" "$OUT/ep_ok_list.log" | tail -5; exit 1; }
# an episode's sources opened to play (the remote's Play): moving in the list stops the automatic start (the
# slow addon holds episode 2's answers 6 s, the wait it would start after), a source chosen
# plays, and left alone the best one still starts by itself
run ep_list_choose 1000 "right,ok,down,down,right,play,down" "$SLOW" HOME="$(home_with eplist_choose '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -qE "player: candidate|racing" "$OUT/ep_list_choose.log" && { echo "a source started by itself after the viewer moved in the list"; grep -E "streams|player" "$OUT/ep_list_choose.log" | tail -5; exit 1; }
run ep_list_pick 700 "right,ok,down,down,right,play,down,up,ok" "$SLOW" HOME="$(home_with eplist_pick '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -qE "player: candidate 1/[0-9]+ Fake Addon - Example" "$OUT/ep_list_pick.log" || { echo "the source chosen in the list did not play"; grep -E "streams|player" "$OUT/ep_list_pick.log" | tail -5; exit 1; }
run ep_list_untouched 1000 "right,ok,down,down,right,play" "$SLOW" HOME="$(home_with eplist_untouched '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: candidate" "$OUT/ep_list_untouched.log" || { echo "left alone, the best source did not start"; grep -E "streams|player" "$OUT/ep_list_untouched.log" | tail -5; exit 1; }
# a link answering 200 with an HTML error page is not a video: it loses the start race
run race_error_page 700 "wait,ok,ok,wait,wait,wait,wait,wait,up,ok,wait,wait" HOME="$(home_with errpage '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/errpage/manifest.json"
grep -q "streams: racing" "$OUT/race_error_page.log" && grep -q "player: candidate 1/" "$OUT/race_error_page.log" && ! grep "player: candidate 1/" "$OUT/race_error_page.log" | grep -q expiredpage \
  || { echo "an HTML error page won the start race"; grep -E "streams: (source|racing|#)|player: candidate" "$OUT/race_error_page.log"; exit 1; }
# a link that answers late (a debrid link resolving on first use) is slow, not dead: it is not struck off in the start race
run race_slow_link 700 "wait,ok,ok,wait,wait,wait,wait,wait,up,ok,wait,wait,wait,wait" HOME="$(home_with slowlink '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/slowlink/manifest.json,http://127.0.0.1:$PORT/manifest.json"
grep -q "streams: racing" "$OUT/race_slow_link.log" && grep -q "is slow to answer the race" "$OUT/race_slow_link.log" && ! grep -q "did not answer the race" "$OUT/race_slow_link.log" \
  && grep "player: candidate 1/" "$OUT/race_slow_link.log" | grep -q slowvideo \
  || { echo "a slow link was struck off as dead in the start race"; grep -E "streams: (source|racing|#)|player: candidate" "$OUT/race_slow_link.log"; exit 1; }
# TorBox down: six TorBox sources rank on top; at most three stand by, and once two fail
# the rest of TorBox's are skipped for another addon's (a web scraper)
run torbox_down_failover 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_FAKE_FAIL_LOADS=2 HOME="$(home_with tbdown '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/tbdown/manifest.json,http://127.0.0.1:$PORT/manifest.json"
grep -q "player: TorBox looks down" "$OUT/torbox_down_failover.log" && grep "player: candidate" "$OUT/torbox_down_failover.log" | sed -n 3p | grep -qv "tb=" \
  || { echo "TorBox being down did not move playback to another addon"; grep -E "player: (candidate|source|TorBox)|streams: .*stand by" "$OUT/torbox_down_failover.log"; exit 1; }
# what an addon says about its stream is kept: the headers it needs (the TV's player can't send
# them, so the checks don't either and the source ranks after others), the file's hash
# (subtitles matched to it), and the subtitles it sends along
run stream_hints 700 "wait,ok,ok,wait,wait,wait,wait,wait,up,ok,wait,wait" HOME="$(home_with rich '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/rich/manifest.json" SNTV_NO_RACE=1
grep -q "subs: file hash 0123456789abcdef from the addon" "$OUT/stream_hints.log" && grep -q "subs: 1 subtitle(s) came with the source" "$OUT/stream_hints.log" && grep -q "player: source 1 asks for request headers" "$OUT/stream_hints.log" \
  || { echo "the stream's headers, hash or subtitles were dropped"; grep -E "subs:|probe:|player: candidate" "$OUT/stream_hints.log"; exit 1; }
run player_next_loading 300 "right,ok,down,down,play,wait,down,right" "$SLOW" SNTV_FAKE_START=300
run player_next_ring 300 "right,ok,down,down,play,wait,down,right,right" "$SLOW" SNTV_FAKE_START=300
run player_skipped 300 "right,ok,down,down,play,wait,ok"
# the controls up (Play brings them) and Skip intro showing: OK skips, it does not pause
run player_skip_controls_up 320 "right,ok,down,down,play,wait,play,ok" HOME="$(home_with skipup '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: skipping intro" "$OUT/player_skip_controls_up.log" || { echo "OK with the controls up paused instead of skipping"; grep -E "player:" "$OUT/player_skip_controls_up.log" | tail -5; exit 1; }
run upnext_credits 300 "right,ok,down,down,play,wait" SNTV_FAKE_START=2999 HOME="$(home_with fresh_upnext_credits '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run search 210 "search,t:ar"
run search_grid 260 "search,t:ar,right,right,right,right,right,right,right,right,right,right,right"
run search_keyboard 200 "search,ok" SNTV_PHONE_HOST=192.168.1.20
# movies and series come back in their own groups; Down moves into the next group
run search_groups 320 "search,t:re,right,right,right,right,right,right,right,right,right,right,right,down"
grep -q "search: grouped 2 movies, 1 series, 0 other" "$OUT/search_groups.log" || { echo "search results were not grouped by type"; grep search "$OUT/search_groups.log"; exit 1; }
# an addon whose search answers with its whole catalog: the title typed still comes first
# an addon answering with 200 films, none named like the words typed: the suggestions read past
# the 96th result and wrote over other memory (a TV crashed on the title page opened next)
run search_many_hits 300 "search,t:zebra,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manyhits/manifest.json
grep -q 'search: "Other Film 0" first of 200' "$OUT/search_many_hits.log" || { echo "a search with 200 results did not come back"; grep "search" "$OUT/search_many_hits.log"; exit 1; }
run search_exact_first 260 "search,t:breaking bad" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/noisy/manifest.json
grep -q 'search: "Breaking Bad" first of [0-9]*' "$OUT/search_exact_first.log" \
  || { echo "the title typed was not the first result"; grep "search" "$OUT/search_exact_first.log"; exit 1; }
# suggestions above the keyboard as you type: Up reaches them, OK searches that title
run search_suggest 420 "search,t:br,wait,wait,wait,wait,up,up,up,up,up,ok,wait,wait"
grep -q 'search: 1 suggestion, first "Breaking Bad"' "$OUT/search_suggest.log" && grep -q 'search: suggestion "Breaking Bad" taken' "$OUT/search_suggest.log" \
  || { echo "no suggestions above the keyboard, or one could not be taken"; grep "search" "$OUT/search_suggest.log"; exit 1; }
# suggestions at once from what this TV knows (the library here), before any addon answers:
# with no addon that searches at all, the title in the library is still suggested
SL_HOME="$(home_with suglib '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"; cp "$ROOT/tests/fixtures/library.json" "$SL_HOME/.local/share/stremio/nativetv/library.json"
run search_suggest_known 300 "search,t:brea,wait" HOME="$SL_HOME" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/tbget/manifest.json
grep -q 'search: 1 suggestion, first "Breaking Bad"' "$OUT/search_suggest_known.log" \
  || { echo "a title in the library was not suggested while typing"; grep "search" "$OUT/search_suggest_known.log"; exit 1; }
# the TV's own keyboard (Settings, on by default on the TV): opened while typing only, its Enter
# searches, and the next OK opens the title (it does not bring the keyboard back)
# Search keeps the phone page open (not only while typing): text sent from a phone searches at once
( for i in $(seq 1 20); do sleep 0.5; curl -s -m 2 -d 'text=breaking%20bad' http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/ 2>/dev/null | grep -q "Sent to the TV" && break; done ) & PHONE_POST=$!
run search_from_phone 360 "search,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_PHONE_HOST=192.168.1.20 HOME="$(home_with sfp '{"setupDone":1}')"
wait $PHONE_POST   # only it: a bare wait waits for the fake addon too
grep -q 'search: "breaking bad" from a phone' "$OUT/search_from_phone.log" || { echo "text sent from the phone did not search"; grep -E "search|phone" "$OUT/search_from_phone.log"; exit 1; }
# Sync with another device: one shows its code; the other, opened with the code's link, syncs both ways.
# The one showing the code has a title with progress, a rating, a list entry and Trakt; the other an MDBList key,
# one more addon and the same list entry (added at another time).
SYH="$(home_with synch '{"setupDone":1,"traktToken":"trk1","ratings":{"":{"tt0111161":{"r":9,"t":1729700000}}},"lists":{"Favourites":[{"id":"tt0111161","t":1}]}}')"
printf '%s' '{"result":[{"_id":"tt0111161","name":"The Shawshank Redemption","type":"movie","removed":false,"temp":false,"_ctime":"2026-10-01T00:00:00.000Z","_mtime":"2026-10-20T00:00:00.000Z","state":{"lastWatched":"2026-10-20T00:00:00.000Z","timeOffset":600000,"duration":8520000,"timeWatched":600000,"timesWatched":0,"flaggedWatched":0,"video_id":"tt0111161"}}]}' > "$SYH/.local/share/stremio/nativetv/library.json"
SYC="$(home_with syncc '{"setupDone":1,"mdblistKey":"md123","lists":{"Favourites":[{"id":"tt0111161","t":2}]}}')"
( run sync_host 5000 "left,down,down,down,down,down,ok,up,ok,t:sync with another,down,down,down,down,left,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_PHONE_HOST=127.0.0.1 SNTV_SYNC_KEY=synctest HOME="$SYH" ) & SYNC_HOST=$!
for i in $(seq 1 150); do grep -q "sync: showing" "$OUT/sync_host.log" 2>/dev/null && break; sleep 0.2; done
run sync_client 700 "wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_NO_PHONE=1 SNTV_EXPECT_SCREEN=sync SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/scope/manifest.json" SNTV_OPEN_LINK="cuelight://sync?u=http%3A%2F%2F127.0.0.1%3A$(grep -o 'page at http://[0-9.]*:[0-9]*' "$OUT/sync_host.log" | grep -o '[0-9]*$')%2F&k=synctest" HOME="$SYC"
wait $SYNC_HOST
grep -q "sync: took in 0 sign-ins, 0 addons, 1 titles, 1 entries" "$OUT/sync_client.log" && grep -q "sync: sent this device's bundle to" "$OUT/sync_client.log" \
  || { echo "the phone did not take in the TV's title and rating"; grep -E "sync|app:" "$OUT/sync_client.log"; exit 1; }
grep -q "sync: took in 1 sign-ins, 1 addons, 0 titles, 0 entries" "$OUT/sync_host.log" || { echo "the TV did not take in the phone's sign-in and addon"; grep sync "$OUT/sync_host.log"; exit 1; }
grep -qE '"mdblistKey":[[:space:]]*"md123"' "$SYH/.local/share/stremio/nativetv/settings.json" && grep -q 'scope/manifest.json' "$SYH/.local/share/stremio/nativetv/settings.json" \
  && grep -q 'tt0111161' "$SYC/.local/share/stremio/nativetv/library.json" || { echo "the synced setup was not saved"; exit 1; }
# Trakt stays on its device (two sharing a refresh token lock each other out); a list entry both have is not doubled
! grep -q traktToken "$SYC/.local/share/stremio/nativetv/settings.json" && [ "$(grep -o '"id":[[:space:]]*"tt0111161"' "$SYH/.local/share/stremio/nativetv/settings.json" | wc -l)" = 1 ] \
  || { echo "Trakt was copied, or a list entry was doubled"; exit 1; }
# no camera (another TV): the other device finds this one on the network and types the number
# under its code; a wrong number is refused, the right one syncs as a scanned code would
PRH="$(home_with pairh '{"setupDone":1,"ratings":{"":{"tt0111161":{"r":9,"t":1729700000}}}}')"
PRC="$(home_with pairc '{"setupDone":1,"mdblistKey":"md456"}')"
( run pair_host 3000 "left,down,down,down,down,down,ok,up,ok,t:sync with another,down,down,down,down,left,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_PHONE_HOST=127.0.0.1 SNTV_PHONE_PORT=$(( ${SNTV_PHONE_PORT:-11471} + 130 )) SNTV_SYNC_PIN=4821 HOME="$PRH" ) & PAIR_HOST=$!
for i in $(seq 1 150); do grep -q "sync: showing" "$OUT/pair_host.log" 2>/dev/null && break; sleep 0.2; done
run pair_client 1400 "left,down,down,down,down,down,ok,up,ok,t:sync with another,down,down,down,down,left,ok,ok,wait,wait,ok,wait,wait,wait,wait,wait,wait,ok,wait,t:1111,wait,wait,wait,t:4821,wait,wait,wait,wait,wait,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_PHONE_HOST=127.0.0.1 SNTV_PHONE_PORT=$(( ${SNTV_PHONE_PORT:-11471} + 150 )) SNTV_SCAN_HOSTS="127.0.0.1:$(grep -o 'page at http://[0-9.]*:[0-9]*' "$OUT/pair_host.log" | grep -o '[0-9]*$')" HOME="$PRC"
wait $PAIR_HOST
grep -q "sync: found a Computer at 127.0.0.1" "$OUT/pair_client.log" && grep -q "sync: pairing failed (403)" "$OUT/pair_client.log" && grep -q "sync: paired with Computer by its number" "$OUT/pair_client.log" \
  && grep -q "sync: sent this device's bundle to Computer" "$OUT/pair_client.log" && grep -q "sync: 127.0.0.1 gave a wrong number" "$OUT/pair_host.log" && grep -q "sync: took in 1 sign-ins" "$OUT/pair_host.log" \
  || { echo "a device without a camera did not find, pair with and sync the other"; grep sync "$OUT/pair_client.log"; grep sync "$OUT/pair_host.log"; exit 1; }
# a link to anywhere but the home network is refused: sign-ins must not go elsewhere
run sync_outside 160 "wait,wait" SNTV_NO_PHONE=1 SNTV_EXPECT_SCREEN=sync SNTV_OPEN_LINK="cuelight://sync?u=http%3A%2F%2F192.168.1.5.example.com%2F&k=abc" HOME="$(home_with syncx '{"setupDone":1}')"
grep -q "sync: a link that is not a device on the home network" "$OUT/sync_outside.log" && ! grep -q "sync: found" "$OUT/sync_outside.log" || { echo "a link outside the home network was followed"; exit 1; }
run search_tv_keyboard 420 "search,ok,t:breaking,wait,wait,ok,wait,ok,wait,wait" HOME="$(home_with tvkb '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tvKeyboard":1}')"
grep -q "search: typing with the TV's keyboard" "$OUT/search_tv_keyboard.log" && grep -q "UI check: screen=detail" "$OUT/search_tv_keyboard.log" \
  || { echo "typing with the TV's keyboard did not search and open the title"; grep -E "search|detail|screen" "$OUT/search_tv_keyboard.log"; exit 1; }
# a search whose results come late still goes into the recent searches (they arrive after Enter,
# take the focus themselves, and the search is left without opening anything)
run search_recent_late 2800 "search,ok,t:shawshank,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,back,back,wait,wait,search,wait,wait,down,wait" HOME="$(home_with recentlate '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tvKeyboard":1}')" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/slowsearch/manifest.json
grep -qE '"recentSearches":[[:space:]]*\["shawshank"\]' "$OUT/home_recentlate/.local/share/stremio/nativetv/settings.json" \
  || { echo "a search with late results was not kept in the recent searches"; grep -o '"recentSearches":[^]]*]' "$OUT/home_recentlate/.local/share/stremio/nativetv/settings.json"; exit 1; }
# Cuelight's own catalog (TMDB, answered inside the app): Home's rows, a film's and a series' pages, search
TK='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}'
CAT="SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg SNTV_DEFAULT_ADDONS=cuelight://catalog/manifest.json,http://127.0.0.1:$PORT/manifest.json"
run catalog_home 400 "wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with cathome "$TK")" $CAT
grep -q "catalog: movie popular page 1 -> 3 titles" "$OUT/catalog_home.log" && grep -q "catalog: series trending page 1 -> 1 titles" "$OUT/catalog_home.log" \
  && grep -q "catalog: poster for tt1003 from TMDB (200)" "$OUT/catalog_home.log" \
  || { echo "the built-in catalog did not fill Home"; grep -E "catalog|addons" "$OUT/catalog_home.log"; exit 1; }
run catalog_movie 700 "wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,ok,wait,wait,wait,wait" HOME="$(home_with catm "$TK")" $CAT
grep -q "catalog: movie tt1001 -> Fake Saga, trailer, 1 cast, 0 episodes" "$OUT/catalog_movie.log" \
  || { echo "the built-in catalog did not give a film's details"; grep -E "catalog|detail" "$OUT/catalog_movie.log"; exit 1; }
run catalog_series 760 "wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,down,ok,wait,wait,wait,wait,down" HOME="$(home_with cats "$TK")" $CAT
grep -q "catalog: series tt0903747 -> Breaking Bad, trailer, 2 cast, 6 episodes" "$OUT/catalog_series.log" \
  || { echo "the built-in catalog did not give a series' seasons"; grep -E "catalog|detail" "$OUT/catalog_series.log"; exit 1; }
run catalog_search 400 "search,t:saga,wait,wait,wait,wait,wait,wait" HOME="$(home_with catq "$TK")" $CAT
grep -q 'search: "Fake Saga" first of 2' "$OUT/catalog_search.log" \
  || { echo "the built-in catalog did not search"; grep -E "catalog|search" "$OUT/catalog_search.log"; exit 1; }
# Stremio sync is optional. Turning it off keeps the account's addons and library on the TV
SIGNED='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey","email":"sam.rivera@example.com"}'
SS="$(home_with p2stop "$SIGNED")"
run sync_stop 420 "left,down,down,down,down,down,ok,right,ok,wait,wait,wait" HOME="$SS" SNTV_API_URL=http://127.0.0.1:$PORT
grep -q "account: stopped syncing; 1 addons and 2 library items stay on this TV" "$OUT/sync_stop.log" \
  && python3 -I -c "import json,sys; s=json.load(open(sys.argv[1])); l=json.load(open(sys.argv[2])); sys.exit(0 if s.get('localAddons')==['http://127.0.0.1:$PORT/manifest.json'] and 'authKey' not in s and len(l['result'])==2 else 1)" \
     "$SS/.local/share/stremio/nativetv/settings.json" "$SS/.local/share/stremio/nativetv/library.json" \
  || { echo "turning Stremio sync off lost the addons or the library"; grep -E "account|library|addons" "$OUT/sync_stop.log"; exit 1; }
# an update keeps a Stremio sign-in: a profile saved by 0.10.58 is still signed in and syncing, and sees what is new once
KEPT="$(home_with kept '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey","email":"sam.rivera@example.com"}')"
run sync_kept 260 "wait,wait,ok,wait" HOME="$KEPT" SNTV_API_URL=http://127.0.0.1:$PORT SNTV_NO_WHATSNEW=
grep -q "whatsnew: updated from an earlier version" "$OUT/sync_kept.log" && grep -q "^.* I library: 2 items$" "$OUT/sync_kept.log" && ! grep -q "stopped syncing" "$OUT/sync_kept.log" \
  && python3 -I -c "import json,sys; s=json.load(open(sys.argv[1])); sys.exit(0 if s.get('authKey')=='testkey' and s.get('email')=='sam.rivera@example.com' else 1)" "$KEPT/.local/share/stremio/nativetv/settings.json" \
  || { echo "after the update the Stremio sign-in was not kept"; grep -E "account|library|addons|whatsnew" "$OUT/sync_kept.log"; exit 1; }
# What's new: once after an update, not again at the next start; About shows what Cuelight is with the same notes
run whatsnew_update 200 "wait" HOME="$(home_with wn '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"seenVersion":"0.10.1"}')" SNTV_NO_WHATSNEW= SNTV_EXPECT_SCREEN=whatsnew
run whatsnew_again 200 "wait" HOME="$OUT/home_wn" SNTV_NO_WHATSNEW= SNTV_EXPECT_SCREEN=home
run about 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,ok,wait,wait" HOME="$(home_with ab '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_EXPECT_SCREEN=whatsnew
# ...the TV keeps its own list (one addon down at this start stays on it), and signing in adds this TV's addons to the account
LOCAL='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"localAddons":["http://127.0.0.1:'$PORT'/noisy/manifest.json","http://127.0.0.1:'$PORT'/manifest.json","http://127.0.0.1:1/gone/manifest.json"]}'
run sync_local 240 "wait,wait" HOME="$(home_with p2local "$LOCAL")" SNTV_DEFAULT_ADDONS=
grep -q "addons: 2 loaded" "$OUT/sync_local.log" || { echo "the addons kept on this TV did not load"; grep addons "$OUT/sync_local.log"; exit 1; }
run sync_start 700 "left,down,down,down,down,down,down,ok,wait,ok,t:sam@example.com,down,t:secret,right,ok,wait,wait,wait,wait,wait,wait" HOME="$(home_with p2merge "$LOCAL")" SNTV_DEFAULT_ADDONS= SNTV_API_URL=http://127.0.0.1:$PORT
grep -q "account: 3 addons from this TV will join the account" "$OUT/sync_start.log" && grep -q "toast: Installed Fake Addon" "$OUT/sync_start.log" \
  || { echo "signing in did not bring this TV's addons into the account"; grep -E "account|addons|toast" "$OUT/sync_start.log"; exit 1; }
run settings 280 "left,down,down,down,down,down,ok,down,down,down,right,down,down,down,down,down"
run diagnostics 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok"
# Diagnostics > YouTube: every way to get a video tried, the verdict shown, and Play opens it
run diagnostics_youtube 1000 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,right,right,ok,wait,wait,wait,wait,wait,wait,wait,wait,down,ok,wait,wait"
grep -q "youtube test: visionOS answered | its files 1080p video and audio open | TV not needed | its files not needed | playlist up to 1080p, plays | file available (360p at most) => 1080p, from the visionOS client" "$OUT/diagnostics_youtube.log" \
  && grep -q "player: candidate 1/4 YouTube test - best" "$OUT/diagnostics_youtube.log" \
  || { echo "the YouTube test did not report or play"; grep -E "youtube|player: candidate" "$OUT/diagnostics_youtube.log"; exit 1; }
# Diagnostics > Speed: startup milestones, key-to-screen time and frame cost per screen
run diagnostics_speed 700 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,wait,right,wait"
grep -q "speed: Startup: window .*first frame" "$OUT/diagnostics_speed.log" && grep -q "speed: Startup: .*first row" "$OUT/diagnostics_speed.log" && grep -q "speed: Key to screen: typical [0-9]* ms" "$OUT/diagnostics_speed.log" && grep -q "speed:   home " "$OUT/diagnostics_speed.log" \
  || { echo "the Speed tab did not measure"; grep speed: "$OUT/diagnostics_speed.log"; exit 1; }
run diagnostics_filter 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,left,left"
run diagnostics_qr 480 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,ok"
grep -q "diagnostics: the log ([0-9]* lines) is at a link while the code shows" "$OUT/diagnostics_qr.log" || { echo "the log QR is not a link to the whole log"; grep diagnostics "$OUT/diagnostics_qr.log"; exit 1; }
run diagnostics_prev_qr 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,blue,ok"
run settings_more 340 "left,down,down,down,down,down,ok,down,down,down,down,down,right,down"
# first-run setup on a fresh profile: first step, then a later step after a few answers
run welcome 90 "" HOME="$(home_with fresh '{}')"
run welcome_subs_when 170 "ok,down,ok" HOME="$(home_with fresh2 '{}')"
# subtitles stay off when the audio already is the subtitle language
run player_native_audio 300 "right,ok,down,down,play,wait" HOME="$(home_with native '{"prefSubs":"eng","setupDone":1}')"
# pick Spanish audio and Portuguese subtitles, then the next episode starts with them
MEM="$(home_with memory '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_pick_tracks 520 "right,ok,down,down,play,wait,down,right,right,right,right,ok,down,down,ok,left,ok,down,down,down,down,ok" HOME="$MEM"
run player_remembered 300 "right,ok,down,down,play,wait" HOME="$MEM"
run home_oled 150 "right,right" HOME="$(home_with oled '{"oled":1,"setupDone":1}')"
run settings_subs_style 340 "left,down,down,down,down,down,ok,down,down,down,right,down,down,down,right"
run player_cue_yellow 300 "right,ok,down,down,play,wait" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with yellow '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"subsStyle":2,"subsSize":2}')"
run player_cue_boxed 300 "right,ok,down,down,play,wait" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with boxed '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"subsStyle":3}')"
P='right,ok,down,down,ok,wait,back'
run pip_detail 320 "$P"
run pip_home 360 "$P,back,back"
run pip_restore 380 "$P,back,back,play"
run pip_claim 340 "$P,ok"
run pip_stop 340 "$P,back,stop"
run player_stats 300 "right,ok,down,down,play,wait,red"
run player_bar 300 "right,ok,down,down,play,wait,down"
run player_bar_style 520 "right,ok,down,down,play,wait,down,right,right,right,ok,down,down,right,right,down,right"
run player_bar_sources 380 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,ok"
run pip_focus 420 "$P,back,back,down,down,down,down,down,down,down"
run pip_focus_close 460 "$P,back,back,down,down,down,down,down,down,down,right,ok"
run player_subs_scrolled 420 "right,ok,down,down,play,wait,down,right,right,right,ok,right,down,down,down" HOME="$(home_with subs3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles in" "$OUT/player_subs_scrolled.log" || { echo "the Subtitles sheet did not open"; grep -E "drawer|subtitles" "$OUT/player_subs_scrolled.log"; exit 1; }
run player_episodes 400 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,right,ok"
run player_paused 320 "right,ok,down,down,play,wait,ok" SNTV_FAKE_START=200
# the content advisory as a video starts: IMDb's parents guide, once the controls step aside; off in Settings, none
run player_advisory 300 "right,ok,down,down,play" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05
grep -q "player: advisory shown: Severe violence, severe drugs, moderate language, mild nudity" "$OUT/player_advisory.log" || { echo "the content advisory did not show"; grep -E "advisory" "$OUT/player_advisory.log"; exit 1; }
run player_advisory_off 300 "right,ok,down,down,play" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05 HOME="$(home_with adv_off '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"advisory":0}')"
grep -qE "advisory: |advisory shown" "$OUT/player_advisory_off.log" && { echo "the content advisory showed while off"; exit 1; }
# paused: the critics' and audience scores and Trakt's numbers in the right-hand corner
run player_corner 330 "right,ok,down,down,play,wait,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05 SNTV_MDBLIST_API=http://127.0.0.1:$PORT/mdblist SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt \
  HOME="$(home_with corner '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"mdblistKey":"k","traktClientId":"cid"}')"
grep -q "player: paused corner: Tomatometer 91%, Popcornmeter 96%, Metacritic 82, Roger Ebert 2.5, Letterboxd 4.5, Trakt 85%, TMDB 84%, MDBList 8.0, Watching now 37, Trakt watchers 23K, Trakt plays 1.4M" "$OUT/player_corner.log" \
  || { echo "the paused corner did not show the scores and Trakt's numbers"; grep -E "corner|mdblist|trakt" "$OUT/player_corner.log"; exit 1; }
# paused: a line from Trakt's comments on this episode, spoilers and one-word ones left out
run player_comments 330 "right,ok,down,down,play,wait,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05 SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt HOME="$(home_with comments '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid"}')"
grep -q "trakt: 2 comments for tt0903747:" "$OUT/player_comments.log" || { echo "the paused panel did not get Trakt's comments"; grep -E "trakt" "$OUT/player_comments.log"; exit 1; }
run player_rest 330 "right,ok,down,down,play,wait,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05
grep -q "player: paused and idle, the controls step aside" "$OUT/player_rest.log" || { echo "the paused player never leaned back"; grep player "$OUT/player_rest.log"; exit 1; }
run player_rest_ok 360 "right,ok,down,down,play,wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,ok" SNTV_FAKE_START=200 SNTV_FRAME_DT=0.05
grep -q "player: OK plays on from the lean-back screen" "$OUT/player_rest_ok.log" || { echo "OK did not play on from the lean-back screen"; grep player "$OUT/player_rest_ok.log"; exit 1; }
run player_episodes_season 420 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,right,ok,right"
run player_speed 400 "right,ok,down,down,play,wait,down,right,right,right,right,ok,right,right"
run player_aspect 360 "right,ok,down,down,play,wait,down,right,right,right,right,right,ok"
run home_showcase 200 "" HOME="$(home_with showcase '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run home_showcase_rows 240 "down,right" HOME="$(home_with showcase2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":1}')"
run rail_open 150 "left"
run explore 320 "left,down,ok,right,down,down,down"
# details beside a card that stays focused: on Home, in a grid, opening left at the last column, off in Settings
run home_peek 300 "right,right"
# instant Home: what Home showed is kept, and the next launch warms it while the fonts load
run home_first_view 1600 "" HOME="$(home_with first_view '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run home_first_view_again 150 "" HOME="$OUT/home_first_view"
grep -q "home: first view kept ([0-9]* pictures)" "$OUT/home_first_view.log" && grep -q "home: warming [0-9]* pictures from last time" "$OUT/home_first_view_again.log" \
  || { echo "Home's first view was not kept and warmed"; grep "home:" "$OUT/home_first_view.log" "$OUT/home_first_view_again.log"; exit 1; }
run home_peek_marquee 300 "right,right" HOME="$(home_with peek_marquee '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"theme":1}')"
run explore_peek_edge 800 "left,down,ok,right,down,down,down,right,right,right,right,right,right"
run home_peek_off 200 "right,right" HOME="$(home_with peek_off '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"cardDetails":0}')"
# every theme has the same functions: Aperture shows card details and its trailers too
run home_peek_aperture 900 "right,right,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with peek_ap '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"theme":7}')"
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
# a nearly full drive: the artwork cache shrinks so the TV (and the next update) keeps room
DH="$(home_with diskfull '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"; mkdir -p "$DH/.local/share/stremio/nativetv/img"
for i in $(seq 1 20); do head -c 1048576 /dev/zero > "$DH/.local/share/stremio/nativetv/img/$(printf '%016x' "$i").img"; done
FREE_MB=$(df -m --output=avail "$DH" | tail -1)
run disk_full 120 "" HOME="$DH" SNTV_DISK_KEEP_FREE_MB=$((FREE_MB + 10)) SNTV_TIDY_AFTER_S=0
grep -q "image: .* MB free on the cache's drive; artwork cache kept to" "$OUT/disk_full.log" && [ "$(ls "$DH/.local/share/stremio/nativetv/img" | wc -l)" -lt 15 ] \
  || { echo "the artwork cache did not make room on a full drive"; grep image "$OUT/disk_full.log"; exit 1; }
# webOS 10: sign-in kept in the developer partition's temp folder moves to internal storage (no account server is reached)
MH="$OUT/home_migrate"; rm -rf "$MH"; mkdir -p "$MH/internal" "$MH/devtemp/org.stremio.nativetv/img"
echo '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"moved-key"}' > "$MH/devtemp/org.stremio.nativetv/settings.json"
run data_move 90 "" HOME="$MH" SNTV_FLASH_ROOTS="$MH/internal,$MH/devtemp" SNTV_API_URL=http://127.0.0.1:9   # its key is made up: no real server may sign it out
grep -q "store: data moved here from $MH/devtemp/org.stremio.nativetv" "$OUT/data_move.log" && grep -q "moved-key" "$MH/internal/.org.stremio.nativetv/settings.json" && [ ! -e "$MH/devtemp/org.stremio.nativetv" ] \
  || { echo "sign-in did not move off the developer partition"; grep -E "data dir|store" "$OUT/data_move.log"; exit 1; }
# Sources panel > Find more sources: every addon is asked while the video plays on, and the new ones join the list
run player_more_sources 600 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,ok,up,up,up,up,up,up,up,up,up,ok,wait,wait,wait,wait" \
  "SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/many/manifest.json" HOME="$(home_with more_src "$NO_PREFLIGHT")"
grep -qE "player: every addon asked from the Sources panel: [1-9][0-9]* more sources" "$OUT/player_more_sources.log" && [ "$(grep -c "streams: #1 score" "$OUT/player_more_sources.log")" -eq 1 ] \
  || { echo "Find more sources did not add sources while playing"; grep -E "player: (every|asking)" "$OUT/player_more_sources.log"; exit 1; }
# the Sources panel during playback (as Nuvio's): Right narrows it to one addon, OK there switches at the same moment
run player_sources_filter 560 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,ok,right,right,down,ok,wait" \
  "SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/many/manifest.json" HOME="$(home_with src_filter "$NO_PREFLIGHT")"
grep -q "player: sources shown for Many Addon" "$OUT/player_sources_filter.log" && grep -q "player: candidate [0-9]*/[0-9]* Many Addon - Many @[0-9]*s" "$OUT/player_sources_filter.log" \
  || { echo "the Sources panel's addon filter did not switch to that addon's source"; grep -E "player: (sources|candidate)" "$OUT/player_sources_filter.log"; exit 1; }
# the Sources sheet during playback: Left/Right walk the addon tabs, Up climbs to Find more
run player_sources_tabs 520 "right,ok,down,down,play,wait,down,right,right,right,right,right,right,ok,right,right,wait" \
  "SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/many/manifest.json" HOME="$(home_with src_tabs "$NO_PREFLIGHT")"
grep -q "player: sources shown for Many Addon" "$OUT/player_sources_tabs.log" && grep -q "player: sources shown for Fake Addon" "$OUT/player_sources_tabs.log" \
  || { echo "the Sources sheet's addon tabs did not switch"; grep -E "player: sources" "$OUT/player_sources_tabs.log"; exit 1; }
run player_lighter 700 "right,ok,down,down,play,wait" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter "$NO_PREFLIGHT")"
grep -q "player: [0-9]* stalls; offering source [0-9] ([0-9]*p)" "$OUT/player_lighter.log" || { echo "repeated buffering offered no lighter source"; grep player "$OUT/player_lighter.log"; exit 1; }
# a connection that drops after the video played: the same source again from the same moment, not the next one
run net_reconnect 1600 "right,ok,down,down,play,wait" HOME="$(home_with netre '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_FAKE_START=600 SNTV_FAKE_NET_ERROR_AFTER=3 SNTV_NET_RETRY_SCALE=0.2
grep -q "player: the connection dropped (Network error (code 300)); the same source again" "$OUT/net_reconnect.log" && grep -q "player: reconnecting to source 1 at 60[0-9]s (try 1 of 3)" "$OUT/net_reconnect.log" && ! grep -q "Trying the next source\|player: rescue" "$OUT/net_reconnect.log" \
  || { echo "a dropped connection did not reconnect to the same source"; grep -E "player: (the conn|reconn|candidate|rescue)|toast" "$OUT/net_reconnect.log"; exit 1; }
# a server that ignores Range sends the whole file: a 64 KB check keeps 64 KB, not 20 MB
run http_norange 30 "" SNTV_HTTP_PROBE=http://127.0.0.1:$PORT/norange
grep -q "http probe: status 200, 65536 bytes, cut at its budget" "$OUT/http_norange.log" || { echo "a range request kept more than it asked for"; grep "http probe" "$OUT/http_norange.log"; exit 1; }
# audio this TV can't decode: another track by itself; no track at all: another source from the same moment
run audio_repair_track 500 "right,ok,down,down,play,wait,wait" HOME="$(home_with audfix '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_FAKE_BAD_AUDIO=0
grep -q "player: audio track 0 can't be played here; switching to spa AC3" "$OUT/audio_repair_track.log" \
  || { echo "an undecodable audio track was not replaced by itself"; grep -E "audio|player" "$OUT/audio_repair_track.log"; exit 1; }
run audio_repair_source 700 "right,ok,down,down,play,wait,wait,wait" "$LIGHTER" HOME="$(home_with audsrc '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0}')" SNTV_FAKE_BAD_AUDIO="*" SNTV_FAKE_START=300 SNTV_NO_RACE=1
grep -q "player: no audio track of source 1 plays here; another source" "$OUT/audio_repair_source.log" && grep -q "player: rescue (This TV can't play this source's audio) -> source 2 at 30[0-9]s" "$OUT/audio_repair_source.log" \
  || { echo "a source with no playable audio was not replaced"; grep -E "audio|player: (rescue|candidate)" "$OUT/audio_repair_source.log"; exit 1; }
# fallbacks: a source that dies mid-film is replaced at the same moment with the same subtitles;
# the next source is checked while playing; a draining buffer moves to a lighter copy first;
# Play races the top sources; when all fail the addons are asked again
FB="SNTV_FAKE_NO_TEXT_TRACKS=1"
run fallback_rescue 4000 "right,ok,down,down,play,wait" "$LIGHTER" "$FB" HOME="$(home_with fb1 "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_DEAD_AFTER=7 SNTV_RESCUE_AFTER=2 SNTV_WARM_AFTER=1
grep -q "player: next source 2 does not answer" "$OUT/fallback_rescue.log" && grep -q "player: rescue (The source stopped sending video) -> source 3 at 6[01][0-9]s" "$OUT/fallback_rescue.log" && grep -q "player: subtitles kept across the switch" "$OUT/fallback_rescue.log" \
  || { echo "a dead source was not replaced at the same moment"; grep -E "player|video" "$OUT/fallback_rescue.log"; exit 1; }
run fallback_drain 4000 "right,ok,down,down,play,wait" "$LIGHTER" "$FB" HOME="$(home_with fb2 "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_DRAIN=1 SNTV_DRAIN_EVERY=1 SNTV_WARM_AFTER=1
grep -q "player: buffer draining .* switching to source [23] ([0-9]*p) before it stops" "$OUT/fallback_drain.log" \
  || { echo "the draining buffer did not move to a working lighter source"; grep -E "player" "$OUT/fallback_drain.log"; exit 1; }
# a seek while the buffer drains: the samples start afresh, so the switch waits for new evidence (not the old samples against the new)
run drain_after_seek 4000 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,right" "$LIGHTER" "$FB" HOME="$(home_with fb2s "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_DRAIN=1 SNTV_DRAIN_EVERY=1 SNTV_WARM_AFTER=1
jmp=$(grep "the drain samples start afresh" "$OUT/drain_after_seek.log" | head -1 | cut -c1-8); sw=$(grep "switching to source" "$OUT/drain_after_seek.log" | head -1 | cut -c1-8)
[ -n "$jmp" ] && [ -n "$sw" ] && [ $(( $(date -u -d "$sw" +%s) - $(date -u -d "$jmp" +%s) )) -ge 3 ] || { echo "a seek did not restart the drain samples (jump $jmp, switch $sw)"; grep -E "player" "$OUT/drain_after_seek.log"; exit 1; }
# a stall the TV reports as a pause (nobody pressed pause): a stall all the same, rescued like one
run stall_as_pause 1800 "right,ok,down,down,play,wait" "$LIGHTER" "$FB" HOME="$(home_with fb2p "$NO_PREFLIGHT")" SNTV_FAKE_START=600 SNTV_FAKE_PAUSE_STALL=2 SNTV_RESCUE_AFTER=3 SNTV_WARM_AFTER=1
grep -q "player: the pipeline paused by itself" "$OUT/stall_as_pause.log" && grep -q "player: rescue (The source stopped sending video) -> source 2" "$OUT/stall_as_pause.log" \
  || { echo "a stall reported as a pause was not rescued"; grep -E "player|video\(null\)" "$OUT/stall_as_pause.log"; exit 1; }
# the best source answers nothing at all: the next one after a few seconds, not the TV's 40
run first_byte 1500 "right,ok,down,down,play,wait" SNTV_FAKE_FETCH=1 HOME="$(home_with fbyte "$NO_PREFLIGHT")" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/slowstart/manifest.json,http://127.0.0.1:$PORT/manifest.json" SNTV_NO_RACE=1 SNTV_NO_BUFFER= SNTV_FIRST_BYTE=2
grep -q "player: source 1 sent nothing in [0-9]*s; the next one" "$OUT/first_byte.log" && grep -q "player: candidate 2/" "$OUT/first_byte.log" \
  || { echo "a silent source was not given up on quickly"; grep -E "player|relay" "$OUT/first_byte.log"; exit 1; }
run fallback_race 2000 "right,ok,down,down,play,wait,wait,wait" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/slowstart/manifest.json,http://127.0.0.1:$PORT/manifest.json" HOME="$(home_with fb3 "$NO_PREFLIGHT")"
grep -q "player: candidate 1/[0-9]* Slow Start Addon - Slow @0s -> .*q=1080" "$OUT/fallback_race.log" \
  || { echo "Play did not start the source that answered"; grep -E "streams|player" "$OUT/fallback_race.log"; exit 1; }
# (P2P off: with the engine built, its copy would play and nothing would be asked again)
run fallback_reask 5000 "right,ok,down,down,play,wait,wait,wait" "$LIGHTER" HOME="$(home_with fb4 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0,"p2pFallback":0}')" SNTV_FAKE_FAIL_LOADS=3 SNTV_REASK_AFTER=1 SNTV_NO_RACE=1
grep -q "player: asked the addons again: 3 sources" "$OUT/fallback_reask.log" && ! grep -q "player: error card" "$OUT/fallback_reask.log" \
  || { echo "the addons were not asked again (or the error card came up while they were)"; grep -E "player" "$OUT/fallback_reask.log"; exit 1; }
# a link that fails to open with a network error is tried once more before it counts as failed
run open_net_retry 1200 "right,ok,down,down,play,wait,wait" "$LIGHTER" HOME="$(home_with netopen '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0,"p2pFallback":0}')" SNTV_FAKE_FAIL_LOADS=1 SNTV_FAKE_FAIL_CODE=300 SNTV_NET_RETRY_SCALE=0.2 SNTV_NO_RACE=1
grep -q "player: the connection dropped (Network Error (code 300)); the same source again" "$OUT/open_net_retry.log" && grep -q "player: reconnecting to source 1" "$OUT/open_net_retry.log" && ! grep -q "player: error card" "$OUT/open_net_retry.log" \
  || { echo "a link that failed to open on a network error was not tried again"; grep -E "player" "$OUT/open_net_retry.log"; exit 1; }
# only when every source, every retry and every fresh link failed does the error card come up
run all_failed_card 3000 "right,ok,down,down,play,wait,wait" "$LIGHTER" HOME="$(home_with allfail '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"preflight":0,"p2pFallback":0}')" SNTV_FAKE_FAIL_LOADS=99 SNTV_REASK_AFTER=0.2 SNTV_NO_RACE=1
grep -q "player: error card" "$OUT/all_failed_card.log" && [ "$(grep -c "player: asked the addons again" "$OUT/all_failed_card.log")" = 2 ] && ! grep -q "tv profile: .* failed here" "$OUT/all_failed_card.log" \
  || { echo "the error card did not wait for the addons to be asked again twice (or links that failed taught the TV profile)"; grep -E "player|tv profile" "$OUT/all_failed_card.log"; exit 1; }
# a source that loads and then waits for data for ever (a torrent nobody seeds) gives way to the next
run fallback_stuck 1200 "right,ok,down,down,play,wait" "$LIGHTER" HOME="$(home_with fb5 "$NO_PREFLIGHT")" SNTV_FAKE_STUCK=1 SNTV_START_TIMEOUT=1
grep -q "player: source 1 did not start in [0-9]*s" "$OUT/fallback_stuck.log" && grep -q "player: candidate 2/[0-9]*" "$OUT/fallback_stuck.log" \
  || { echo "a source stuck at the start was not replaced"; grep -E "player|video" "$OUT/fallback_stuck.log"; exit 1; }
run player_lighter_ok 820 "right,ok,down,down,play,wait$(printf ',wait%.0s' $(seq 1 28)),ok" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter_ok "$NO_PREFLIGHT")"
grep -q "player: switching to the lighter source [0-9]" "$OUT/player_lighter_ok.log" || { echo "OK did not take the lighter source"; grep player "$OUT/player_lighter_ok.log"; exit 1; }
run player_lighter_seek 700 "right,ok,down,down,play,wait,right,right,right,right,right,left,left,left,right,right,right" "$LIGHTER" SNTV_FAKE_STALLS=1 HOME="$(home_with lighter_seek "$NO_PREFLIGHT")"
grep -q "offering source" "$OUT/player_lighter_seek.log" && { echo "seeking counted as buffering trouble"; exit 1; }
run library_stats 280 "left,down,down,down,ok,up,right,right,right,right,ok"
run calendar 300 "left,down,down,down,down,ok"
run settings_rows 380 "left,down,down,down,down,down,ok,down,down,down,down,down,down,right,ok,down,right" HOME="$(home_with rows '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run settings_addons 360 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down"
# an addon with a settings page: OK offers Configure; the TV shows a code to this TV's phone page,
# which links to the addon's page and takes back the install link it ends with, and the addon
# set up that way takes the old one's place (a phone opens the page in its browser instead)
CONF_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/conf-a/manifest.json"
CFH="$(home_with cfg '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
( for i in $(seq 1 40); do sleep 0.5; curl -s -m 2 http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/ 2>/dev/null | grep -q "Open Configurable" && break; done
  curl -s -m 2 http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/ > "$OUT/addon_configure_page.html"
  curl -s -m 2 -d "text=http%3A%2F%2F127.0.0.1%3A$PORT%2Fconf-bb%2Fmanifest.json" http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/ > /dev/null ) & CONF_POST=$!
run addon_configure 900 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down,down,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait," SNTV_PHONE_HOST=127.0.0.1 HOME="$CFH" SNTV_DEFAULT_ADDONS="$CONF_ADDONS"
wait $CONF_POST
grep -q "Open Configurable's settings" "$OUT/addon_configure_page.html" && grep -q 'href="http://127.0.0.1:[0-9]*/conf-a/configure"' "$OUT/addon_configure_page.html" \
  || { echo "the phone page did not offer the addon's settings"; cat "$OUT/addon_configure_page.html"; exit 1; }
grep -q "addons: Configurable set up again, in its place (2)" "$OUT/addon_configure.log" || { echo "the addon set up again did not take its own place"; grep -E "addons:|toast" "$OUT/addon_configure.log"; exit 1; }
python3 -I - "$CFH/.local/share/stremio/nativetv/settings.json" <<'PY' || { echo "the saved addons were not the new link in the old one's place"; exit 1; }
import json, sys
a = json.load(open(sys.argv[1]))["localAddons"]
sys.exit(0 if len(a) == 2 and a[1].endswith("/conf-bb/manifest.json") else 1)
PY
run addon_configure_browser 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down,down,ok,ok,wait,wait" SNTV_FAKE_BROWSER=1 HOME="$(home_with cfgb '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="$CONF_ADDONS"
grep -q "video: a browser opened the page" "$OUT/addon_configure_browser.log" && grep -q "addons: Configurable's settings opened in the browser" "$OUT/addon_configure_browser.log" \
  || { echo "the addon's settings did not open in the browser"; grep -E "addons:|video:" "$OUT/addon_configure_browser.log"; exit 1; }
run addon_options 400 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down,down,ok" HOME="$(home_with cfgo '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="$CONF_ADDONS"
grep -q "UI check: screen=options" "$OUT/addon_options.log" || { echo "OK on an addon did not open its options"; exit 1; }
run addon_setup_screen 500 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,right,down,down,ok,ok,wait,wait,wait" SNTV_PHONE_HOST=192.168.1.20 HOME="$(home_with cfgs '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="$CONF_ADDONS"
run diagnostics_copy_phone 480 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,ok,ok,wait" SNTV_HANDHELD=1 SNTV_WINDOW=1280x600
grep -q "diagnostics: the log ([0-9]* lines) copied" "$OUT/diagnostics_copy_phone.log" || { echo "a phone did not copy the log"; grep -E "diagnostics|UI check" "$OUT/diagnostics_copy_phone.log"; exit 1; }
# the subtitles sheet: languages, then their choices; Look and timing at the end
run player_subs_second 520 "right,ok,down,down,play,wait,down,right,right,right,ok,up,up,ok,right,back,down,down,down,ok" SNTV_FAKE_START=200 HOME="$(home_with subs2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles sheet: Choose for Second subtitles" "$OUT/player_subs_second.log" && grep -q "player: subtitles sheet: a file from Fake Addon in Portugu.*, as the second" "$OUT/player_subs_second.log" \
  || { echo "a second subtitle language was not chosen in the sheet"; grep -E "subtitles" "$OUT/player_subs_second.log"; exit 1; }
run player_seek_thumb 238 "right,ok,down,down,play,wait,right,right,right,right,right,right" HOME="$(home_with thumbs '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
# a scope film framed in 16:9 (bars baked into the picture): its colours glow in the bars, the subtitles sit in the one below
run player_letterbox 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/scope/manifest.json HOME="$(home_with lbox '{"prefSubs":"","setupDone":1,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
grep -q "player: living letterbox, bars of 1[34][0-9] above and below (picture 2\.[34][0-9]:1)" "$OUT/player_letterbox.log" || { echo "a scope film's bars did not take its colours"; grep -E "letterbox|player: (aspect|candidate)" "$OUT/player_letterbox.log"; exit 1; }
grep -q "player: living letterbox changes with the film's cuts" "$OUT/player_letterbox.log" && grep -qE "player: the bars follow the cut at (2\.50|6\.25|11\.00|17\.50|24\.00) s" "$OUT/player_letterbox.log" \
  || { echo "the bars did not follow the film's cuts"; grep -E "letterbox|thumbs" "$OUT/player_letterbox.log"; exit 1; }
run player_letterbox_off 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/scope/manifest.json HOME="$(home_with lboxoff '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"livingLetterbox":0,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
! grep -q "player: living letterbox" "$OUT/player_letterbox_off.log" || { echo "the living letterbox drew while turned off"; exit 1; }
# one colour: the scene's main colour in the bars, the subtitles still in the lower one
run player_letterbox_colour 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/scope/manifest.json HOME="$(home_with lboxcol '{"prefSubs":"","setupDone":1,"livingLetterbox":2,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
grep -q "player: living letterbox, bars of 1[34][0-9] above and below (picture 2\.[34][0-9]:1), One colour" "$OUT/player_letterbox_colour.log" || { echo "the one-colour bars did not draw"; grep letterbox "$OUT/player_letterbox_colour.log"; exit 1; }
# the TV draws a film's own subtitle track itself and an LG hides it while the app paints: the
# letterbox steps aside then, the screen left as with it off
run player_letterbox_tvsubs 700 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/scope/manifest.json HOME="$(home_with lboxsub '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
grep -q "player: the living letterbox steps aside while the TV draws the film's own subtitles" "$OUT/player_letterbox_tvsubs.log" && ! grep -q "player: living letterbox, bars" "$OUT/player_letterbox_tvsubs.log" \
  || { echo "the living letterbox drew over the TV's own subtitles"; grep -E "letterbox|subtitles" "$OUT/player_letterbox_tvsubs.log"; exit 1; }
# the TV ends the app (memory, mostly) and nothing can be written then: the next start says so
# in Problems, with the last memory reading and the last lines before it
env SNTV_KILL_SELF_AFTER=6 SNTV_KEYS="wait" SNTV_SCREENSHOT_FRAMES=100000 HOME="$(home_with killed '{"setupDone":1}')" \
  xvfb-run -a -n "${SMOKE_XVFB_N:-99}" -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" > "$OUT/killed_first.log" 2>&1 || true
run killed_next 160 "wait,wait" HOME="$OUT/home_killed"
grep -q "the previous session ended with no crash report: the system closed it" "$OUT/killed_next.log" && grep -q "previous session, last memory reading: .*memory: app" "$OUT/killed_next.log" \
  && grep -q "previous session, near its end:" "$OUT/killed_next.log" || { echo "an app the TV ended was not reported at the next start"; grep -E "previous|memory" "$OUT/killed_next.log"; exit 1; }
# ended while in the background (swiped off Android's recents, a TV short of memory): no crash, no notice
env SNTV_BACKGROUND_AFTER=3 SNTV_KILL_SELF_AFTER=6 SNTV_KEYS="wait" SNTV_SCREENSHOT_FRAMES=100000 HOME="$(home_with killed_away '{"setupDone":1}')" \
  xvfb-run -a -n "${SMOKE_XVFB_N:-99}" -s "-screen 0 1920x1080x24" "$BUILD/stremio-tv" > "$OUT/killed_away_first.log" 2>&1 || true
run killed_away_next 160 "wait,wait" HOME="$OUT/home_killed_away"
grep -q "the previous session was closed by the system while in the background" "$OUT/killed_away_next.log" && ! grep -q "did not exit cleanly" "$OUT/killed_away_next.log" \
  || { echo "an app ended in the background was taken for a crash"; grep -E "previous|window|background" "$OUT/killed_away_first.log" "$OUT/killed_away_next.log"; exit 1; }
# ... unless the film plays through the buffer: the app reads its own text subtitle track from
# the bytes as they come and draws it (the TV's drawing off), and the letterbox stays
run player_own_subs 700 "right,ok,down,down,play,wait,wait,wait,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/subsfilm/manifest.json HOME="$(home_with ownsubs '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
grep -q "mkvsubs: 2 subtitle tracks in the file: eng S_TEXT/UTF8, por S_TEXT/ASS" "$OUT/player_own_subs.log" && grep -q "player: the film's own subtitles (eng, S_TEXT/UTF8) are drawn by the app" "$OUT/player_own_subs.log" \
  && grep -q "player: the film's own subtitles: 3 lines read so far" "$OUT/player_own_subs.log" && grep -q "player: living letterbox, bars of" "$OUT/player_own_subs.log" \
  || { echo "the film's own subtitles were not drawn by the app with the letterbox on"; grep -E "mkvsubs|own subtitles|letterbox|subtitles" "$OUT/player_own_subs.log"; exit 1; }
run player_own_subs_ass 700 "right,ok,down,down,play,wait,wait,wait,wait,wait" SNTV_NO_BUFFER= SNTV_FAKE_FETCH=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/subsfilm/manifest.json HOME="$(home_with ownass '{"prefSubs":"por","subsAlways":1,"setupDone":1,"thumbServer":"http://127.0.0.1:'"$PORT"'/thumbs"}')"
grep -q "player: the film's own subtitles (por, S_TEXT/ASS) are drawn by the app" "$OUT/player_own_subs_ass.log" && grep -q "player: the film's own subtitles: 2 lines read so far" "$OUT/player_own_subs_ass.log" \
  || { echo "the film's own ASS subtitles were not drawn by the app"; grep -E "mkvsubs|own subtitles|subtitles" "$OUT/player_own_subs_ass.log"; exit 1; }
run settings_letterbox 420 "left,down,down,down,down,down,ok,up,ok,t:living letterbox,down,down,down,down,left,ok"
grep -q "settings: found Living letterbox" "$OUT/settings_letterbox.log" || { echo "Living letterbox is not in Settings"; grep settings "$OUT/settings_letterbox.log"; exit 1; }
# no helper: the TV decodes the keyframe itself (bundled FFmpeg) through its local /frame
if grep -q "^SNTV_FFMPEG_PREFIX:PATH=." "$BUILD/CMakeCache.txt" 2>/dev/null; then
  run player_seek_local 238 "right,ok,down,down,play,wait,right,right,right,right,right,right"
  grep -q "frames: [0-9]* keyframes indexed" "$OUT/player_seek_local.log" && grep -q "frames: [0-9]* s of 127.0.0.1 in" "$OUT/player_seek_local.log" || { echo "seek preview not decoded on the TV"; grep frames "$OUT/player_seek_local.log"; exit 1; }
  # the keyframe index is read while the film plays, before the first seek
  run player_thumbs_warm 400 "right,ok,down,down,play,wait,wait,wait,wait,wait" SNTV_THUMBS_WARM_AFTER=1 HOME="$(home_with thumbs_warm '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
  grep -q "player: seek previews warming up" "$OUT/player_thumbs_warm.log" && grep -q "frames: [0-9]* keyframes indexed" "$OUT/player_thumbs_warm.log" \
    || { echo "seek previews were not warmed up before the first seek"; grep -E "frames|warming" "$OUT/player_thumbs_warm.log"; exit 1; }
  # a long fast seek: one decode at a time, none turned away, and pictures keep coming
  run player_seek_fast 420 "right,ok,down,down,play,wait,right,right,right,right,right,right,right,right,right,right,right,right,right,right,right,right,right,right,wait,wait,wait"
  [ "$(grep -c "frames: [0-9]* s of 127.0.0.1 in" "$OUT/player_seek_fast.log")" -ge 2 ] && ! grep -q "frame?t=.* -> HTTP 503" "$OUT/player_seek_fast.log" \
    || { echo "fast seeking starved the previews"; grep -E "frames|frame\?t" "$OUT/player_seek_fast.log" | head; exit 1; }
else echo "skip: seek previews decoded on the TV (build FFmpeg: tools/ffmpeg.sh)"; fi
run settings_parental 400 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right"
run pin_pad 460 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,right,ok,ok,right,ok"
run pin_locked 300 "left,down,down,down,down,down,ok" HOME="$(home_with pinned '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"parentalPin":"1234","pinSettings":1}')"
run settings_account 300 "left,down,down,down,down,down,ok,right" HOME="$(home_with accts '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"accounts":[{"email":"other@example.com","key":"k"}]}')"
TM='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}'
run detail_tmdb 200 "right,right,ok" HOME="$(home_with tmdb "$TM")" SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
run player_paused_tmdb 320 "right,ok,down,down,play,wait,ok" SNTV_FAKE_START=200 HOME="$(home_with tmdb2 "$TM")" SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
run detail_franchise 420 "right,right,ok,down,down,down,right,ok" HOME="$(home_with tmdb3 "$TM")" SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
grep -q 'detail: franchise -> tt1002' "$OUT/detail_franchise.log" || { echo "the franchise row did not open the next film"; grep -E "detail|tmdb" "$OUT/detail_franchise.log"; exit 1; }
run settings_keys 340 "left,down,down,down,down,down,ok,right"
run settings_rules 380 "left,down,down,down,down,down,ok,up,ok,t:largest,down,down,down,down,left,ok,right" HOME="$(home_with rules '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"srcExclude":"CAM, 3D"}')"
run streams_rules 240 "right,ok,down,down,play" HOME="$(home_with rules2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"srcExclude":"web-dl","preflight":0}')"
# a YouTube stream from an addon (YouTubio, trailer addons) plays here, streamed as Nuvio does
run streams_youtube 440 "right,ok,down,down,yellow,down,down,down,ok,down,down,ok,wait,wait,wait" HOME="$(home_with ytstream '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "streams: YouTube video for tt0903747:1:1 streaming" "$OUT/streams_youtube.log" || { echo "an addon's YouTube stream did not play"; grep -E "streams|youtube" "$OUT/streams_youtube.log"; exit 1; }
run settings_tvcap 380 "left,down,down,down,down,down,ok,up,ok,t:this tv can,down,down,down,down,left,ok,ok" HOME="$(home_with tvcap '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tvCaps":{"dts":[0,2],"hevc":[5,0]}}')"
run settings_backup 420 "left,down,down,down,down,down,ok,up,ok,t:back up,down,down,down,down,left,ok,ok"
grep -q "settings: found Back up or restore settings" "$OUT/settings_backup.log" || { echo "Back up or restore is not in Settings"; grep settings "$OUT/settings_backup.log"; exit 1; }
# the backup carries the Stremio sign-in under the Cuelight tag; a restore follows it and keeps this TV's other accounts
BK="$OUT/backup_saved.json"; rm -f "$BK"
( for i in $(seq 1 30); do sleep 0.5; curl -s -m 2 -o "$BK" http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/backup.json 2>/dev/null && grep -q '"_backup"' "$BK" && break; done
  python3 -c 'import json,sys,urllib.parse; d=json.load(open(sys.argv[1])); d["authKey"]="key-two"; d["email"]="two@example.com"; d["themeName"]="restored"; print("text="+urllib.parse.quote(json.dumps(d)))' "$BK" > "$BK.post"
  curl -s -m 2 --data-binary @"$BK.post" http://127.0.0.1:${SNTV_PHONE_PORT:-11471}/restore | grep -q "Sent to the TV" ) & BK_POST=$!
run settings_backup_round 760 "wait,wait,wait,left,down,down,down,down,down,ok,up,ok,t:back up,down,down,down,down,left,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_PHONE_HOST=192.168.1.20 SNTV_API_URL="http://127.0.0.1:$PORT" HOME="$(home_with bkr '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey","email":"one@example.com","accounts":[{"email":"one@example.com","authKey":"testkey"}]}')"
wait $BK_POST
grep -q '"_backup":"cuelight"' "$BK" && grep -q '"authKey":"testkey"' "$BK" || { echo "the backup did not carry the Cuelight tag and the Stremio sign-in"; head -c 300 "$BK"; exit 1; }
grep -q "settings: restored" "$OUT/settings_backup_round.log" || { echo "the backup was not restored"; grep -E "settings|phone" "$OUT/settings_backup_round.log"; exit 1; }
BKS="$OUT/home_bkr/.local/share/stremio/nativetv/settings.json"
grep -q '"authKey":[[:space:]]*"key-two"' "$BKS" && grep -q 'one@example.com' "$BKS" && grep -q 'two@example.com' "$BKS" && grep -q '"signinsShared":[[:space:]]*1' "$BKS" \
  || { echo "the restored sign-in was not followed, or this TV's account was lost"; grep -oE '"(authKey|email|signinsShared)":[^,]*' "$BKS"; exit 1; }
TK='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid","traktClientSecret":"sec"}'
run trakt_connect 1200 "left,down,down,down,down,down,ok,up,ok,t:trakt,down,down,down,down,left,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with trakt "$TK")" SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt
grep -q "tracking: Trakt connected" "$OUT/trakt_connect.log" && grep -q "tracking: Trakt watchlist has 2 titles" "$OUT/trakt_connect.log" || { echo "Trakt did not connect"; grep tracking "$OUT/trakt_connect.log"; exit 1; }
TK2='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid","traktClientSecret":"sec","traktToken":"tok1","simklClientId":"scid","simklToken":"stok"}'
# (an episode played to its end, autoplay off: a jump to 92% and Back is a peek and scrobbles no "watched")
run trakt_scrobble 2400 "wait,wait,right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,back,back,back" HOME="$(home_with trakt2 "${TK2%\}},\"autoplayNext\":0}")" SNTV_FAKE_DURATION=130 SNTV_FAKE_START=125 SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt SNTV_SIMKL_API=http://127.0.0.1:$PORT/simkl
grep -q "tracking: Trakt start tt0903747" "$OUT/trakt_scrobble.log" && grep -q "tracking: Trakt stop tt0903747" "$OUT/trakt_scrobble.log" && grep -q "tracking: SimKL watched tt0903747" "$OUT/trakt_scrobble.log" \
  || { echo "playback was not scrobbled"; grep tracking "$OUT/trakt_scrobble.log"; exit 1; }
# renewing a token that lasts a day: once, not again every second; a renewal that fails waits
NOWS=$(date +%s)
run trakt_renew 300 "" HOME="$(home_with trenew "{\"prefSubs\":\"eng\",\"subsAlways\":1,\"setupDone\":1,\"traktClientId\":\"cid\",\"traktToken\":\"tok1\",\"traktRefresh\":\"ref1\",\"traktExpires\":$((NOWS + 3600))}")" SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt
[ "$(grep -c "tracking: Trakt token renewed" "$OUT/trakt_renew.log")" = 1 ] || { echo "the Trakt token was not renewed exactly once"; grep tracking "$OUT/trakt_renew.log" | head; exit 1; }
run trakt_renew_down 300 "" HOME="$(home_with trenew2 "{\"prefSubs\":\"eng\",\"subsAlways\":1,\"setupDone\":1,\"traktClientId\":\"cid\",\"traktToken\":\"tok1\",\"traktRefresh\":\"refdown\",\"traktExpires\":$((NOWS + 3600))}")" SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt
[ "$(grep -c "tracking: Trakt token renewal failed" "$OUT/trakt_renew_down.log")" = 1 ] || { echo "a failed Trakt renewal was not left to wait"; grep tracking "$OUT/trakt_renew_down.log" | head; exit 1; }
# paused on a phone: Trakt's resume points reach Continue Watching when newer than the TV's
run trakt_resume 600 "wait,wait,wait" HOME="$(home_with trakt_resume '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"traktClientId":"cid","traktToken":"tokresume"}')" SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt
grep -q "tracking: Trakt has 2 resume points, 2 newer than this TV's" "$OUT/trakt_resume.log" || { echo "Trakt's resume points were not taken"; grep -E "tracking|library" "$OUT/trakt_resume.log"; exit 1; }
run simkl_connect 2500 "left,down,down,down,down,down,ok,up,ok,t:simkl,down,down,down,down,left,ok,ok" HOME="$(home_with simkl '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"simklClientId":"scid"}')" SNTV_SIMKL_API=http://127.0.0.1:$PORT/simkl
grep -q "tracking: SimKL sign-in code SV2CODE" "$OUT/simkl_connect.log" && grep -q "tracking: SimKL connected" "$OUT/simkl_connect.log" && grep -qE '"simklRefresh":[[:space:]]*"sref"' "$OUT/home_simkl/.local/share/stremio/nativetv/settings.json" \
  || { echo "SimKL did not connect with OAuth 2.0"; grep tracking "$OUT/simkl_connect.log"; exit 1; }
# an app registered before AUTH V2: the PIN
run simkl_connect_v1 2500 "left,down,down,down,down,down,ok,up,ok,t:simkl,down,down,down,down,left,ok,ok" HOME="$(home_with simkl_v1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"simklClientId":"v1app"}')" SNTV_SIMKL_API=http://127.0.0.1:$PORT/simkl
grep -q "tracking: SimKL sign-in code SIM12" "$OUT/simkl_connect_v1.log" && grep -q "tracking: SimKL connected" "$OUT/simkl_connect_v1.log" || { echo "an old SimKL app did not fall back to the PIN"; grep tracking "$OUT/simkl_connect_v1.log"; exit 1; }
# a week on: the token is renewed
run simkl_refresh 150 "" HOME="$(home_with simkl_ref '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"simklClientId":"scid","simklToken":"stok2","simklRefresh":"sref","simklExpires":1}')" SNTV_SIMKL_API=http://127.0.0.1:$PORT/simkl
grep -q "tracking: SimKL token renewed" "$OUT/simkl_refresh.log" || { echo "the SimKL token was not renewed"; grep tracking "$OUT/simkl_refresh.log"; exit 1; }
run detail_mdblist 260 "wait,ok,down" HOME="$(home_with mdb '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"mdblistKey":"k"}')" SNTV_MDBLIST_API=http://127.0.0.1:$PORT/mdblist
run transition_push 32 "right,ok" SNTV_NO_TRANSITIONS=
# OK on a card: the title page grows out of it (fixed frame time so the capture lands mid-way)
run transition_zoom 34 "right,ok" SNTV_NO_TRANSITIONS= SNTV_FRAME_DT=0.016
run home_mem_tight 600 "right,right" SNTV_FAKE_MEM=3000000,90000 SNTV_MEM_SAMPLE_S=0.5   # read twice a second: the step down comes whatever the frame rate
grep -q "memory saver on (memory got tight)" "$OUT/home_mem_tight.log" && grep -q "memory: tight (87 MB free of 2929): the saver" "$OUT/home_mem_tight.log" || { echo "a tight TV did not step down"; grep -E "memory" "$OUT/home_mem_tight.log"; exit 1; }
run home_mem_low 600 "right,right" SNTV_FAKE_MEM=3000000,400000 SNTV_MEM_SAMPLE_S=0.5
grep -q "memory: getting low (390 MB free of 2929): poster prefetch" "$OUT/home_mem_low.log" && ! grep -q "memory saver on" "$OUT/home_mem_low.log" || { echo "a TV getting low did not pause the extras"; grep -E "memory" "$OUT/home_mem_low.log"; exit 1; }
run home_lowmem 150 "right,right" HOME="$(home_with lowmem '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"memoryMode":1}')"
BR='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":723,"breakLead":5,"breaksEverywhere":1}'
run break_banner 150 "right" HOME="$(home_with brk "$BR")" SNTV_NOW=12:01
run break_screen 320 "left,down,down,down,down,down,ok,down,down,right,ok"
run break_player 300 "right,ok,down,down,play,wait" HOME="$(home_with brk2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720}')" SNTV_NOW=12:00
# a recording over a paused film; when the break ends the film is back where it was, still paused
run break_paused 1300 "right,ok,down,down,play,wait,ok,wait" HOME="$(home_with brkp '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakLen":-1,"breakVideo":0,"breakAudio":"http://127.0.0.1:8765/video/sample_h264_aac.mp4"}')" SNTV_NOW=12:00+8 SNTV_FAKE_START=200 SNTV_BREAK_LEN_S=4
grep -q "player: candidate 1/[0-9]* .* @20[0-9]s" "$OUT/break_paused.log" && grep -q "player: paused again at 20[0-9]s" "$OUT/break_paused.log" \
  || { echo "the paused film did not come back paused where it was"; grep -E "player|break" "$OUT/break_paused.log"; exit 1; }
# the screensaver waits while a break counts down (it froze the break, so the film never came back)
run break_no_saver 900 "right,ok,down,down,play,wait" HOME="$(home_with brkns '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720}')" SNTV_NOW=12:00+3 SNTV_AMBIENT_SECONDS=1
grep -q "UI check: screen=break" "$OUT/break_no_saver.log" || { echo "the screensaver covered a break"; grep "UI check" "$OUT/break_no_saver.log"; exit 1; }
# nor a playing video; only one the viewer paused (a stall webOS reports as a pause is not one)
run player_no_saver 700 "right,ok,down,down,play,wait,wait" HOME="$(home_with nosaver '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_AMBIENT_SECONDS=1
grep -q "UI check: screen=player" "$OUT/player_no_saver.log" || { echo "the screensaver covered a playing video"; grep "UI check" "$OUT/player_no_saver.log"; exit 1; }
run player_paused_saver 700 "right,ok,down,down,play,wait,ok,wait" SNTV_FAKE_START=200 HOME="$(home_with pausesaver '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_AMBIENT_SECONDS=1
grep -q "UI check: screen=ambient" "$OUT/player_paused_saver.log" || { echo "the screensaver did not come over a video the viewer paused"; grep "UI check" "$OUT/player_paused_saver.log"; exit 1; }
run break_sound_file 520 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,down,down,down" HOME="$(home_with brk4 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakSound":1,"breakAudio":"sntv-sound://start.mp3"}')"
# no cached copy: the TV plays the torrent itself, then goes back to TorBox once it has the title
if [ -n "${FAKE_TORRENT_HASH:-}" ]; then
run p2p_playing 420 "right,ok,down,down,play,wait" HOME="$(home_with p2p1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/p2ponly/manifest.json" SNTV_P2P_RECHECK=1
grep -q "still no cached copy; staying on P2P" "$OUT/p2p_playing.log" || { echo "P2P did not keep playing without a cached copy"; grep -E "player|p2p" "$OUT/p2p_playing.log"; exit 1; }
run settings_p2p 400 "left,down,down,down,down,down,ok,down,down,down,down,right,down,down,down,down,down,down" HOME="$(home_with p2pset '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
# P2P plays well when TorBox has it again: it stays, and goes back to the cached copy at the next seek
run p2p_switch 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,right" HOME="$(home_with p2p '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/p2p/manifest.json" SNTV_P2P_RECHECK=2
grep -q "candidate 1/.* -> http://127.0.0.1:${SNTV_ENGINE_PORT:-11490}/$FAKE_TORRENT_HASH/0" "$OUT/p2p_switch.log" && grep -q "player: file is H.264 180p, 0 chapters" "$OUT/p2p_switch.log" \
  && grep -q "staying on P2P while it plays well" "$OUT/p2p_switch.log" && grep -q "TorBox has it again, leaving P2P" "$OUT/p2p_switch.log" || { echo "P2P fallback, the wait or the switch back failed"; grep -E "player|p2p|torrent|probe" "$OUT/p2p_switch.log"; exit 1; }
# TorBox said it had the torrent, then went down: the same torrent from peers plays (it only
# came as TorBox's link), and the player stays on it until TorBox opens it again
run torbox_down_p2p 1100 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with tbdp2p '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey"}')" SNTV_TORBOX_API=http://127.0.0.1:$PORT/torboxdown SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/p2ponly/manifest.json" SNTV_P2P_RECHECK=2
grep -q "streams: P2P of the same torrent rides along, for when TorBox is down" "$OUT/torbox_down_p2p.log" \
  && grep -q "video(null): load http://127.0.0.1:${SNTV_ENGINE_PORT:-11490}/$FAKE_TORRENT_HASH/0" "$OUT/torbox_down_p2p.log" \
  && grep -q "toast: TorBox is not answering: playing P2P on this TV" "$OUT/torbox_down_p2p.log" \
  && grep -q "player: TorBox still not answering (.*); staying on P2P" "$OUT/torbox_down_p2p.log" && ! grep -q "TorBox has it again, leaving P2P" "$OUT/torbox_down_p2p.log" \
  || { echo "TorBox being down did not end in P2P of the same torrent, or the player left P2P for it"; grep -E "torbox|player: (candidate|TorBox|source)|video\(null\): load|toast" "$OUT/torbox_down_p2p.log"; exit 1; }
fi
# progress reaches the account as the app closes mid-film, dated the way stremio-core reads it
# an account whose addons an old version saved with partial manifests: refetched and saved back whole
run addons_repair 300 "wait" HOME="$(home_with repair '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"brokenkey"}')" SNTV_API_URL="http://127.0.0.1:$PORT"
grep -q "addons: 2 of the account's addons were saved without their manifest" "$OUT/addons_repair.log" && grep -q "addons: 1 addon had no answer last time; asking again" "$OUT/addons_repair.log" \
  && grep -q "addons: no manifest from http://127.0.0.1:1/gone/manifest.json (0); keeping it with empty lists" "$OUT/addons_repair.log" \
  && grep -q "addons: saving the repaired collection (3 addons, 2 fixed, 1 still not answering)" "$OUT/addons_repair.log" && grep -q "addons: the account's collection is repaired" "$OUT/addons_repair.log" \
  && grep -q "addons: 3 loaded" "$OUT/addons_repair.log" && ! grep -q "opensubtitles.strem.io" "$OUT/addons_repair.log" \
  || { echo "the account's addons were not repaired"; grep -E "addons|home" "$OUT/addons_repair.log"; exit 1; }
# addons left blank by an earlier repair: the collection is used at once, and the one that answers now is saved back
run addons_reask 300 "wait" HOME="$(home_with reask '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"blankkey"}')" SNTV_API_URL="http://127.0.0.1:$PORT"
grep -q "addons: 2 addons had no answer last time; asking again" "$OUT/addons_reask.log" && grep -q "addons: 3 loaded" "$OUT/addons_reask.log" \
  && grep -q "addons: 1 addon answered again; saving to the account" "$OUT/addons_reask.log" && ! grep -q "addons: not saved to the account" "$OUT/addons_reask.log" \
  || { echo "blank addons were not asked again"; grep -E "addons" "$OUT/addons_reask.log"; exit 1; }
run progress_sync 400 "right,ok,down,down,play,wait" HOME="$(home_with sync '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"authKey":"testkey"}')" SNTV_API_URL="http://127.0.0.1:$PORT"
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
run break_video 300 "right,ok,down,down,play,wait" HOME="$(home_with brk5 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":1,"breakAudio":"https://www.youtube.com/watch?v=dQw4w9WgXcQ","thumbServer":"http://127.0.0.1:'"$PORT"'"}')" SNTV_NOW=12:00
# a break's YouTube video not saved yet, refused: YouTube's own player where the device has one
run break_video_embed 300 "right,ok,down,down,play,wait" SNTV_FAKE_EMBED=1 SNTV_YT_BASE=http://127.0.0.1:9 HOME="$(home_with brke '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":1,"breakAudio":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')" SNTV_NOW=12:00
grep -q "break: playing youtube-embed:dQw4w9WgXcQ (video)" "$OUT/break_video_embed.log" || { echo "a break's refused YouTube video did not play in YouTube's own player"; grep -E "break|youtube|video" "$OUT/break_video_embed.log"; exit 1; }
# a YouTube link typed into Settings is saved by the TV itself (QuickJS runs the solver), then plays at the break with no helper
run break_yt_save 900 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,down,down,down,ok,t:youtu.be/dQw4w9WgXcQ,down,down,down,down,left,ok" HOME="$(home_with brkyt '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0}')" SNTV_NOW=03:00
grep -q "itag 140 from the VISIONOS client" "$OUT/break_yt_save.log" && grep -q "youtube: dQw4w9WgXcQ ready as yt-dQw4w9WgXcQ-audio.m4a" "$OUT/break_yt_save.log" || { echo "youtube save failed"; grep youtube "$OUT/break_yt_save.log"; exit 1; }
run break_yt_play 300 "right,ok,down,down,play,wait" HOME="$OUT/home_brkyt" SNTV_NOW=12:00
grep -q "break: playing http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ-audio.m4a" "$OUT/break_yt_play.log" || { echo "saved YouTube recording not played"; grep break "$OUT/break_yt_play.log"; exit 1; }
# YouTube's bot check on the visionOS client: the TV client, with the solver, takes over
: > "$OUT/fake_track.log"   # only this run's asks count below
run break_yt_tv 400 "right,ok,down,down,play,wait" HOME="$(home_with brkyt2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0,"breakAudio":"https://youtu.be/tvOnlyVid01"}')" SNTV_NOW=12:00
grep -q "itag 140 from the TVHTML5 client" "$OUT/break_yt_tv.log" && grep -q "ready as yt-tvOnlyVid01-audio.m4a" "$OUT/break_yt_tv.log" || { echo "TV client fallback failed"; grep youtube "$OUT/break_yt_tv.log"; exit 1; }
[ "$(grep -c "yt refused ANDROID tvOnlyVid01\|yt refused IOS tvOnlyVid01" "$OUT/fake_track.log")" = 0 ] || { echo "after the bot check the other token-free clients were still asked"; grep "yt refused" "$OUT/fake_track.log"; exit 1; }
# the sign-in wall on the TV app too: once more with a fresh visitor id, which passes
run break_yt_retry 500 "right,ok,down,down,play,wait" HOME="$(home_with brkyt3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0,"breakAudio":"https://youtu.be/tvbotVid001"}')" SNTV_NOW=12:00
grep -q "asking again with a fresh visitor id" "$OUT/break_yt_retry.log" && grep -q "ready as yt-tvbotVid001-audio.m4a" "$OUT/break_yt_retry.log" || { echo "the retry with a fresh visitor id failed"; grep youtube "$OUT/break_yt_retry.log"; exit 1; }
# the phone apps' file is refused past its first bytes (a token wall): the next client's file is taken
run break_yt_savepot 500 "right,ok,down,down,play,wait" HOME="$(home_with brkyt4 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakVideo":0,"breakAudio":"https://youtu.be/savepotVid1"}')" SNTV_NOW=12:00
grep -q "the VISIONOS client's file was refused" "$OUT/break_yt_savepot.log" && grep -q "ready as yt-savepotVid1-audio.m4a" "$OUT/break_yt_savepot.log" || { echo "a refused file did not move to the next client"; grep youtube "$OUT/break_yt_savepot.log"; exit 1; }
# hold OK: a short press still opens the title; the hint fills, then the release opens the sheet
HOLD="wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait"
run options_sheet 400 "right,right,hold,$HOLD,release"
run hold_hint 200 "right,ok,down,down,right,hold,wait,wait"
run episode_options 400 "right,ok,down,down,right,hold,$HOLD,release"
# timing for a subtitle track inside the file (the TV draws it; the player sends the offset)
run player_subs_timing_embedded 460 "right,ok,down,down,play,wait,down,right,right,right,ok,up,up,ok,down,right,right,right" SNTV_FAKE_START=300 HOME="$(home_with substime '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles sheet: Timing +0.3 s" "$OUT/player_subs_timing_embedded.log" || { echo "the video's subtitles were not retimed"; grep -E "subtitles" "$OUT/player_subs_timing_embedded.log"; exit 1; }
# Look and timing steps aside to a narrow panel so the subtitles show while they change; Back brings the drawer back
# a credit over the dialogue and a line marked {\an8}: the mark is not shown, that line goes to the top, the credit and the name stack
run player_subs_signs 360 "right,ok,down,down,play,wait,wait" SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/signs/manifest.json HOME="$(home_with signs '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "subs: loaded 3 cues" "$OUT/player_subs_signs.log" || { echo "the subtitles with a sign did not load"; grep -iE "subs" "$OUT/player_subs_signs.log"; exit 1; }
run player_subs_panel 460 "right,ok,down,down,play,wait,down,right,right,right,ok,up,up,ok,down,down,right" HOME="$(home_with subspanel '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles Look and timing panel, the drawer aside" "$OUT/player_subs_panel.log" && grep -q "player: subtitles sheet: Size Large" "$OUT/player_subs_panel.log" \
  || { echo "the Look and timing panel did not open, or Size did not change in it"; grep -E "subtitles" "$OUT/player_subs_panel.log"; exit 1; }
run episode_marked 470 "right,ok,down,down,right,hold,$HOLD,release,wait,wait,ok" HOME="$(home_with ep_marked '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run episode_upto 520 "right,ok,down,down,right,right,right,hold,$HOLD,release,wait,wait,down,ok" HOME="$(home_with ep_upto '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run options_yellow 200 "right,right,right,yellow"
run search_recent 200 "search,down,down" HOME="$(home_with recent '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"recentSearches":["breaking bad","midnight harbor","something about prison and hope"]}')"
run offline_update 220 "right" SNTV_FAKE_OFFLINE=1 SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json
# a new release: at a quiet moment the app asks; Update now hands it to the Homebrew Channel
run update_prompt 600 "" HOME="$(home_with upd1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json
W=$(printf 'wait,%.0s' $(seq 1 40))
run update_install 1600 "${W}down,ok" HOME="$(home_with upd2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json SNTV_FAKE_INSTALL=1
grep -q "update: installing" "$OUT/update_prompt.log" && { echo "it installed without being asked"; exit 1; }
[ "$(grep -c "update: 9.9.9 available" "$OUT/update_install.log")" = 1 ] && grep -q "update: the viewer said update now" "$OUT/update_install.log" && grep -q "update: installing 9.9.9 from http://127.0.0.1:$PORT/org.stremio.nativetv_9.9.9_arm.ipk" "$OUT/update_install.log" && grep -q "update: 9.9.9 installed" "$OUT/update_install.log" || { echo "the update did not install"; grep update "$OUT/update_install.log"; exit 1; }
# Later is the prompt's first choice: an OK that lands as it opens never installs
run update_ok_is_later 1600 "${W}ok" HOME="$(home_with upd4 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json SNTV_FAKE_INSTALL=1
grep -q "update: the viewer said later" "$OUT/update_ok_is_later.log" && ! grep -q "update: installing" "$OUT/update_ok_is_later.log" \
  || { echo "OK on the update prompt installed (Later must come first)"; grep -E "update" "$OUT/update_ok_is_later.log"; exit 1; }
ASR='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":0,"breakMethod":1,"breakAsr":1,"breakLat":"43.676","breakLon":"-79.485","breakCity":"Toronto","breakLead":5,"breaksEverywhere":1}'
# the Android app's notice (version, apkUrl, sha256) reads the same
run update_android_notice 600 "" HOME="$(home_with upda '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/android.json
grep -q "update: 9.9.9 available" "$OUT/update_android_notice.log" || { echo "the Android update notice was not read"; grep update "$OUT/update_android_notice.log"; exit 1; }
run island_update 900 "$(printf 'wait,%.0s' $(seq 1 35))back" HOME="$(home_with islu '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json
run update_refused 1700 "${W}down,ok,wait,wait,wait,ok" HOME="$(home_with upd3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json
grep -q "toast: Open the Homebrew Channel from the Home screen to update" "$OUT/update_refused.log" || { echo "a refused update did not point to the Homebrew Channel"; grep -E "update|toast" "$OUT/update_refused.log"; exit 1; }
# refused once: the next launch tells how (once per version) and doesn't ask the installer unasked
run update_refused_again 1700 "${W}down,ok,wait,wait" HOME="$OUT/home_upd3" SNTV_UPDATE_URL=http://127.0.0.1:$PORT/repo.json
grep -q "update: the viewer asked how" "$OUT/update_refused_again.log" && ! grep -q "update: installing" "$OUT/update_refused_again.log" \
  || { echo "a TV that refused the installer was asked again"; grep -E "update|toast" "$OUT/update_refused_again.log"; exit 1; }
run break_asr_change 520 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,right,back,back" HOME="$(home_with asr "$ASR")" TZ=America/Toronto SNTV_NOW=15:58
run break_media_length 230 "right,ok,down,down,play,wait" HOME="$(home_with bml '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakCustom1":720,"breakSound":1,"breakLen":-1,"breakVideo":0,"breakAudio":"http://127.0.0.1:'"$PORT"'/video/sample_h264_aac.mp4"}')" SNTV_NOW=12:00 SNTV_FAKE_DURATION=6
run break_settings 480 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,ok" SNTV_IPAPI=http://127.0.0.1:$PORT/ipapi HOME="$(home_with brk3 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1}')"
# the found country picks its own authority's prayer times (Saudi Arabia: Umm al-Qura)
grep -q "breaks: prayer times by Umm al-Qura" "$OUT/break_settings.log" || { echo "the location did not pick the local prayer time method"; grep breaks "$OUT/break_settings.log"; exit 1; }
run settings_phone 400 "left,down,down,down,down,down,ok,right,down,down,down,down,down,down,ok" SNTV_PHONE_HOST=192.168.1.20
run player_back_stops 220 "$P" HOME="$(home_with nopip '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run player_autosync 520 "right,ok,down,down,play,wait,down,right,right,right,ok,up,up,ok,down,down,down,down,down,ok,wait" SNTV_FAKE_START=200 HOME="$(home_with subsync '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles sheet: Auto-sync Off" "$OUT/player_autosync.log" || { echo "auto-sync was not switched in the sheet"; grep -E "subtitles" "$OUT/player_autosync.log"; exit 1; }
# two English files, no file-matched one: the one that runs to the end of the video wins
run player_subs_bestfit 420 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,down,down,ok" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/bestfit/manifest.json HOME="$(home_with bestfit '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')" SNTV_FAKE_START=200 SNTV_FAKE_NO_TEXT_TRACKS=1
grep -q "subs: switching to file 1, it fits better" "$OUT/player_subs_bestfit.log" || { echo "the better-fitting subtitle was not picked"; grep subs "$OUT/player_subs_bestfit.log"; exit 1; }
# OpenSubtitles.com built in: the file made for this video (its hash) is the one downloaded
run player_subs_opensubs 420 "right,ok,down,down,play,wait,wait,wait,wait,wait,down,down,ok" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/bestfit/manifest.json HOME="$(home_with os '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"osApiKey":"oskey"}')" SNTV_FAKE_START=200 SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_OPENSUBS_API=http://127.0.0.1:$PORT/os
grep -q "subs: 2 from OpenSubtitles.com, one made for this file" "$OUT/player_subs_opensubs.log" && grep -q "opensubs: file 222, 4 downloads left today" "$OUT/player_subs_opensubs.log" \
  && ! grep -q "opensubs: file 111" "$OUT/player_subs_opensubs.log" || { echo "OpenSubtitles.com did not pick the file made for this video"; grep -E "subs|opensubs" "$OUT/player_subs_opensubs.log"; exit 1; }
AI='{"prefSubs":"eng","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}'
# subtitle downloads that come late (the /latesubs addon's first English file takes 12 s): a
# file chosen meanwhile, or Off, is not replaced by it; a choice made in the sheet stays across
# a source switch; best-fit files fetched for a source that failed are not scored for the next
# a subtitle file that does not load: the next one in the same language, as a source that fails gives way to the next
run subs_fallback 900 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/badsubs/manifest.json SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with subfb '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "subs: a eng file from .* did not load" "$OUT/subs_fallback.log" && grep -q "subs: another file instead" "$OUT/subs_fallback.log" && grep -q "subs: loaded 6 cues" "$OUT/subs_fallback.log" \
  || { echo "a subtitle file that failed did not give way to the next"; grep subs "$OUT/subs_fallback.log"; exit 1; }
LATE="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/latesubs/manifest.json"
LATE2="SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/lighter/manifest.json,http://127.0.0.1:$PORT/latesubs/manifest.json"
run subs_late_pick 2000 "right,ok,down,down,play,wait,down,right,right,right,ok,right,down,ok" "$LATE" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with latepick '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "subs: loaded 5 cues" "$OUT/subs_late_pick.log" && grep -q "subs: a subtitle download that was replaced is dropped" "$OUT/subs_late_pick.log" && ! grep -q "subs: loaded 7 cues" "$OUT/subs_late_pick.log" \
  || { echo "a late subtitle file replaced the one chosen meanwhile"; grep "subs" "$OUT/subs_late_pick.log"; exit 1; }
run subs_late_off 2000 "right,ok,down,down,play,wait,down,right,right,right,ok,up,ok" "$LATE" SNTV_FAKE_NO_TEXT_TRACKS=1 HOME="$(home_with lateoff '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "player: subtitles sheet: off" "$OUT/subs_late_off.log" && grep -q "subs: a subtitle download that was replaced is dropped" "$OUT/subs_late_off.log" && ! grep -q "subs: loaded" "$OUT/subs_late_off.log" \
  || { echo "a late subtitle file turned subtitles back on"; grep "subs" "$OUT/subs_late_off.log"; exit 1; }
run subs_late_switch 2500 "right,ok,down,down,play,wait,down,right,right,right,ok,right,down,ok,back" "$LATE2" SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_FAKE_DEAD_AFTER=6 SNTV_RESCUE_AFTER=2 SNTV_NO_RACE=1 HOME="$(home_with lateswitch "$NO_PREFLIGHT")"
grep -q "player: subtitles kept across the switch" "$OUT/subs_late_switch.log" && sed -n '/player: candidate 2\//,$p' "$OUT/subs_late_switch.log" | grep -q "subs: loaded 5 cues" && ! grep -q "subs: loaded 7 cues" "$OUT/subs_late_switch.log" \
  || { echo "the subtitles chosen did not stay across a source switch"; grep -E "subs|player: (candidate|rescue|subtitles)" "$OUT/subs_late_switch.log"; exit 1; }
run subs_late_bestfit_switch 2400 "right,ok,down,down,play" "$LATE2" SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_FAKE_DEAD_AFTER=1 SNTV_RESCUE_AFTER=1 SNTV_NO_RACE=1 HOME="$(home_with latebf "$NO_PREFLIGHT")"
grep -q "player: candidate 2/" "$OUT/subs_late_bestfit_switch.log" && grep -q "subs: a best-fit file for an earlier source is dropped" "$OUT/subs_late_bestfit_switch.log" \
  || { echo "best-fit files for a failed source were scored for the next"; grep -E "subs|player: (candidate|rescue)" "$OUT/subs_late_bestfit_switch.log"; exit 1; }
# a decoder failure teaches the TV profile only when one feature explains it (DTS alone);
# one needing Dolby Vision and TrueHD blames neither, and still counts against the source
CAPS="SNTV_FAKE_FAIL_LOADS=2 SNTV_FAKE_FAIL_CODE=1007 SNTV_NO_RACE=1"
run caps_one_feature 700 "right,ok,down,down,play,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/caps/dts/manifest.json,http://127.0.0.1:$PORT/manifest.json $CAPS HOME="$(home_with caps1 "$NO_PREFLIGHT")"
grep -q "tv profile: dts failed here" "$OUT/caps_one_feature.log" || { echo "a DTS source failing in the decoder did not teach the TV profile"; grep -E "tv profile|player: (candidate|source)" "$OUT/caps_one_feature.log"; exit 1; }
run caps_several 700 "right,ok,down,down,play,wait,wait" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/caps/dvtruehd/manifest.json,http://127.0.0.1:$PORT/manifest.json $CAPS HOME="$(home_with caps2 "$NO_PREFLIGHT")"
grep -q "tv profile: a source needing several features failed; none is blamed" "$OUT/caps_several.log" && ! grep -q "tv profile: .* failed here" "$OUT/caps_several.log" \
  && sed -n '/caps=dvtruehd/,$p' "$OUT/caps_several.log" | grep -q "health: .* failed" \
  || { echo "a source needing several features taught the TV profile (or did not count against the source)"; grep -E "tv profile|health|player: (candidate|source)" "$OUT/caps_several.log"; exit 1; }
run detail_ai 340 "right,right,ok,hold,down,down,ok" HOME="$(home_with ai "$AI")" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
run detail_ai_model 400 "right,right,ok,hold,down,down,ok" HOME="$(home_with aim '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key","aiModel":"missing-model"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
# three plain words are a title, not a request for the assistant: the addons are searched at once
run search_title_ai 520 "search,ok,t:the shawshank redemption,wait,wait,ok,wait,ok,wait,wait" HOME="$(home_with ai1 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tvKeyboard":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
grep -q "the assistant suggests" "$OUT/search_title_ai.log" && { echo "a plain title went to the assistant"; grep search "$OUT/search_title_ai.log"; exit 1; }
grep -q 'search: 1 result for "the shawshank redemption"' "$OUT/search_title_ai.log" && grep -qE "UI check: screen=(detail|player)" "$OUT/search_title_ai.log" || { echo "a three-word title was not found and opened"; grep -E "search|screen" "$OUT/search_title_ai.log"; exit 1; }
run search_ai 360 "search,t:something about prison and hope" HOME="$(home_with ai2 "$AI")" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
grep -q "search: no suggestions from the assistant" "$OUT/search_ai.log" && { echo "the assistant's suggestions were not used"; exit 1; } || true
grep -q "search: the assistant suggests 3 titles (Stories of hope behind bars), 3 ways to refine" "$OUT/search_ai.log" \
  || { echo "the assistant's summary or follow-ups were not taken"; grep "search" "$OUT/search_ai.log"; exit 1; }
# its follow-ups sit in the left list: one press asks again, the request kept
run search_ai_refine 520 "search,t:something about prison and hope,wait,wait,wait,wait,back,down,ok,wait,wait,wait" HOME="$(home_with ai3 "$AI")" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
grep -q 'search: refine "Only series"' "$OUT/search_ai_refine.log" && [ "$(grep -c "search: the assistant suggests" "$OUT/search_ai_refine.log")" -ge 2 ] \
  || { echo "a follow-up did not ask the assistant again"; grep "search" "$OUT/search_ai_refine.log"; exit 1; }
# two words that ask for a kind of thing are a request too
run search_ai_short 300 "search,t:korean thrillers,wait,wait" HOME="$(home_with ai4 "$AI")" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
grep -q "search: the assistant suggests" "$OUT/search_ai_short.log" || { echo "\"korean thrillers\" did not go to the assistant"; grep "search" "$OUT/search_ai_short.log"; exit 1; }
# a request with a film industry: the assistant's filters go to TMDB, whose titles join its picks; no word-by-word search
run search_ai_filters 420 "search,t:bollywood comedies from the nineties" HOME="$(home_with ai5 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key","tmdbKey":"0123456789abcdef0123456789abcdef"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1 SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg
grep -q "search: the assistant's filters: movies, language hi, country IN, 1 genre, 1990-1999" "$OUT/search_ai_filters.log" && grep -q "search: 2 more from TMDB for the assistant's filters" "$OUT/search_ai_filters.log" \
  || { echo "the assistant's filters did not bring TMDB's titles"; grep -E "search|tmdb" "$OUT/search_ai_filters.log"; exit 1; }
run player_translate 600 "right,ok,down,down,play,wait,down,right,right,right,ok,up,up,up,up,up,up,down,down,ok,wait,wait" SNTV_FAKE_START=6 HOME="$(home_with ai3 '{"prefSubs":"fra","subsAlways":1,"setupDone":1,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1
grep -q "player: subtitles sheet: AI translation in Fran" "$OUT/player_translate.log" || { echo "the AI translation was not offered under its language"; grep -E "subtitles" "$OUT/player_translate.log"; exit 1; }
run settings_ai 340 "left,down,down,down,down,down,ok,right,down,down,down,down,down,down,down,down" HOME="$(home_with ai4 "$AI")"
run home_compact 200 "right,right" HOME="$(home_with compact '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"homeStyle":2}')"
run pip_focus_ok 440 "$P,back,back,down,down,down,down,down,down,down,ok"
# screensaver after one quiet second (no keys at all)
run ambient 200 "" SNTV_AMBIENT_SECONDS=1
run ambient_fire 400 "" SNTV_AMBIENT_SECONDS=1 HOME="$(home_with fire '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceFree":0}')"
# the drawn scenes: Aurora, Rainy window, Now showing, Warp
for st in 5:Aurora 6:Rainy 7:Now 8:Warp; do
  run ambient_scene${st%%:*} 300 "" SNTV_AMBIENT_SECONDS=1 HOME="$(home_with scene${st%%:*} "{\"prefSubs\":\"eng\",\"subsAlways\":1,\"setupDone\":1,\"ambientStyle\":${st%%:*},\"ambientMinutes\":2}")"
  grep -q "ambient: ${st#*:}[a-z ]* screensaver" "$OUT/ambient_scene${st%%:*}.log" || { echo "the ${st#*:} screensaver did not start"; grep ambient "$OUT/ambient_scene${st%%:*}.log"; exit 1; }
done
# Depth: Home's backdrop pushes into its depth map (none when turned off); the Depth
# screensaver stands the clock behind the people
DEP="SNTV_DEPTH_URL=http://127.0.0.1:$PORT/depth SNTV_FAKE_MEM=8000000,6000000 SNTV_FAKE_DISK_FREE_MB=20000"   # a roomy TV: the runner's own memory and drive must not turn Depth off
run home_depth 260 "right,right,wait,wait,wait" $DEP
grep -q "depth: 43 backdrops have depth maps" "$OUT/home_depth.log" && grep -q "home: the backdrop moves in depth ([hm][0-9a-z]*)" "$OUT/home_depth.log" \
  || { echo "Home's backdrop did not move in depth"; grep -E "depth" "$OUT/home_depth.log"; exit 1; }
run home_depth_off 260 "right,right,wait,wait,wait" $DEP HOME="$(home_with depthoff '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"depthMotion":0}')"
! grep -q "home: the backdrop moves in depth" "$OUT/home_depth_off.log" || { echo "Home's backdrop moved in depth while turned off"; exit 1; }
# Depth makes way when the TV is short of memory or storage: its list is not even asked for, the backdrop stays still
run home_depth_lowmem 260 "right,right,wait,wait,wait" $DEP SNTV_FAKE_MEM=3000000,90000 HOME="$(home_with depthmem '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "depth: off while memory is low" "$OUT/home_depth_lowmem.log" && ! grep -q "home: the backdrop moves in depth\|backdrops have depth maps" "$OUT/home_depth_lowmem.log" \
  || { echo "Depth stayed on with the TV short of memory"; grep -E "depth|memory" "$OUT/home_depth_lowmem.log"; exit 1; }
# a TV's app drive often has a few hundred MB free: Depth (a few MB of maps) stays on
run home_depth_tv_drive 260 "right,right,wait,wait,wait" $DEP SNTV_FAKE_DISK_FREE_MB=400 HOME="$(home_with depthtvd '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "home: the backdrop moves in depth" "$OUT/home_depth_tv_drive.log" && ! grep -q "depth: off" "$OUT/home_depth_tv_drive.log" \
  || { echo "Depth went off with 400 MB free on the app's drive"; grep -E "depth|storage" "$OUT/home_depth_tv_drive.log"; exit 1; }
run home_depth_lowdisk 260 "right,right,wait,wait,wait" $DEP SNTV_FAKE_LOW_DISK=1 HOME="$(home_with depthdisk '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "depth: off while storage is low" "$OUT/home_depth_lowdisk.log" && ! grep -q "home: the backdrop moves in depth" "$OUT/home_depth_lowdisk.log" \
  || { echo "Depth stayed on with the TV's storage nearly full"; grep -E "depth|storage" "$OUT/home_depth_lowdisk.log"; exit 1; }
run ambient_depth 400 "" SNTV_AMBIENT_SECONDS=1 $DEP HOME="$(home_with adepth '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":9,"ambientMinutes":2}')"
grep -q "ambient: Depth screensaver" "$OUT/ambient_depth.log" && grep -q "ambient: depth backdrop [hm][0-9a-z]*, the clock [0-9]*% across behind them" "$OUT/ambient_depth.log" \
  || { echo "the Depth screensaver did not put the clock behind the people"; grep -E "ambient|depth" "$OUT/ambient_depth.log"; exit 1; }
run settings_depth 420 "left,down,down,down,down,down,ok,up,ok,t:depth on backdrops,down,down,down,down,left,ok"
grep -q "settings: found Depth on backdrops" "$OUT/settings_depth.log" || { echo "Depth on backdrops is not in Settings"; grep settings "$OUT/settings_depth.log"; exit 1; }
# your own video on TorBox: the picker lists the videos, the pick is kept, the fireplace plays it through TorBox's permanent link
TB="SNTV_TORBOX_API=http://127.0.0.1:$PORT/torbox"
run torbox_pick 600 "left,down,down,down,down,down,ok,down,down,right,down,down,down,down,down,down,down,down,down,down,down,ok,wait,down,ok" "$TB" HOME="$(home_with tbpick '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"breaksOn":1,"breakSched":7,"breakSound":1,"torboxKey":"tbkey"}')"
grep -q "torbox: 4 video files" "$OUT/torbox_pick.log" && grep -q "torbox: break recording set to torbox://torrents/42/3/adhan.mkv" "$OUT/torbox_pick.log" \
  || { echo "the TorBox picker did not set the break recording"; grep -E "torbox" "$OUT/torbox_pick.log"; exit 1; }
run torbox_fireplace 500 "" SNTV_AMBIENT_SECONDS=1 "$TB" HOME="$(home_with tbfire '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"torbox://torrents/41/0/fireplace.mp4","torboxKey":"tbkey"}')"
grep -q "video(null): load http://127.0.0.1:[0-9]*/torbox/torrents/requestdl?token=\*\*\*&torrent_id=41&file_id=0&redirect=true (Fireplace)" "$OUT/torbox_fireplace.log" \
  || { echo "the TorBox fireplace video did not play"; grep -E "ambient|video" "$OUT/torbox_fireplace.log"; exit 1; }
# a torrent-only source TorBox has cached plays from TorBox (not peers); the one it lacks stays a torrent
run torbox_torrent 700 "wait,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,down,ok,wait,wait" "$TB" HOME="$(home_with tbsrc '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey"}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbsrc/manifest.json"
grep -q "torbox: 1 of 2 torrents cached" "$OUT/torbox_torrent.log" && grep -q "torbox: cafecafecafecafecafecafecafecafecafecafe plays from TorBox" "$OUT/torbox_torrent.log" \
  && grep -q "video(null): load http://127.0.0.1:[0-9]*/video/sample_h264_aac.mkv?torbox=77" "$OUT/torbox_torrent.log" \
  && ! grep -q "tbkey" "$OUT/torbox_torrent.log" \
  || { echo "a TorBox-cached torrent did not play from TorBox"; grep -E "torbox|candidate|video" "$OUT/torbox_torrent.log"; exit 1; }
# hold OK on a torrent nobody has cached: your TorBox gets it, its row follows the download,
# and once TorBox has it, OK plays it from there
NAV="wait,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,down,down,down,down,down,down,down,down"
# P2P off, so the torrent ranks last with or without the engine in the build (CI's UI jobs have none):
# the downs land on it whatever else is listed
run torbox_get 1700 "$NAV,hold,$HOLD,release,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,ok,wait,wait,wait" "$TB" SNTV_TORBOX_POLL_S=1 HOME="$(home_with tbget '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey","preflight":0,"p2pFallback":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbget/manifest.json"
grep -q "torbox: F00DF00DF00DF00DF00DF00DF00DF00DF00DF00D added to your TorBox (42%)" "$OUT/torbox_get.log" && grep -q "on your TorBox: 87%" "$OUT/torbox_get.log" \
  && grep -q "is ready on your TorBox" "$OUT/torbox_get.log" && grep -q "torbox: f00df00df00df00df00df00df00df00df00df00d plays from TorBox" "$OUT/torbox_get.log" \
  && ! grep -q "tbkey" "$OUT/torbox_get.log" && ! grep -q "streams: #[0-9]* .*Netflix" "$OUT/torbox_get.log" \
  || { echo "a torrent was not got onto TorBox and played from there (or a link only another app opens was listed)"; grep -E "torbox|candidate|toast" "$OUT/torbox_get.log"; exit 1; }
# your own TorBox copy of the title heads the sources and plays first, through its direct link (a remake with another year does not match)
run torbox_library 700 "wait,ok,ok,wait,wait,wait,wait,wait,wait,wait,wait,up,ok,wait" "$TB" HOME="$(home_with tblib '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey"}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbsrc/manifest.json"
grep -q "torbox: 1 of your videos matches tt0111161" "$OUT/torbox_library.log" && grep -q "streams: #1 score [0-9]*  My TorBox Your copy" "$OUT/torbox_library.log" \
  && grep -q "player: candidate 1/[0-9]* My TorBox - Your copy @0s -> http://127.0.0.1:[0-9]*/video/sample_h264_aac.mkv?torbox=43" "$OUT/torbox_library.log" \
  || { echo "your TorBox copy did not come first"; grep -E "torbox|streams: #|candidate" "$OUT/torbox_library.log"; exit 1; }
# Your TorBox for an episode: only files that are this show (not "Breaking Bad Insider"), named
# for this episode by their own name (not the item's), and not titled as another episode
run torbox_library_episode 600 "right,ok,down,down,play,wait" "$TB" HOME="$(home_with tblibep '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkeylib"}')"
grep -q "torbox: 1 of your videos matches tt0903747:1:1" "$OUT/torbox_library_episode.log" && grep -q "torbox: Breaking.Bad.S01E01.Cats.in.the.Bag.1080p.mkv names \"Cat's in the Bag...\", another episode: left out" "$OUT/torbox_library_episode.log" \
  || { echo "your TorBox offered files of another show or episode"; grep -E "torbox" "$OUT/torbox_library_episode.log"; exit 1; }
# A season pack with no file named: the episode's own file plays, not the largest (another episode)
run torbox_pack 600 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait" "$TB" HOME="$(home_with tbpack '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey"}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbsrc/manifest.json"
grep -q "torbox: the episode's file by its name: Breaking Bad S01 1080p/Breaking.Bad.S01E01.1080p.mkv" "$OUT/torbox_pack.log" \
  || { echo "the season pack did not play the episode's file"; grep -E "torbox|candidate" "$OUT/torbox_pack.log"; exit 1; }
# A disc rip (title_t00, title_t01...): the names say nothing, so the AI is asked
run torbox_disc 600 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait" "$TB" HOME="$(home_with tbdisc '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey","aiProvider":1,"aiKey":"gsk_smoke_test_key"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1 SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbdisc/manifest.json"
grep -q "torbox: the AI picked the episode's file: BREAKING_BAD_D1/title_t01.mkv" "$OUT/torbox_disc.log" && ! grep -q "gsk_" "$OUT/torbox_disc.log" \
  || { echo "the AI did not pick the disc rip's episode"; grep -E "torbox|ai:" "$OUT/torbox_disc.log"; exit 1; }
# A pack that names its episodes but not this one: no extra or other episode plays; the next source does
run torbox_pack_missing 700 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait" "$TB" HOME="$(home_with tbmiss '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey","preflight":0}')" SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbmiss/manifest.json"
grep -q "torbox: no file in this torrent is S1E1 (it names other episodes)" "$OUT/torbox_pack_missing.log" && grep -q "player: candidate 2/" "$OUT/torbox_pack_missing.log" && ! grep -q "torbox=81" "$OUT/torbox_pack_missing.log" \
  || { echo "a pack without the episode played something else"; grep -E "torbox|player: (candidate|source)" "$OUT/torbox_pack_missing.log"; exit 1; }
# The next source a TorBox torrent: made ready on TorBox while the first plays, so when the first dies it takes over at once
run torbox_standby 4000 "right,ok,down,down,play,wait" "$TB" HOME="$(home_with tbstand '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"torboxKey":"tbkey","preflight":0,"aiProvider":1,"aiKey":"gsk_smoke_test_key"}')" SNTV_AI_API=http://127.0.0.1:$PORT/ai/v1 SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json,http://127.0.0.1:$PORT/tbdisc/manifest.json" SNTV_FAKE_NO_TEXT_TRACKS=1 SNTV_FAKE_DEAD_AFTER=7 SNTV_RESCUE_AFTER=2 SNTV_WARM_AFTER=1
grep -q "player: next source 2 is ready on TorBox" "$OUT/torbox_standby.log" && grep -q "player: next source 3 answers" "$OUT/torbox_standby.log" && grep -q "player: source 2 was made ready on TorBox ahead" "$OUT/torbox_standby.log" \
  || { echo "the TorBox source standing by was not ready ahead"; grep -E "player: (next|source|rescue|candidate)" "$OUT/torbox_standby.log"; exit 1; }
# resting on a title page asks for its sources ahead: Play opens on them
run detail_prefetch 900 "wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,ok,wait" HOME="$(home_with prefetch "$ABOUT_HOME")"
grep -q "streams: using [0-9]* prefetched sources for tt0111161" "$OUT/detail_prefetch.log" || { echo "the title page did not prefetch its sources"; grep streams "$OUT/detail_prefetch.log"; exit 1; }
# The player's extras. Back after days away: "Previously" with the last episode's summary, and a little earlier than where it stopped
PL='{"prefSubs":"eng","subsAlways":1,"setupDone":1}'
run player_previously 330 "right,ok,down,down,right,play,wait,wait,wait" SNTV_AWAY_S=200000 HOME="$(home_with prev "$PL")"
grep -q "player: previously card for tt0903747:1:1" "$OUT/player_previously.log" || { echo "no Previously card after days away"; grep player "$OUT/player_previously.log"; exit 1; }
AW_HOME="$(home_with away "$PL")"; cp "$ROOT/tests/fixtures/library.json" "$AW_HOME/.local/share/stremio/nativetv/library.json"
run player_away 700 "wait,wait,ok,wait,wait,ok,wait,wait,wait,ok,wait,wait,wait" HOME="$AW_HOME"
grep -q "player: away [0-9]* days, resuming 30 s earlier" "$OUT/player_away.log" && grep -q "player: candidate 1/[0-9]* .* @1470s" "$OUT/player_away.log" || { echo "a week away did not resume earlier"; grep player "$OUT/player_away.log"; exit 1; }
# resuming from the link it played last, another source can be chosen: Other sources on the
# title page lists them all, and the one chosen starts where it stopped
OS_HOME="$(home_with othersrc "{\"prefSubs\":\"eng\",\"subsAlways\":1,\"setupDone\":1,\"resume:tt0903747\":{\"url\":\"http://127.0.0.1:$PORT/video/sample_h264_aac.mkv\",\"video_id\":\"tt0903747:2:3\"}}")"; cp "$ROOT/tests/fixtures/library.json" "$OS_HOME/.local/share/stremio/nativetv/library.json"
run other_sources 800 "wait,wait,ok,wait,wait,right,right,ok,wait,wait,wait,wait,ok,wait,wait,wait" HOME="$OS_HOME"
grep -q "detail: other sources for tt0903747:2:3" "$OUT/other_sources.log" && grep -q "player: candidate [0-9]*/[0-9]* .* @1[45][0-9][0-9]s" "$OUT/other_sources.log" \
  || { echo "Other sources did not list the sources, or the one chosen did not resume"; grep -E "detail|player: candidate|streams:" "$OUT/other_sources.log"; exit 1; }
# resuming, the first source fails before it loads (it reports 0:00): the next one starts at the resume point, not the beginning
RF_HOME="$(home_with resumefail "$NO_PREFLIGHT")"; cp "$ROOT/tests/fixtures/library.json" "$RF_HOME/.local/share/stremio/nativetv/library.json"
run resume_failover 900 "wait,wait,ok,wait,wait,ok,wait,wait,wait,ok,wait,wait,wait" "$LIGHTER" HOME="$RF_HOME" SNTV_FAKE_FAIL_LOADS=1 SNTV_NO_RACE=1
grep -q "player: candidate 2/[0-9]* .* @1[45][0-9][0-9]s" "$OUT/resume_failover.log" || { echo "a failed first source lost the resume point"; grep "player: candidate" "$OUT/resume_failover.log"; exit 1; }
# Blue: the last 10 s again, with subtitles on for them when they were off
run player_replay 900 "right,ok,down,down,right,play,wait,wait,wait,wait,wait,wait,blue,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" HOME="$(home_with replay '{"prefSubs":"","setupDone":1}')"
grep -q "player: replay from [0-9]* s with subtitles" "$OUT/player_replay.log" && grep -q "player: replay over, subtitles off again" "$OUT/player_replay.log" \
  || { echo "the instant replay did not bring subtitles for its 10 s"; grep -E "replay|subs" "$OUT/player_replay.log"; exit 1; }
# Info: who is in it
run player_cast 420 "right,ok,down,down,right,play,wait,wait,wait,wait,info,right" HOME="$(home_with pcast "$PL")"
grep -q "player: cast drawer (3 people)" "$OUT/player_cast.log" || { echo "Info did not show the cast"; grep player "$OUT/player_cast.log"; exit 1; }
# most LG remotes have no Info key: the bar's Cast button (its last) opens the same drawer
run player_cast_button 700 "right,ok,down,down,right,play,wait,wait,wait,wait,down,right,right,right,right,right,right,right,right,right,right,right,right,ok" HOME="$(home_with pcastb "$PL")"
grep -q "player: cast drawer (3 people)" "$OUT/player_cast_button.log" || { echo "the Cast button did not show the cast"; grep player "$OUT/player_cast_button.log"; exit 1; }
# Scenes: the rail from the bar, and a moment marked there is kept for the video
run player_scenes 640 "right,ok,down,down,right,play,wait,wait,wait,wait,down,right,right,right,right,right,right,right,right,ok,left,left,left,ok,wait,wait" HOME="$(home_with scenes "$PL")"
grep -q "player: scenes rail (3)" "$OUT/player_scenes.log" && grep -q "player: moment marked at [0-9]* s" "$OUT/player_scenes.log" \
  && tr -d ' \t\n' < "$OUT/home_scenes/.local/share/stremio/nativetv/settings.json" | grep -q '"marks":{"tt0903747:1:2":\[' \
  || { echo "the Scenes rail did not mark a moment"; grep -E "scene|marked" "$OUT/player_scenes.log"; exit 1; }
# the last minutes: the next episode's likely file is fetched ahead
run player_warm_next 600 "right,ok,down,down,right,play,wait" SNTV_FAKE_DURATION=1200 SNTV_FAKE_START=900 HOME="$(home_with warmnext "$PL")"
grep -q "streams: next episode's file warmed (206" "$OUT/player_warm_next.log" || { echo "the next episode's file was not warmed"; grep streams "$OUT/player_warm_next.log"; exit 1; }
# a film with a scene after the credits says so (TMDB's keywords)
run player_stinger 500 "right,right,ok,wait,ok,wait,ok,wait,wait" SNTV_FAKE_DURATION=3600 SNTV_FAKE_START=3300 SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg \
  HOME="$(home_with stinger '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}')"
grep -q "player: the film has a scene after the credits" "$OUT/player_stinger.log" || { echo "the after-credits scene was not mentioned"; grep -E "tmdb|player" "$OUT/player_stinger.log"; exit 1; }
# second subtitles stay on for the show; night mode plays quieter
run player_second_subs 400 "right,ok,down,down,right,play,wait,wait,wait,wait" HOME="$(home_with second '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"secondSubs":{"tt0903747":"spa"}}')"
grep -q "player: second subtitles spa (on for this show)" "$OUT/player_second_subs.log" || { echo "the show's second subtitles did not come back"; grep -E "subtitles|subs:" "$OUT/player_second_subs.log"; exit 1; }
run player_night 330 "right,ok,down,down,right,play,wait,wait,wait" HOME="$(home_with night '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"nightMode":2}')"
grep -q "player: night mode on" "$OUT/player_night.log" && grep -q "video(null): volume 60" "$OUT/player_night.log" || { echo "night mode did not lower the volume"; grep -E "night|volume" "$OUT/player_night.log"; exit 1; }
# a series you had caught up on comes back to Continue Watching when an episode airs after you watched (Stremio's own apps do too)
NE_WAIT="$(printf 'wait,%.0s' $(seq 1 76))"
NE_HOME="$(home_with newep "$PL")"; cp "$ROOT/tests/fixtures/library_newep.json" "$NE_HOME/.local/share/stremio/nativetv/library.json"
run newep_row 1900 "${NE_WAIT}ok,wait,wait" HOME="$NE_HOME"
grep -q "newep: Breaking Bad has 2 new episodes from tt0903747:2:2" "$OUT/newep_row.log" && grep -q "UI check: screen=detail" "$OUT/newep_row.log" \
  || { echo "a new episode did not bring the series back"; grep -E "newep|UI check" "$OUT/newep_row.log"; exit 1; }
# put away from the options, it stays away until a later episode
NE_HOME2="$(home_with newep2 "$PL")"; cp "$ROOT/tests/fixtures/library_newep.json" "$NE_HOME2/.local/share/stremio/nativetv/library.json"
run newep_dismiss 2000 "${NE_WAIT}yellow,down,ok,wait" HOME="$NE_HOME2"
tr -d ' \t\n' < "$NE_HOME2/.local/share/stremio/nativetv/settings.json" | grep -q '"newEpSeen":{"tt0903747":"tt0903747:2:3"}' \
  || { echo "removing the new episode from Continue Watching was not kept"; grep -E "newep|toast" "$OUT/newep_dismiss.log"; exit 1; }
# a colour palette sets the accent and the background together
PAL_HOME="$(home_with palette '{"theme":7,"lookStreaming":1,"themeAccent":0,"themeBg":0,"setupDone":1,"autoTrailers":0,"prefSubs":"eng","subsAlways":1}')"
run settings_palette 380 "wait,left,down,down,down,down,down,ok,wait,down,down,down,down,down,right,down,right" HOME="$PAL_HOME"
tr -d ' \t\n' < "$PAL_HOME/.local/share/stremio/nativetv/settings.json" | grep -q '"themeAccent":4,"themeBg":1' \
  || { echo "the palette did not set the accent and background"; grep -o '"theme[A-Za-z]*":[0-9]*' "$PAL_HOME/.local/share/stremio/nativetv/settings.json"; exit 1; }
# a 1 GB TV: the saver is on from the start, and the artwork cache shrinks while a video fills the screen
run memory_playback 520 "right,ok,down,down,play,wait,wait,wait,wait,wait,back,back,back,wait,wait" SNTV_FAKE_MEM=1000000,500000 HOME="$(home_with memplay "$PL")"
grep -q "memory saver on (small TV)" "$OUT/memory_playback.log" && grep -q "memory: artwork trimmed while the video plays" "$OUT/memory_playback.log" \
  && grep -q "memory: artwork budget back after the video" "$OUT/memory_playback.log" \
  || { echo "the memory saver did not trim during playback"; grep -E "memory" "$OUT/memory_playback.log"; exit 1; }
run ambient_fire_video 500 "" SNTV_AMBIENT_SECONDS=1 HOME="$(home_with firev '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
grep -q "ambient: fireplace video 1/4" "$OUT/ambient_fire_video.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ.m3u8 (Fireplace)" "$OUT/ambient_fire_video.log" \
  || { echo "the fireplace video did not play"; grep -E "ambient|youtube|video" "$OUT/ambient_fire_video.log"; exit 1; }
# refused: YouTube's own player where the device has one; elsewhere Settings says so on the row
run ambient_fire_embed 500 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_EMBED=1 HOME="$(home_with firee '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
grep -q "video(null): load youtube-embed:dQw4w9WgXcQ (Fireplace)" "$OUT/ambient_fire_embed.log" || { echo "a refused fireplace video did not play in YouTube's own player"; grep -E "ambient|youtube|video" "$OUT/ambient_fire_embed.log"; exit 1; }
FRH="$(home_with firer '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
run ambient_fire_refused 500 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 HOME="$FRH"
run ytweb_fire 500 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=1 HOME="$(home_with ytwf '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
grep -q "video: Cuelight YouTube opened dQw4w9WgXcQ at 0s (loop)" "$OUT/ytweb_fire.log" || { echo "a refused fireplace video did not open in Cuelight YouTube"; grep -E "ambient|youtube|video" "$OUT/ytweb_fire.log"; exit 1; }
python3 -I -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1])).get("fireplaceRefused") == "https://www.youtube.com/watch?v=dQw4w9WgXcQ" else 1)' "$FRH/.local/share/stremio/nativetv/settings.json" \
  || { echo "a refused fireplace video was not noted for Settings"; grep -E "ambient|youtube" "$OUT/ambient_fire_refused.log"; exit 1; }
# a TV that will not let Cuelight open the companion (it answers the launch with a no): said on screen and in
# Settings, and the fireplace stops waiting for it
run ytweb_fire_refused 600 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FAKE_YTWEB=1 SNTV_FAKE_YTWEB_REFUSE="Permission denied" HOME="$(home_with ytwfr '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
grep -q "ambient: the TV would not open Cuelight YouTube for the fireplace (Permission denied)" "$OUT/ytweb_fire_refused.log" && grep -q "toast: The TV would not open Cuelight YouTube: Permission denied" "$OUT/ytweb_fire_refused.log" \
  && python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get("ytwebRefused") == "Permission denied" else 1)' "$OUT/home_ytwfr/.local/share/stremio/nativetv/settings.json" \
  || { echo "a TV refusing to open Cuelight YouTube was not said"; grep -E "ambient|Cuelight YouTube|toast" "$OUT/ytweb_fire_refused.log"; exit 1; }
# the fireplace's YouTube video already saved on the TV (for a break) plays from there, YouTube refusing or not
FSH="$(home_with firesv '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
mkdir -p "$FSH/.local/share/stremio/nativetv/sounds" && printf 'saved' > "$FSH/.local/share/stremio/nativetv/sounds/yt-dQw4w9WgXcQ-video.mp4"
run ambient_fire_saved 300 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 HOME="$FSH"
grep -q "ambient: fireplace video from the copy saved on the TV" "$OUT/ambient_fire_saved.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/sounds/yt-dQw4w9WgXcQ-video.mp4 (Fireplace)" "$OUT/ambient_fire_saved.log" \
  || { echo "the fireplace did not play its video saved on the TV"; grep -E "ambient|youtube|video" "$OUT/ambient_fire_saved.log"; exit 1; }
# no fireplace video set: the free one under an open licence plays, credited; one that will not load gives way to the next
run ambient_fire_free 300 "" SNTV_AMBIENT_SECONDS=1 SNTV_FIRE_LIBRARY="http://127.0.0.1:$PORT/firelib" HOME="$(home_with firefree '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2}')"
grep -q "ambient: the free fireplace video (none set)" "$OUT/ambient_fire_free.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/firelib/betv-16515bcm-fireplace-plain.mp4 (Fireplace)" "$OUT/ambient_fire_free.log" \
  || { echo "the free fireplace video did not play"; grep -E "ambient|video" "$OUT/ambient_fire_free.log"; exit 1; }
run ambient_fire_free_next 400 "" SNTV_AMBIENT_SECONDS=1 SNTV_FAKE_FETCH=1 SNTV_FIRE_LIBRARY="http://127.0.0.1:$PORT/firelib" HOME="$(home_with firefree2 '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2}')"
grep -q "video(null): load http://127.0.0.1:[0-9]*/firelib/FireplaceStockFootage.mp4 (Fireplace)" "$OUT/ambient_fire_free_next.log" && grep -q "ambient: the fireplace video would not play; the drawn fire instead" "$OUT/ambient_fire_free_next.log" \
  || { echo "free fireplace videos that would not load did not give way to the next, then the drawn fire"; grep -E "ambient|video" "$OUT/ambient_fire_free_next.log"; exit 1; }
# YouTube refusing the one set, with nothing else to play it: the free one
run ambient_fire_refused_free 400 "" SNTV_AMBIENT_SECONDS=1 SNTV_YT_BASE=http://127.0.0.1:9 SNTV_FIRE_LIBRARY="http://127.0.0.1:$PORT/firelib" HOME="$(home_with firerf '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceVideo":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"}')"
grep -q "ambient: the free fireplace video (YouTube refused the one set)" "$OUT/ambient_fire_refused_free.log" && grep -q "video(null): load http://127.0.0.1:[0-9]*/firelib/betv-16515bcm-fireplace-plain.mp4 (Fireplace)" "$OUT/ambient_fire_refused_free.log" \
  || { echo "a refused fireplace video did not give way to the free one"; grep -E "ambient|youtube|video" "$OUT/ambient_fire_refused_free.log"; exit 1; }
# switched off: the drawn fire
run ambient_fire_drawn 300 "" SNTV_AMBIENT_SECONDS=1 SNTV_FIRE_LIBRARY="http://127.0.0.1:$PORT/firelib" HOME="$(home_with firedr '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":4,"ambientMinutes":2,"fireplaceFree":0}')"
! grep -q "video(null): load" "$OUT/ambient_fire_drawn.log" || { echo "the free fireplace video played though switched off"; exit 1; }
# Clock faces: every one draws (flip, analog, words ... aurora, planets, prayer orbit, horizon, orrery), and the island's styles
for f in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  run ambient_face$f 300 "" SNTV_AMBIENT_SECONDS=1 HOME="$(home_with face$f "{\"prefSubs\":\"eng\",\"subsAlways\":1,\"setupDone\":1,\"ambientStyle\":2,\"ambientMinutes\":2,\"clockFace\":$f,\"breaksOn\":1,\"breakSched\":0,\"breakMethod\":1,\"breakLat\":\"43.676\",\"breakLon\":\"-79.485\",\"breakCity\":\"Toronto\"}")"
  grep -q "ambient: Clock screensaver with [0-9]* backdrops, face " "$OUT/ambient_face$f.log" && ! grep -q "no big clock digits" "$OUT/ambient_face$f.log" || { echo "clock face $f did not draw"; grep -E "ambient|font" "$OUT/ambient_face$f.log"; exit 1; }
done
grep -q "weather: 18C, Partly cloudy" "$OUT/ambient_face15.log" || { echo "the weather clock did not get the weather"; grep -E "weather" "$OUT/ambient_face15.log"; exit 1; }
# Shuffle picks a screensaver each time; the screensaver dims and then goes black after its set times
run ambient_shuffle 200 "" SNTV_AMBIENT_SECONDS=1 SNTV_AMBIENT_PICK=2 HOME="$(home_with shuffle '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":11,"ambientMinutes":2}')"
grep -q "ambient: shuffle picked Clock, face " "$OUT/ambient_shuffle.log" && grep -q "ambient: Clock screensaver with" "$OUT/ambient_shuffle.log" || { echo "shuffle did not pick a screensaver"; grep ambient "$OUT/ambient_shuffle.log"; exit 1; }
run ambient_gallery 400 "" SNTV_AMBIENT_SECONDS=1 SNTV_ART_API=http://127.0.0.1:$PORT/artic SNTV_ART_IIIF=http://127.0.0.1:$PORT/artic/iiif HOME="$(home_with gal '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":10,"ambientMinutes":2}')"
grep -q "ambient: a gallery of 3 works from the Art Institute of Chicago" "$OUT/ambient_gallery.log" || { echo "the gallery did not get its works"; grep ambient "$OUT/ambient_gallery.log"; exit 1; }
run ambient_dim 1200 "" SNTV_AMBIENT_SECONDS=1 SNTV_AMBIENT_DIM_S=1 SNTV_AMBIENT_OFF_S=3 HOME="$(home_with dim '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":0,"ambientMinutes":2}')"
grep -q "ambient: dimmed after [0-9]* s" "$OUT/ambient_dim.log" && grep -q "ambient: picture off after [0-9]* s" "$OUT/ambient_dim.log" || { echo "the screensaver did not dim and go black"; grep ambient "$OUT/ambient_dim.log"; exit 1; }
run ambient_no_dim 200 "" SNTV_AMBIENT_SECONDS=1 HOME="$(home_with nodim '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"ambientStyle":0,"ambientMinutes":2,"ambientDimMin":30}')"
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); sys.exit(0 if d.get("ambientDimMinV2", 0) == 0 else 1)' "$OUT/home_nodim/.local/share/stremio/nativetv/settings.json" && ! grep -q "ambient: dimmed" "$OUT/ambient_no_dim.log" \
  || { echo "the screensaver still dims by default"; grep -E "ambient" "$OUT/ambient_no_dim.log"; exit 1; }
run island_dial 150 "" HOME="$(home_with isl_dial '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"islandStyle":3}')"
run island_seconds 150 "" HOME="$(home_with isl_sec '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"islandStyle":2}')"
run ambient_paused 300 "right,ok,down,down,play,wait,ok" SNTV_AMBIENT_SECONDS=1 SNTV_FAKE_START=200 HOME="$(home_with fresh_ambient_paused '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run login_qr 260 "left,down,down,down,down,down,down,ok"
run login_pw 280 "left,down,down,down,down,down,down,ok,wait"
# touch: a tap lands on what is drawn there (canvas units), a swipe moves the focus
run tap_rail 160 "wait,tap:52:496,wait" SNTV_EXPECT_SCREEN=search HOME="$(home_with tap1 '{"setupDone":1}')"
run tap_card 200 "wait,wait,tap:877:810,wait,wait" SNTV_EXPECT_SCREEN=detail HOME="$(home_with tap2 '{"setupDone":1}')"
run tap_play 260 "wait,wait,tap:877:810,wait,wait,tap:175:632,wait,wait,wait" SNTV_EXPECT_SCREEN=streams HOME="$(home_with tap3 '{"setupDone":1}')"
run tap_sheet 240 "wait,wait,yellow,wait,wait,tap:700:562,wait,wait" SNTV_EXPECT_SCREEN=home HOME="$(home_with tap4 '{"setupDone":1}')"
grep -q "toast: Added to Library" "$OUT/tap_sheet.log" || { echo "tapping a sheet's choice did not take it"; exit 1; }
run tap_typing 400 "wait,tap:52:496,wait,tap:300:178,wait,tap:212:284,tap:370:428,tap:450:284,tap:132:284,wait,wait" HOME="$(home_with tap5 '{"setupDone":1}')"
grep -q 'search: 1 suggestion, first "Breaking Bad"' "$OUT/tap_typing.log" || { echo "tapped keys did not type"; exit 1; }
run tap_settings_key 260 "wait,tap:52:760,wait,tap:800:714,wait,tap:600:560,tap:700:560,wait" SNTV_EXPECT_SCREEN=settings HOME="$(home_with tap6 '{"setupDone":1}')"
run handheld_home 160 "wait,wait" SNTV_WINDOW=1920x864 SNTV_HANDHELD=1 HOME="$(home_with hh '{"setupDone":1}')"
grep -q "handheld: drawn larger" "$OUT/handheld_home.log" || { echo "handheld mode did not turn on"; exit 1; }
# a phone held upright: its own layouts (780 units across, the tab bar along the bottom)
PH="SNTV_WINDOW=450x1000 SNTV_HANDHELD=1"
run phone_home 160 "wait,wait,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with ph1 '{"setupDone":1}')"
run phone_title 220 "wait,wait,tap:152:1050,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=detail HOME="$(home_with ph2 '{"setupDone":1}')"
run phone_series 260 "wait,wait,tap:400:1050,wait,wait,wait,up,up,up,up" $PH SNTV_EXPECT_SCREEN=detail HOME="$(home_with ph3 '{"setupDone":1}')"
run phone_sources 300 "wait,wait,tap:152:1050,wait,wait,tap:390:666,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=streams HOME="$(home_with ph4 '{"setupDone":1}')"
run phone_search 300 "wait,tap:390:1660,wait,tap:390:180,wait,t:brea,wait,wait,ok,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=search HOME="$(home_with ph5 '{"setupDone":1}')"
# a phone keyboard's space bar sends the key as well as the text: a space, not OK (which closed the keyboard)
run phone_search_space 400 "wait,tap:390:1660,wait,tap:390:180,wait,t:breaking,space,t:bad,wait,wait,ok,wait,wait" $PH SNTV_EXPECT_SCREEN=search HOME="$(home_with phsp '{"setupDone":1}')"
grep -qE '"recentSearches":[[:space:]]*\["breaking bad"\]' "$OUT/home_phsp/.local/share/stremio/nativetv/settings.json" \
  || { echo "the space bar of a phone keyboard did not type a space"; grep -o '"recentSearches":[^]]*]' "$OUT/home_phsp/.local/share/stremio/nativetv/settings.json"; exit 1; }
run phone_settings 240 "wait,tap:702:1660,wait,tap:300:300,wait,wait" $PH SNTV_EXPECT_SCREEN=settings HOME="$(home_with ph6 '{"setupDone":1}')"
run phone_explore 220 "wait,tap:234:1660,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=explore HOME="$(home_with ph7 '{"setupDone":1}')"
run phone_library 200 "wait,tap:546:1660,wait,wait" $PH SNTV_EXPECT_SCREEN=library HOME="$(home_with ph8 '{"setupDone":1}')"
run phone_setup 160 "wait,wait" $PH SNTV_EXPECT_SCREEN=welcome HOME="$(home_with ph9 '{}')"
run phone_signin 260 "wait,tap:702:1660,wait,tap:300:300,wait,tap:300:270,wait,wait" $PH SNTV_EXPECT_SCREEN=login HOME="$(home_with ph10 '{"setupDone":1}')"
run phone_signin_pw 300 "wait,tap:702:1660,wait,tap:300:300,wait,tap:300:270,wait,tap:390:791,wait,t:me@example.com,wait" $PH SNTV_EXPECT_SCREEN=login HOME="$(home_with ph11 '{"setupDone":1}')"
run phone_about 280 "wait,tap:702:1660,wait,tap:300:1430,wait,tap:400:364,wait,wait" $PH SNTV_EXPECT_SCREEN=whatsnew HOME="$(home_with ph12 '{"setupDone":1}')"
run phone_diagnostics 280 "wait,tap:702:1660,wait,tap:300:1430,wait,tap:400:803,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=diagnostics HOME="$(home_with ph13 '{"setupDone":1}')"
run phone_calendar 220 "wait,tap:546:1660,wait,tap:370:76,wait,wait" $PH SNTV_EXPECT_SCREEN=calendar HOME="$(home_with ph14 '{"setupDone":1}')"
# a phone's real size (600 units across, as 360-400 dp phones are): settings rows keep their titles with the
# value below, the sync page fits a short screen with its button, the recommendation names the stream
PR="SNTV_WINDOW=450x975 SNTV_HANDHELD=1 SNTV_PHONE_UNITS=600"
run phone600_breaks 400 "wait,tap:540:1227,wait,tap:300:352,wait,wait" $PR SNTV_EXPECT_SCREEN=settings HOME="$(home_with p6br '{"setupDone":1}')"
run phone600_sources 400 "wait,wait,wait,ok,wait,wait,ok,wait,wait,wait" $PR SNTV_EXPECT_SCREEN=streams HOME="$(home_with p6src '{"setupDone":1,"autoplay":0,"autoRec":0}')"
run phone600_sync_short 300 "wait,wait,wait" SNTV_WINDOW=450x800 SNTV_HANDHELD=1 SNTV_PHONE_UNITS=600 SNTV_OPEN_LINK="cuelight://sync" SNTV_PHONE_HOST=192.168.1.20 SNTV_PHONE_PORT=11640 SNTV_SYNC_PIN=4805 SNTV_EXPECT_SCREEN=sync HOME="$(home_with p6sy '{"setupDone":1}')"
# touch that follows the finger: a dragged page and row, the player's gestures (a phone plays sideways)
# a phone's size: its canvas from its width in dp (here 450 px at 1.1 px a dp: 409 dp, 660 units across)
run phone_size_dp 160 "wait,wait" SNTV_WINDOW=450x1000 SNTV_HANDHELD=1 SNTV_DENSITY=1.1 SNTV_EXPECT_SCREEN=home HOME="$(home_with psz '{"setupDone":1}')"
grep -q "gfx: upright on a phone, 660 units across" "$OUT/phone_size_dp.log" || { echo "the phone's canvas did not follow its size in dp"; grep "gfx:" "$OUT/phone_size_dp.log"; exit 1; }
# a thumb's swipe leans sideways: still the page up and down; across chips that do not scroll, nothing
run phone_drag_lean 220 "wait,wait,wait,drag:390:1400:190:700,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with plean '{"setupDone":1}')"
grep -q "touch: drag on home follows the finger up and down" "$OUT/phone_drag_lean.log" || { echo "a leaning swipe did not scroll the page"; grep "touch:" "$OUT/phone_drag_lean.log"; exit 1; }
run phone_drag_chips 220 "wait,tap:546:1660,wait,drag:500:160:100:160,wait" $PH SNTV_EXPECT_SCREEN=library HOME="$(home_with pchips '{"setupDone":1}')"
grep -q "touch: drag on library at .*: nothing scrolls across there" "$OUT/phone_drag_chips.log" \
  || { echo "a swipe across the Library's filters did something"; grep -E "touch:" "$OUT/phone_drag_chips.log"; exit 1; }
# the hero is swiped across to the next title; Home tapped again goes back to the top
run phone_hero_swipe 240 "wait,wait,wait,drag:600:400:100:400,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with phsw '{"setupDone":1}')"
grep -q "home: hero swiped to [0-9]* of [0-9]*" "$OUT/phone_hero_swipe.log" || { echo "a swipe across the hero did not turn its page"; exit 1; }
run phone_home_retap 280 "wait,wait,wait,drag:390:1400:390:400,wait,tap:90:1640,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with phrt '{"setupDone":1}')"
grep -q "home: back to the top" "$OUT/phone_home_retap.log" || { echo "Home tapped again did not go back to the top"; exit 1; }
# an empty Search on a phone: Discover; a genre opens Explore on it, a poster its title page
run phone_discover_genre 300 "wait,wait,wait,tap:390:1660,wait,tap:100:464,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=explore HOME="$(home_with phdg '{"setupDone":1}')"
grep -q "explore: opened on Action" "$OUT/phone_discover_genre.log" || { echo "Discover's genre did not open Explore on it"; grep -E "search|explore" "$OUT/phone_discover_genre.log"; exit 1; }
run phone_discover_title 300 "wait,wait,wait,tap:390:1660,wait,tap:390:940,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=detail HOME="$(home_with phdt '{"setupDone":1}')"
grep -q "search: discover opens " "$OUT/phone_discover_title.log" || { echo "Discover's poster did not open its title"; exit 1; }
# a phone with Continue Watching and a library: it opens at the top, the hero's catalog comes
# (rows below the first screen once came only as the focus reached them, which a finger never moves)
PHL="$(home_with phlib '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"; cp "$ROOT/tests/fixtures/library.json" "$PHL/.local/share/stremio/nativetv/library.json"
run phone_home_library 300 "wait,wait,wait,wait,wait,wait,wait,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$PHL"
grep -q "home: the hero shows [1-9][0-9]* titles" "$OUT/phone_home_library.log" || { echo "with a library the phone's hero never came"; exit 1; }
run phone_drag_page 220 "wait,wait,wait,drag:390:1400:390:500,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with pd1 '{"setupDone":1}')"
run phone_drag_row 220 "wait,wait,wait,drag:600:1050:100:1050,wait" $PH SNTV_EXPECT_SCREEN=home HOME="$(home_with pd2 '{"setupDone":1}')"
LS="SNTV_WINDOW=1000x450 SNTV_HANDHELD=1"
run fs_player 460 "right,right,ok,wait,ok,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,tap:960:600,wait,wait" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs1 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
run fs_player_hidden 460 "right,right,ok,wait,ok,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs2 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
run fs_player_subs 620 "right,right,ok,wait,ok,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,tap:960:600,wait,wait,wait,wait,wait,wait,tap:1488:84,wait,wait" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs3 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
run fs_player_volume 420 "right,right,ok,wait,ok,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait,drag:1500:300:1500:600" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs4 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
grep -q "player: volume [0-9]*%" "$OUT/fs_player_volume.log" || { echo "sideways, a drag down the right did not change the volume"; exit 1; }
FSP="right,right,ok,wait,ok,wait,wait,wait,ok,wait,wait,wait,wait,wait,wait"
run fs_player_dtap 420 "$FSP,dtap:1500:540,wait" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs5 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
grep -q "player: double tap: +10 s" "$OUT/fs_player_dtap.log" || { echo "a double tap on the picture did not skip"; grep "player:" "$OUT/fs_player_dtap.log" | tail -5; exit 1; }
run fs_player_seek 420 "$FSP,drag:600:540:1200:540" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs6 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
grep -q "player: dragged to [0-9]* s" "$OUT/fs_player_seek.log" || { echo "a drag across the picture did not seek"; exit 1; }
run fs_player_bright 420 "$FSP,drag:400:300:400:700" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs7 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
grep -q "player: brightness [0-9]*%" "$OUT/fs_player_bright.log" || { echo "a drag down the left of the picture did not change the brightness"; exit 1; }
# A phone on its side (2025x820): a series' title page shows its whole episode row, so a tap there opens the episode
run ls_series_episodes 300 "right,ok,wait,wait,tap:400:640,wait,wait" SNTV_WINDOW=1250x506 SNTV_HANDHELD=1 SNTV_EXPECT_SCREEN=streams HOME="$(home_with lse '{"setupDone":1}')"
# a finger held still on the picture plays at double speed until it lifts (and is not a tap)
run fs_player_hold 640 "$FSP,press:1300:420,wait,wait,wait,wait,wait" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs8 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
grep -q "player: held: double speed" "$OUT/fs_player_hold.log" && grep -q "player: let go: back to 1x" "$OUT/fs_player_hold.log" || { echo "holding the picture did not play at double speed and back"; grep "player:" "$OUT/fs_player_hold.log" | tail -5; exit 1; }
# a swipe up from the bottom edge is the system's, not the brightness
run fs_player_edge 420 "$FSP,drag:400:1060:400:600" $LS SNTV_EXPECT_SCREEN=player HOME="$(home_with fs9 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
! grep -q "player: brightness [0-9]*%" "$OUT/fs_player_edge.log" || { echo "a swipe from the bottom edge changed the brightness"; exit 1; }
# a phone's sources, scrolled: no row under the recommendation, every chip right of the addon's name
run phone_streams_scrolled 360 "wait,wait,tap:152:1050,wait,wait,tap:390:666,wait,wait,wait,drag:390:1500:390:1000,wait" $PH SNTV_EXPECT_SCREEN=streams HOME="$(home_with ph22 '{"setupDone":1,"prefSubs":"eng","subsAlways":1}')"
# Settings > About > Storage: what the app keeps here, and OK clears the cache
run storage_clear 700 "wait,tap:702:1660,wait,tap:300:1430,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,tap:300:905,wait,wait,wait" $PH SNTV_TIDY_DELAY=0 HOME="$(home_with st1 '{"setupDone":1}')"
grep -q "storage: [0-9]* MB here" "$OUT/storage_clear.log" && grep -q "tidy: cache cleared" "$OUT/storage_clear.log" || { echo "Storage did not count or clear the cache"; grep -E "storage|tidy" "$OUT/storage_clear.log"; exit 1; }
# a TV low on storage: the cache clears by itself and says so once
run storage_low 300 "wait,wait,wait,wait,wait,wait,wait,wait" SNTV_TIDY_DELAY=0 SNTV_FAKE_LOW_DISK=1 SNTV_AUTO_CLEAR_MIN_MB=0 HOME="$(home_with stlow '{"setupDone":1}')"
grep -q "storage: only .* MB free on the TV; clearing the cache" "$OUT/storage_low.log" && grep -q "toast: The TV was low on storage" "$OUT/storage_low.log" || { echo "a TV low on storage did not get its cache cleared"; grep -E "storage|tidy|toast" "$OUT/storage_low.log"; exit 1; }
# ... but a cache too small to make the room stays (it would go at every start), and the last
# session's log is never part of it (after a crash it is the only record)
STH="$(home_with stsmall '{"setupDone":1}')"; echo "12:00:00 E crash: SIGSEGV at 0x0" > "$STH/.local/share/stremio/nativetv/app.prev.log"
run storage_low_small 300 "wait,wait,wait,wait,wait,wait,wait,wait" SNTV_TIDY_DELAY=0 SNTV_FAKE_LOW_DISK=1 HOME="$STH"
grep -q "the cache (.* MB) is too small to make a difference, so it stays" "$OUT/storage_low_small.log" && ! grep -q "clearing the cache" "$OUT/storage_low_small.log" \
  && grep -q "E crash: SIGSEGV" "$STH/.local/share/stremio/nativetv/app.prev.log" \
  || { echo "a small cache was cleared on a TV low on storage"; grep -E "storage|tidy" "$OUT/storage_low_small.log"; exit 1; }
run settings_storage 420 "left,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,right,down,down,down,down,down" SNTV_TIDY_DELAY=0
# an episode's guest stars lead the cast drawer
run player_guests 480 "right,ok,down,down,right,play,wait,wait,wait,wait,wait,wait,info,wait" SNTV_TMDB_API=http://127.0.0.1:$PORT/tmdb/3 SNTV_TMDB_IMG=http://127.0.0.1:$PORT/tmdbimg HOME="$(home_with pguest '{"prefSubs":"eng","subsAlways":1,"setupDone":1,"tmdbKey":"0123456789abcdef0123456789abcdef"}')"
grep -q "tmdb: S[0-9]*E[0-9]* of [0-9]* has 2 guest stars" "$OUT/player_guests.log" && grep -q "player: cast drawer (6 people)" "$OUT/player_guests.log" || { echo "the episode's guest stars did not join the cast"; grep -E "tmdb|cast" "$OUT/player_guests.log"; exit 1; }
# Home's Library row ends in a See all card
SEEALL="$(home_with seeall '{"prefSubs":"eng","setupDone":1}')"; cp "$ROOT/tests/fixtures/library_six.json" "$SEEALL/.local/share/stremio/nativetv/library.json"
run home_see_all 300 "wait,down,right,right,right,right,right,right,right,right,right,right,ok,wait" HOME="$SEEALL" SNTV_EXPECT_SCREEN=library
# a long season: chips of ten beside the season tabs jump the episodes there
run detail_ep_ranges 300 "right,ok,down,right,right" SNTV_DEFAULT_ADDONS=http://127.0.0.1:$PORT/longseason/manifest.json HOME="$(home_with eprange '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "detail: episodes from 21" "$OUT/detail_ep_ranges.log" || { echo "the episode range chips did not jump"; grep detail "$OUT/detail_ep_ranges.log"; exit 1; }
# your own rating: Rate on the title page opens the stars at 4; Right adds half a star, a tag, Save
run rate_sheet 500 "right,right,ok,wait,right,right,right,right,right,right,ok,wait,right,down,right,ok" HOME="$(home_with rate_sheet '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
run rate_title 600 "right,right,ok,wait,right,right,right,right,right,right,ok,wait,right,down,right,ok,down,ok,wait" HOME="$(home_with rate_title '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "ratings: tt[0-9]* rated 9/10" "$OUT/rate_title.log" && grep -q "ratings: tt[0-9]* tagged Great ending" "$OUT/rate_title.log" \
  || { echo "the title's rating or its tag was not saved"; grep ratings "$OUT/rate_title.log"; exit 1; }
# Back leaves it unrated
run rate_back 500 "right,right,ok,wait,right,right,right,right,right,right,ok,wait,right,back,wait" HOME="$(home_with rate_back '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
! grep -q "ratings: tt[0-9]* rated" "$OUT/rate_back.log" || { echo "Back saved a rating"; exit 1; }
# an episode, from the hold-OK sheet: Rate this episode, two half stars fewer (the waits: its episodes in first, on a busy machine)
run rate_episode 860 "right,ok,wait,wait,wait,down,down,right,hold,$HOLD,release,down,down,down,down,down,down,down,down,ok,wait,left,left,ok,wait" HOME="$(home_with rate_ep '{"prefSubs":"eng","subsAlways":1,"setupDone":1}')"
grep -q "ratings: tt[0-9]*:[0-9]*:[0-9]* rated 6/10" "$OUT/rate_episode.log" || { echo "the episode rating was not saved"; grep ratings "$OUT/rate_episode.log"; exit 1; }
run rate_phone 300 "right,right,ok,wait" $PH
# the soundtrack on the title page: the two albums of it (not a band or a 1979 film's of the same name), all the tracks on OK
MUS="SNTV_ITUNES_API=http://127.0.0.1:$PORT/itunes"
run music_row 500 "right,ok,wait,wait,wait,down,down,down,down,down,down" "$MUS"
grep -q "music: 2 soundtrack albums for tt0903747" "$OUT/music_row.log" || { echo "the soundtrack albums were not found"; grep music "$OUT/music_row.log"; exit 1; }
run music_album 560 "right,ok,wait,wait,wait,down,down,down,down,down,down,right,ok,wait" "$MUS"
grep -q "music: album Breaking Bad: Music from the Original TV Series (12 tracks)" "$OUT/music_album.log" || { echo "the album did not open"; grep music "$OUT/music_album.log"; exit 1; }
# sent to Trakt and SimKL when connected: an episode to Trakt only (SimKL rates films and shows)
: > "$FAKE_TRACK_LOG"
run rate_synced 860 "right,ok,wait,wait,wait,down,down,right,hold,$HOLD,release,down,down,down,down,down,down,down,down,ok,wait,right,right,ok,wait,wait" HOME="$(home_with rate_sync "$TK2")" SNTV_TRAKT_API=http://127.0.0.1:$PORT/trakt SNTV_SIMKL_API=http://127.0.0.1:$PORT/simkl
grep -q 'trakt /sync/ratings {"shows":\[{"ids":{"imdb":"tt[0-9]*"},"seasons":\[{"number":[0-9]*,"episodes":\[{"number":[0-9]*,"rating":10}' "$FAKE_TRACK_LOG" && grep -q "trakt: rating saved" "$OUT/rate_synced.log" && ! grep -q "^simkl /sync/ratings" "$FAKE_TRACK_LOG" \
  || { echo "the episode rating did not reach Trakt alone"; cat "$FAKE_TRACK_LOG"; grep -E "ratings|trakt|simkl" "$OUT/rate_synced.log"; exit 1; }
# the loading screen: the backdrop as a wash, the format, the logo, the ring, the episode and what is happening
run veil_series 420 "right,ok,down,down,play,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait" SNTV_FAKE_STUCK=1
python3 -I - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
print("screenshots:", sorted(os.path.basename(x) for x in glob.glob(os.path.join(sys.argv[1], "*.png"))))
PY
