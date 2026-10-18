# Changelog

## 0.4.4

- **Instant Up Next.** When the credits start (or 90 s before the end), the next
  episode's sources are fetched in the background, so the Up Next card and *Play now*
  start it straight away instead of showing the spinner first.
- **Screensaver.** After a quiet spell (5 minutes by default; 2/5/10/20 or off in
  Settings > Interface) backdrops from your library and the Home catalogs drift slowly
  with a large clock. It never covers a playing video, only a paused one, and shifts
  everything a few pixels every minute so OLED panels keep no static image. Any key
  closes it.
- **Stats for nerds.** Red in the player: source and host, codec, resolution, frame rate
  and bitrate as the TV reports them, buffer level with stall count and time, subtitle
  and audio state, skip markers, and the app's own frame time.

## 0.4.3

Ideas taken from Stremio-Kai that work on a TV.

- **Track memory per series.** Pick an audio language or subtitles in the player and the
  next episodes of that series start with the same choice (including subtitles off).
- **Subtitles only for other languages** (new default, Settings > *Turn subtitles on*):
  something already in your subtitle language plays without subtitles; anime and foreign
  films still get them. *Always* keeps the old behaviour.
- Language codes match across forms (`en`, `eng`, `English`, `ger`/`deu`), and the track
  menu shows language names. Urdu added to the language list.
- **Clock and "Ends at"** in the player overlay; 12-hour, 24-hour or off in Settings.
- **First-run setup**: one question per screen for languages, subtitles, intro skipping,
  autoplay, background and sign-in. Back on the first step skips it; Settings > *Run quick
  setup again* brings it back.
- **Pure black (OLED) background** option.

## 0.4.2

- **Skip intro and recap.** Markers come from TheIntroDB (movies and episodes with IMDb
  ids) and AniSkip (anime with Kitsu or MyAnimeList ids). Both are free and need no key.
  While an intro or recap plays, a *Skip intro* button appears; OK jumps past it.
  Settings > *Skip intros and recaps* chooses the button, automatic skipping, or off.
- **Up Next at the credits.** When the credits start time is known, the Up Next card
  appears there and the next episode starts 20 s later (with *Play next episode
  automatically* on). Without markers it still appears 45 s before the end. Back now
  hides the card and cancels the countdown.
- The intro and credits ranges are drawn under the seek bar.
- Answers for a different episode than the one playing are ignored, and AniSkip
  timings marked on a cut more than 10% longer or shorter are dropped.

## 0.4.1

- New **Diagnostics** screen (Settings > Diagnostics, or OK on the playback error card):
  the app's log on the TV, filterable to playback lines or problems, plus the previous
  session's log. OK turns the selected lines and the device details (version, video
  backend, webOS release, root or not) into a QR code you can scan with a phone, so
  problems can be reported from a non-rooted TV.
- Logging keeps the last 600 lines in memory for that screen.

## 0.4.0

First installable LG build.

- Releases now ship a ready-made LG package plus a Homebrew Channel repository.
  Add `https://github.com/smalik29871/stremio-native-tv/releases/latest/download/repo.json`
  in the Homebrew Channel and install from the TV; updates arrive the same way.
- GitHub Actions builds and checks the package on every push with the openlgtv
  webOS toolchain: ARM binary, only SDL2/GLESv2/libc linked, nothing newer than
  glibc 2.7 (loads on webOS 3), required files present, no credentials inside.
- webOS playback rewritten around what works on real TVs (as documented by the
  Nuvio native client):
  - registers on the media bus as `com.webos.media.client.<appId>` (other names
    are refused), with a process-id fallback after an unclean exit
  - LS2, glib, ACB and the SDL webOS functions are loaded at run time, so one
    binary covers webOS 3 to current firmware
  - load payload in the shape the TV accepts, with the real exported window on
    webOS 5+
  - the exported window always gets a real source rectangle (a NULL one closes
    the app on some newer TVs)
  - webOS 3-4: the video plane is bound through ACB after every load with the
    correct player, sink and state values; the old values would have sent video
    to the picture-in-picture plane
  - position, duration, buffering, video size and tracks parsed from the event
    shapes the TV actually sends
  - "audio not supported" (error 200) is shown as a notice; playback continues
    instead of switching source
