# Cuelight

*Your cue to press play.* Cuelight is a fast native player for LG webOS TVs (3.0 and newer)
that works with your **Stremio account and addons**, installed from the
[Homebrew Channel](https://github.com/webosbrew/webos-homebrew-channel) without root.
Samsung Tizen builds (native `.tpk`, WebAssembly `.wgt`) are in the tree but have not been
run on a Samsung set yet.

It is written in C99 on SDL2 + OpenGL ES 2 and hands playback to the TV's own video
pipeline, so it starts in a second, scrolls at full frame rate on 2016 hardware and plays
whatever the TV can decode. It speaks the **Stremio addon protocol** and the **Stremio
account API**: the same account, addons, Library and Continue Watching as the official apps.

> Cuelight is an independent project. It is **not affiliated with, endorsed by or made by
> Stremio**; "Stremio" is used only to say which accounts and addons it works with.

> **v0.10.60.** Confirmed on an LG QNED: installs from the Homebrew Channel, signs in,
> browses, plays debrid HTTPS streams with the control overlay, picture-in-picture resizes
> the video plane. Every release is cross-compiled in CI with the openlgtv toolchain, its
> package checked (ARM, dependencies, glibc 2.7), and the UI is run headlessly through
> 300 scripted scenarios against a fake addon. See *What is verified* below.

## Install on an LG TV

1. Open **Homebrew Channel** on the TV, then **Settings**.
2. Add this repository URL:

   ```
   https://github.com/smalik29871/cuelight-releases/releases/latest/download/repo.json
   ```
3. Back in the app list, install **Cuelight** (listed as "Stremio (native)" before 0.10.57).

**Updates, when you say so.** The app checks this repository at start and every six
hours. When a new release is out it waits until nothing is playing and asks: *Update now*
or *Later* (it asks again at the next start). Update now has the Homebrew Channel's own
installer (`org.webosbrew.hbchannel.service/install`) download the package, check its
SHA-256 and install it. webOS closes the app while it installs; open it again afterwards.
Settings > About has *Update* (check or install now) and *Ask me when an update is out*.
The release files must stay publicly downloadable for this, as for the Homebrew Channel:
a private repository's release files need a GitHub login that a TV does not have.

Or download the `.ipk` from the [latest release](https://github.com/smalik29871/cuelight-releases/releases/latest)
and install it with Developer Mode (`ares-install`) or Dev Manager.

First start asks six questions (languages, when subtitles turn on, intro skipping,
autoplay, background, sign-in by QR code). Everything can be changed later in Settings.

## Screenshots

Captured from the desktop build against the test addon, so the artwork is generated and
the titles are placeholders. On a TV the player shows video where these show a backdrop.

### Themes

Eight looks, each with its own type, colours and way of showing focus (Settings > Interface >
Theme; most also take an accent colour and a background).

| | |
|---|---|
| ![Streaming](docs/screenshots/theme_stream.jpg) **Streaming** (the default): familiar and calm | ![Aperture](docs/screenshots/theme_aperture.jpg) **Aperture**: editorial, serif titles and wide cards |
| ![Signal](docs/screenshots/theme_signal.jpg) **Signal**: numbered rows and a bright focus ring | ![Marquee](docs/screenshots/theme_marquee.jpg) **Marquee**: cinema gold and uppercase rows |
| ![Glass](docs/screenshots/theme_glass.jpg) **Glass**: frosted, rounded panels | ![Metro](docs/screenshots/theme_metro.jpg) **Metro**: flat tiles, edge to edge |
| ![Mist](docs/screenshots/theme_mist.jpg) **Mist**: cool and soft | ![Sunset](docs/screenshots/theme_sunset.jpg) **Sunset**: warm dusk tones |

### Screens

These show the Signal theme.

| | |
|---|---|
| ![Settings](docs/screenshots/settings.jpg) **Settings, with a live preview of each setting** | ![Showcase](docs/screenshots/home_showcase.jpg) **Home, Showcase style** |
| ![Home](docs/screenshots/home.jpg) **Home, Cinematic style** | |
| ![Explore](docs/screenshots/explore.jpg) **Explore: catalogs, genres, endless grid** | ![Library](docs/screenshots/library.jpg) **Library with filters and sort** |
| ![Detail](docs/screenshots/detail_tmdb.jpg) **Title page with cast and age rating** | ![Episodes](docs/screenshots/episodes.jpg) **Seasons and episodes** |
| ![Ask AI](docs/screenshots/detail_ai.jpg) **Ask AI: spoiler-free, with a parents' note** | ![AI search](docs/screenshots/search_ai.jpg) **Search by describing what you want** |
| ![Sources](docs/screenshots/streams_best.jpg) **Sources, with a recommended card** | ![Player](docs/screenshots/player_bar.jpg) **Player controls** |
| ![Paused](docs/screenshots/player_paused_tmdb.jpg) **Paused: what you are watching** | ![Subtitles](docs/screenshots/player_autosync.jpg) **Subtitles: matched files first, auto-synced** |
| ![Translate](docs/screenshots/player_translate.jpg) **Subtitles translated by AI** | ![Stats](docs/screenshots/player_stats.jpg) **Stats for nerds** |
| ![Break](docs/screenshots/break_screen.jpg) **Take breaks: a calm pause** | ![Calendar](docs/screenshots/calendar.jpg) **Calendar of your series** |
| ![Phone](docs/screenshots/settings_phone.jpg) **Type keys and URLs from your phone** | |
| ![PIN](docs/screenshots/pin_pad.jpg) **Parental PIN** | ![QR sign-in](docs/screenshots/login_qr.jpg) **Sign in with a QR code** |
| ![Screensaver](docs/screenshots/ambient.jpg) **Screensaver** | ![Diagnostics](docs/screenshots/diagnostics_qr.jpg) **Diagnostics: the log as a QR code** |

## What it does

### Look
- **Aperture prototype**: an optional charcoal/ivory editorial theme with artwork
  windows, restrained coral focus markers, quiet playback controls, pause title
  cards, a cached seek filmstrip, and subtitle/audio drawers. Select it in
  Settings > Interface > Theme. See [the native prototype guide](docs/APERTURE.md).
- **Four themes**, switched in Settings > Interface, six accent colours each:
  **Signal** (default: graphite, lime, sharp corners, a solid accent frame for focus,
  capital titles, mono metadata, numbered rows), **Marquee** (warm ink and parchment,
  amber, condensed display titles, a light bar under the focused card), **Glass**
  (frosted panels over the artwork, a white focus glow) and **Metro** (flat colour
  tiles, big light lowercase headings, a white tile border for focus). Glass blurs by
  sampling a small mip of artwork already loaded, so it costs one textured quad. The
  Stremio logo keeps its own colours in every theme. See `docs/OVERHAUL.md`.
- **Clock island**: the clock in the corner grows into notices, progress and a *Resume*
  card for the title you just left (Blue opens it), then shrinks back to the time.
- **Motion**: titles open out of the card you chose and shrink back into it on Back;
  rows scroll on a spring; cards dip on OK, nudge at the end of a row and rise in one
  after another when a row arrives. All of it is clipping, scale and fade per vertex:
  no offscreen buffers, and Light effects turns it off.
- **Settings with previews**: rows grouped under headings with a line of help each, and
  beside them a drawing of what the focused setting changes, redrawn as you change it.
- **Details on focus**: rest on a poster and a panel slides open beside it with the
  rating, year, runtime, genres, synopsis and your Library state (can be turned off).

### Browse
- **Side rail** (press Left): Home, Explore, Search, Library, Calendar, Settings, account.
- **Three Home styles**: Cinematic (the focused title becomes the backdrop), Showcase
  (featured carousel) and Compact (rows from the top). Rows can be reordered and hidden,
  watched titles can be hidden, and the addon order syncs with the account.
- **Explore** any catalog by type, list and genre with an endless grid; **Library** with
  filters and sort; **Calendar** of upcoming and recent episodes of the series you follow.
- **Search** across every catalog addon that supports it, debounced as you type. The
  keyboard opens when you press OK on the search bar, with a QR code to type on your phone.
- **Title pages** with logo, IMDb rating, genres, synopsis, cast (photos from the meta
  addon or TMDB), age rating, resume with progress, seasons and episode thumbnails, and
  spoiler blur on episodes you have not seen.
- **Cache-first**: Home opens instantly from the last visit while rows refresh.

### Play
- **The TV's own decoder** (`com.webos.media`), with the video plane resized live for
  **picture-in-picture** (off by default; Settings > Playback) and **aspect ratio** (Fit,
  Fill, Zoom).
- **Control bar**: Previous/Next episode, Play/Pause, Reload, Subtitles, Audio and speed
  (0.5x to 2x), Aspect, Sources, Episodes by season, Stats. Paused for a moment, the
  player shows the synopsis and cast of what is playing.
- **Sources** from every stream addon, scored by name (resolution, codec, HDR/DV, audio,
  size, seeders, cached) against a profile of this TV that learns from what played and
  what failed. The first 2 MB of the best candidates are fetched and their container
  header read, so the recommendation says what was verified ("HEVC 2160p, DD+ 5.1,
  chapters") and dead links or files faster than your line are dropped.
- **Automatic fallback** to the next source when playback fails or stalls; **Up Next**
  with autoplay, fetched while the credits roll; **chapters** on the timeline from the
  file header; -10 s / +10 s feedback while seeking; **seek previews** decoded
  on the TV (below).
- **Skip intro / recap** and credits-based Up Next from TheIntroDB and AniSkip (free,
  keyless): a button, automatic, or off.
- **Track memory**: audio and subtitle choices are remembered per series; subtitles can
  turn on only when the audio is in another language; forced subtitles in the audio's
  language are picked from the file header.
- **Stats for nerds** (Red): codec, resolution, fps, bitrate, buffer, stalls, file hash.
- **Screensaver** with drifting backdrops and a clock after a quiet spell, OLED-safe.

### Subtitles
- Addon subtitles (SRT, VTT) drawn by the app with four **styles**, four sizes and three
  positions, changed live from the player's Subtitles menu or Settings; embedded tracks
  drawn by the TV.
- **Every file per language** is listed, grouped by addon, with a **second subtitle
  language** drawn above the first for learners.
- **Matched to the file**: the player fingerprints the video the way OpenSubtitles does
  (size plus the first and last 64 KB) and asks the addons for files made for that exact
  release. They come first, marked *matched to this file*.
- **Auto-sync**: pick any other file and its cue timings are compared with a matched one;
  the offset is applied by itself ("Subtitles synced to the file, +1.3 s"). Manual
  timing in 0.1 s steps sits at the top of the Subtitles menu.
- **Translate** (with the AI assistant, below) when nobody offers your language: the
  English file is translated in batches around the playhead, so it starts in seconds and
  keeps going as you watch.

### AI assistant (optional, your own free key)
Settings > Accounts > **AI assistant** takes a key from **Groq**, **OpenRouter** or
**Google Gemini** (all have free tiers; the screen says where to get one), or any
OpenAI-compatible endpoint such as an **Ollama** box on your network (no key). Nothing is
sent anywhere until you turn it on. It adds:
- **Ask AI** on a title page: what it is in a nutshell, who will enjoy it, what a parent
  or sensitive viewer should know, three similar titles. Spoiler-free, cached for a month.
- **Describe what you want** in Search ("something funny with robots"): the model names
  titles, your addons find them, and they are listed first with a star.
- **Subtitle translation** into your preferred language, as above.

### Take breaks
Settings > **Take breaks** schedules pauses: **prayer times** computed on the TV from
your location (MWL, ISNA, Egypt, Umm al-Qura, Karachi, Tehran, Jafari methods, Hanafi
Asr; Angelus, Nitnem, Sandhya, zmanim and Baha'i windows as well), **stretch** reminders,
**bedtime**, or **your own times**. A banner warns a few minutes ahead, then the video
pauses on a calm page with a chime or your own recording (an adhan, a dua, a bell: sent
from your phone to the TV, a direct link, or a YouTube link the TV saves once and then
plays offline, shown full screen if it is a video), and playback continues where it stopped. Research and sources are in `docs/BREAKS.md`.

### Everyday conveniences
- **Hold OK** (or press Yellow, Menu or Info) on any title: mark watched or not watched,
  remove from Continue Watching, add to or remove from the Library.
- **From the start** next to Resume, **recent searches**, a **No internet** pill, a notice
  after a crash, and a note when a newer version is in the Homebrew Channel.
- **Sources** in your audio language first, a cap on how long autoplay waits for slow
  addons, failed links sorted last for six hours, and addon notice rows never autoplayed.

### Keys, phones and safety
- **Type it on your phone**: every text field shows a QR code; the TV serves a small page
  on your network where you paste the key, URL or password, and it lands in the field.
- **Accounts** (one place for every account and key): TMDB (the built-in catalog, cast photos and age ratings; a key baked into CI builds from the
  `TMDB_API_KEY` repository secret is used when you have none), Stremio sync, Trakt, SimKL,
  TorBox, OpenSubtitles, MDBList and the AI assistant.
- **Saved sign-ins** you can switch between, and a **parental PIN** that can guard
  Settings and adult addons.
- **Sign in by QR** or short code via link.stremio.com; e-mail and password as fallback.

### Built for old TVs
- Idle frame skipping, a batched GLES2 renderer (one draw call for most screens), disk
  caches for artwork and catalogs, poster prefetch, eased scrolling and cross-fades.
- **Nothing heavy in a frame**: catalog and source replies are parsed and cached on the
  network threads, handed to the screen within an 8 ms budget per frame, and anything
  slower is logged with its host. Idle, the app wakes the instant a key arrives.
- An **Automatic effects level** measures the TV and drops to Light when it cannot keep up.
- A **memory saver** that watches the TV: on a small set (under 2 GB) it is on from the
  start; on any set it turns itself on when free memory gets tight (fewer rows and
  posters kept, artwork budget cut, posters decoded at half size), and off again once
  there is room. While a video fills the screen on such a TV the artwork cache shrinks
  further so the decoder has room, and freed memory is handed back to the system. It
  also answers the system's low-memory warning. Settings > Interface shows why it is on.
- **Shared connections**: the network workers share DNS answers and TLS sessions, so a
  server one of them has reached is resumed by the others without a full handshake.
- **libcurl via `dlopen`**, because LG ships `libcurl.so.5` and the SDK ships `.so.4`.
- **Diagnostics** on the TV itself: the app's log by tab, the previous session's log
  after a crash, and a QR code that carries the lines to your phone.
- **A data folder that stays small** (`src/tidy.c`): reply caches by age and size,
  unused recordings, crash leftovers and old font atlases are cleared on a background
  thread after start; the artwork cache trims itself at 96 MB; the log restarts at 4 MB.

### Where ideas came from
Stremio-Kai: per-series track memory, "subtitles only for other languages", the player
clock and end time, the first-run setup, the OLED theme. The Nuvio port
([nuvio-native-legacy](https://github.com/iqui27/nuvio-native-legacy)): cache-first
Home, Automatic/Full/Light effects, spoiler blur, QR sign-in, skip intro and credits
Up Next, the exported-window / ACB split, dlopen'd curl, the log file. Left out on
purpose for stability: FFmpeg transcoding, libass, trailers, the Samsung .NET host, Trakt/Simkl/debrid integrations
(those are Stremio addons) and a plugin engine. See `docs/ROADMAP.md`.

## Settings at a glance

| Category | What is there |
|---|---|
| Accounts | Stremio sync and saved accounts, Trakt, SimKL, TorBox, streaming server, OpenSubtitles, TMDB, MDBList, AI assistant |
| Playback | Autoplay, skip intro, picture-in-picture, player clock, scrubbing, optional home helper |
| Take breaks | Schedules, location, method, lead time, length, sound, stretch, bedtime, custom times |
| Audio & subtitles | Preferred languages, when subtitles turn on, auto-sync, size, style |
| Sources | Quality preference, resolution cap, pre-flight check, automatic recommendation |
| Interface | Home style, hide watched, effects, page transitions, OLED background, screensaver, blur, memory use |
| Home rows | Order and visibility of rows |
| Addons | Install from URL, remove, reorder (synced to the account) |
| Parental | PIN, PIN for Settings, hide adult addons |
| About | Version, backend, memory, Diagnostics, run setup again |

## Player keys

On Home, Explore, Library, Search and title pages, **hold OK** for the options of the
focused title. In the player, Down opens the control bar (Left/Right to move, OK to open, Up back to the timeline).
OK play/pause (while *Skip intro* is on screen, OK skips). Left/Right seek 10 s and
accelerate while held; the video follows live. Rewind/Forward jump between chapters.
Up audio menu, Down subtitle menu (Left/Right inside it shifts timing by 0.5 s). Red stats
for nerds, Green play Up Next now, Yellow next source. Back hides the Up Next card, else
stops and saves progress (or parks the video in a corner when picture-in-picture is on;
Play/Pause brings it back, Stop closes it).

## Torrents, servers and previews

- **Torrents are the fallback, played on the TV.** Debrid addons (TorBox, Real-Debrid...)
  return plain HTTPS links and come first. A source that is only a torrent (`infoHash`)
  plays through a small engine bundled with the app (`tools/torrentd`, Go, built on
  anacrolix/torrent): a separate process on `127.0.0.1:11490` that speaks the Stremio
  streaming server's URLs, keeps pieces in a capped RAM buffer (96 MB on small TVs, 192 MB
  otherwise; nothing is written to the TV's storage), caps uploads at 256 KB/s and stops
  when nothing plays. P2P sources rank after every cached or direct link and before
  uncached debrid ones; when TorBox fails, the player reaches the P2P source after two
  tries. While P2P plays, the player asks the addons every two minutes and switches to a
  cached copy at least as sharp as soon as one appears, at the same position. Settings >
  Sources turns it off; a **Streaming server** URL (Settings > Account) replaces it.
- **YouTube links** for break recordings are handled on the TV, the way yt-dlp does it:
  with a visitor id from the video's page, it asks as YouTube's visionOS app (no player
  script, no proof-of-origin token), then as YouTube's TV app or embedded player, whose
  URL challenges are solved by yt-dlp's solver (`assets/yt`) in the bundled QuickJS
  engine. It downloads the audio (or a 360p video) once. The first link takes up to a
  minute while the player script is prepared; later ones only download. When YouTube
  changes and the bundled solver stops working, the TV fetches the newest solver from
  yt-dlp's releases on GitHub and tries again; it runs sandboxed, with no file or network
  access. NewPipe does the same job on Android with its own extractor.
- **Seek previews** are made on the TV. The video plane cannot be read, so the app reads
  the file's keyframe index (Matroska Cues, MP4 sample tables), downloads only the
  keyframe at or before the seek position (one range request, ~50-500 KB), decodes it
  with a bundled FFmpeg cut down to its H.264 and HEVC (8/10-bit) decoders, and scales
  it to a small picture (`src/frames.c`). Fragmented MP4 and files without an index get
  no preview.
- **The home helper** (`tools/thumbs/server.py`, ffmpeg and yt-dlp, Dockerfile included)
  is optional. When its URL is set (Settings > Playback) previews come from it, and it is
  the fallback for YouTube when YouTube refuses the TV, and for other page links.

## Diagnostics (reporting a problem)

Settings > About > **Diagnostics** shows the app's own log on the TV; no root, SSH or
developer mode is needed. It opens by itself when you press OK on the "This source can't
be played" card. Left/Right switches *Playback*, *Problems* and *Everything*; Up/Down moves
the cursor; Blue switches to the **previous session**'s log (after a crash or a black
screen); OK shows a QR code with the version, backend, webOS release and the lines up to
the cursor, to scan with a phone and paste into an issue.

If the interface works but video is black, the app's data folder has `app.log` with every
reply from the media service.

## Building

### Desktop preview (Linux, no video)
```
sudo apt install cmake build-essential libsdl2-dev libgles-dev nasm
tools/linux.sh            # builds FFmpeg's two decoders once, then the app; runs unit tests
./build/stremio-tv        # arrows / Enter / Esc, F3 = search, F1/F2/F5/F4 = red/green/yellow/blue
```
Headless UI check (needs Xvfb + Pillow): `bash tests/smoke.sh` writes screenshots to `tests/out/`.
Useful hooks: `SNTV_KEYS`, `SNTV_SCREENSHOT`, `SNTV_FAKE_DURATION/START`, `SNTV_NOW`,
`SNTV_FAKE_MEM`, `SNTV_TMDB_API`, `SNTV_AI_API`, `SNTV_NO_TRANSITIONS`.

### LG webOS (.ipk)
1. Get `arm-webos-linux-gnueabi_sdk-buildroot-x86_64.tar.gz` from
   [openlgtv/buildroot-nc4 releases](https://github.com/openlgtv/buildroot-nc4/releases)
   (macOS: [webosbrew/native-toolchain](https://github.com/webosbrew/native-toolchain)),
   extract it and run `relocate-sdk.sh`. It ships SDL2 (the webOS fork), GLES2, luna-service2.
2. `WEBOS_TOOLCHAIN=/path/to/arm-webos-linux-gnueabi_sdk-buildroot tools/webos.sh --ipk`
3. `tools/check-ipk.sh build-webos/*.ipk $WEBOS_TOOLCHAIN` verifies the package the way CI does.
4. Install with the Homebrew Channel / Dev Manager, or `TV_IP=... tools/webos.sh --install` on a rooted TV.

`tools/package-ipk.sh` uses `ares-package` when present and otherwise writes the
`ar` + tar layout itself, and refuses to package anything that looks like a credential file.
`TMDB_API_KEY` in the environment bakes a built-in TMDB key into the binary.

### Samsung Tizen native (.tpk, Tizen 4.0-9)
Tizen Studio with the TV native rootstrap: `tools/tizen-native.sh`. Left unsigned on
purpose; sign with your certificate profile (`TIZEN_SIGN_PROFILE=...`). 2018-2020 sets
block native code in the app folder; the .NET host that works around it is not in this
repository yet.

### Samsung Tizen WebAssembly (.wgt, Tizen 5.5+)
Emscripten: `EMSDK=~/emsdk tools/tizen-wasm.sh`. Networking must go through the browser in
this build; a `fetch()`-based `http_wasm.c` is the first thing to add before the `.wgt` is useful.

## Releasing

Bump the version in `deploy/webos/appinfo.json`, `deploy/webos/packageinfo.json`,
`src/platform/platform.h` and the Tizen manifests, add a `## x.y.z` section to
`CHANGELOG.md`, and push to `main`. CI builds and checks the package and, because that
version has no release yet, publishes it with the Homebrew Channel files. TVs pick it up
from the channel.

## What is verified, and what is not

| Area | Status |
|---|---|
| Addon protocol and API parsing, link login, subtitles and alignment, skip markers, language matching, track memory, source scoring, container headers, prayer times, memory watch | Unit tests (`tests/test_*.c`; MKV/MP4 fixtures made with ffmpeg) |
| Every screen and flow: rail, three Home styles, Explore, Library, Calendar, detail, sources, player menus, subtitles and auto-sync, AI panels, breaks, PIN, phone entry, settings, diagnostics, screensaver | Smoke-tested headless under Xvfb against `tests/fake_addon.py` (300 scripted runs, screenshots in `tests/out/`) |
| webOS build | Cross-compiled in CI with the openlgtv toolchain; package checked (ARM, dependencies, glibc 2.7, contents) |
| webOS on a real TV | Confirmed on an LG QNED: install from the Homebrew Channel, sign-in, browsing, debrid HTTPS playback with the overlay, picture-in-picture |
| Live `api.strem.io`, Cinemeta, `link.stremio.com`, TMDB, AI providers | Not reachable from the build sandbox; shapes follow stremio-core and the public APIs, exercised against fakes |
| Tizen native and WebAssembly | Written against the platform APIs; not compiled with Tizen Studio or run on a Samsung TV |
| Remote scancodes | Match LG's `SDL_webOS.h` (Back 482, Exit 505, colours 486-489) |

## Layout

```
src/
  main.c               SDL/GL setup, main loop, dev hooks
  app.c/.h             app state: addons, library, auth, screen stack, progress sync, prefs, transitions
  http.c/.h            libcurl over dlopen + worker pool; callbacks on the main thread
  store.c/.h           settings.json + cached JSON in the app's writable dir
  image.c/.h           async artwork loader with an LRU byte budget
  memwatch.c/.h        runtime memory watch (saver on/off, low-memory warning)
  subs.c/.h            SRT / WebVTT parser, cue lookup, timing alignment
  probe.c/.h           Matroska / MP4 header reader (tracks, chapters, HDR, fps)
  quality.c/.h         source scoring from names and headers, TV profile
  tmdb.c/.h            TMDB details (cast, ratings) with cache
  ai.c/.h              OpenAI-compatible chat client (Groq, OpenRouter, Gemini, custom)
  suntimes.c/.h        solar/prayer time computation (PrayTimes methods)
  breaks.c/.h          break schedules, banner, location
  phone.c/.h           LAN page for typing from a phone
  skip.c/.h            TheIntroDB / AniSkip markers
  trackmem.c/.h        per-series track memory
  audio.c/.h           SDL chime
  addon/               Stremio addon protocol and account API
  ui/theme.c           runtime themes (Signal, Marquee, Glass, Metro): tokens, accents, fonts per theme
  ui/island.c          the clock island: clock, notices, progress, the resume card
  ui/settings_preview.c the drawings beside the Settings rows
  ui/gfx.c             batched GLES2 renderer: SDF rounded rects, rings, shadows, lines, per-vertex scale and a transition layer, video hole
  ui/font.c            stb_truetype atlases cached on disk, alternate face, UTF-8, ellipsis, wrap
  ui/widgets.c         icons, pills, chips, toggles, cards, skeletons, backdrops, ambient light, clock
  ui/rail.c            side navigation
  ui/screen_*.c        home, explore, library, calendar, detail, streams, player, search,
                       settings, login, welcome, pin, break, ambient, diagnostics
  video/               backend interface; null (desktop), webOS (com.webos.media), Tizen, wasm
deploy/                webOS appinfo + icons, Tizen manifests
tools/                 linux.sh, webos.sh, check-ipk.sh, package-ipk.sh, tizen-*.sh, thumbs/ (ffmpeg preview server)
tests/                 unit tests, fake_addon.py, smoke.sh, fixtures
docs/                  BREAKS.md (break schedules research), ROADMAP.md, PORTING-NOTES.md, screenshots/
vendor/                cJSON, qrcodegen, stb_truetype, stb_image, libwebp decoder, QuickJS-ng
```

## Configuration and data

Everything lives in the app's writable directory (`SDL_GetPrefPath`, falling back to
`$HOME/.org.stremio.nativetv`): `settings.json` (auth key, preferences, keys you entered,
resume points), cached JSON and artwork, `app.log` and `app.prev.log`. No credentials are
compiled into the binary except the optional TMDB key a CI secret provides.

## Third-party code

`vendor/libwebp` is libwebp 1.4.0's decoder (BSD-3-Clause, see `vendor/libwebp/COPYING`);
`vendor/cJSON` (MIT), `vendor/qrcodegen` (MIT), `vendor/quickjs` (QuickJS-ng 0.17, MIT),
FFmpeg 7.1.1 (LGPL 2.1; built from its source by `tools/ffmpeg.sh` with only the H.264 and
HEVC decoders, no GPL parts, and linked statically: this repository has everything needed
to rebuild the app against a changed FFmpeg),
the P2P engine's Go modules (anacrolix/torrent and its dependencies, MPL 2.0, MIT, BSD;
fetched by `go build` at the versions pinned in `tools/torrentd/go.sum`),
`stb_truetype` and `stb_image` (public domain) keep their own licences in place. The
YouTube solver in `assets/yt` is yt-dlp/ejs 0.8.0 (Unlicense; it bundles meriyah, ISC,
and astring, MIT, with their licences in the file header). The fonts in `assets/fonts` (Inter, Space
Grotesk, JetBrains Mono, Open Sans, Bebas Neue, DM Serif Display) are under the SIL Open Font License 1.1, each
with its licence file next to it. Prayer times use the published PrayTimes.org method
parameters, reimplemented in `src/suntimes.c` (see `docs/BREAKS.md`).
