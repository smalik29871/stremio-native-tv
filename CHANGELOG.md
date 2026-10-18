# Changelog

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