- `appinfo.json` requests `requiredPermissions: ["all"]`, like other native apps.

## 0.3.0

Look and feel
- New renderer: one batched vertex stream with signed-distance shapes in the
  vertex format, so rounded cards, soft shadows, rings, capsule icons and text all
  share draw calls (a typical Home frame is a handful of calls instead of
  thousands). Anti-aliased at any panel resolution.
- Inter (Medium + Bold) replaces DejaVu; proper ellipsis, bullets and quotes.
- Ambient light: every screen is lit by the dominant colour of the focused
  artwork (computed on the decode thread), easing as you move.
- Home: full-bleed cinematic hero with logo or title, IMDb badge, meta, genre
  chips and synopsis; rounded cards that grow and lift on focus with a caption;
  landscape Continue Watching cards with progress; new My Library row; shimmer
  skeletons while catalogs load; cross-faded backdrops with seamless fades.
- Detail: the same cinematic header, pill actions with resume progress, season
  tabs and a horizontal episode rail with thumbnails, watched badges, progress
  and spoiler blur.
- Streams: frosted blurred backdrop, poster column, and parsed quality badges
  (4K/1080p, Dolby Vision/HDR, HEVC/AV1, Atmos/DTS/5.1, size, debrid cached).
- Player: gradient progress bar with glowing knob and seek-time bubble, large
  play indicator when paused, frosted track panel with check marks, Up Next card
  with the next episode's thumbnail and countdown, outlined captions.
- Search, Settings (icons, toggle switches, value choosers) and Sign-in (glowing
  QR card, code tiles, numbered steps) redesigned to match.
- Toasts slide in as pills; the on-screen keyboard has rounded keys.

Fixes
- Preferred subtitle language now waits for subtitle addons instead of giving up
  before they answer.
- Plain and mipmapped variants of the same image no longer collide in the cache.

## 0.2.0

Playback
- Audio and subtitle track menu (Up / Down in the player) backed by the pipeline's
  track lists on webOS (`sourceInfo` / `selectTrack`), Tizen (`player_select_track`)
  and the HTML5 element on the WebAssembly build.
- Addon subtitles: every subtitle addon is queried with `filename` / `videoSize`
  hints, SRT and WebVTT are parsed (tags stripped, CP-1252 recovered) and drawn
  with a timing offset you adjust with Left / Right inside the menu.
- Preferred audio and subtitle languages auto-select once tracks are known.
- Automatic fallback: the stream picker hands the player an ordered list of
  playable sources (same release group first); a pipeline error or a 45 s stall
  moves to the next one, Yellow switches manually.
- Up Next at the credits with autoplay on the same release group; Green plays it now.
- Seek requests coalesce while the key repeats and show only the bar when the
  controls are hidden; buffering percentage and "ends in" readouts.
- webOS backend rewritten against the real webosbrew SDL API
  (`SDL_webOSCreateExportedWindow(VIDEO)`, panel resolution), two load payload
  variants, `bufferingStart/End`, `trackSelected`, a 40 s load watchdog, and
  app background/foreground handling.
- Correct webOS remote scancodes (Back = 482, Exit = 505, colours 486-489).

Snappier
- libcurl handle per worker: keep-alive across posters (the TV's old curl needs it).
- Disk cache for artwork (96 MB, LRU) and for catalog JSON, so Home opens from
  cache at once and refreshes in the background.
- Poster prefetch around the focus and for the next row; fade-in on arrival;
  cross-faded hero backdrop; eased row / list scrolling; focused poster grows.
- Idle frame skipping: nothing animating means no redraws at all.
- Visual effects Automatic / Full / Light: Automatic measures the first seconds
  and drops to Light when the TV cannot hold ~40 fps.
- Artwork budget follows the TV's RAM (40 / 96 / 192 MB).

Also
- QR / short-code sign-in through link.stremio.com (password keyboard kept as fallback).
- Episode thumbnails with spoiler blur for unwatched episodes (mipmap LOD bias, no shader cost).
- Settings: preferred languages, subtitle size, autoplay next, effects level, blur.

## 0.1.0

Initial scaffold: addon protocol, account API, Home / Detail / Streams / Player /
Search / Settings / Sign-in, desktop preview, webOS and Tizen packaging.
