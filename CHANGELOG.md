# Changelog

## 0.6.4

- **Your own adhan, dua or bell from your phone.** Settings > Take breaks > Recording at
  the start (or at the end) shows the phone QR code as before, and the phone page now
  has *Send a recording from this phone*: pick an MP3, M4A, OGG or WAV and it is stored
  on the TV (under 40 MB), served to the TV's own player from the TV, and plays at each
  break even without internet. Direct links still work. YouTube links are refused
  with an explanation: a native TV app cannot play YouTube pages, and resolving them
  from the TV is blocked or breaks weekly (the Nuvio developers found the same).
- **YouTube and other page links for the adhan or a dua.** Paste a YouTube (or
  SoundCloud, archive.org, Facebook...) link and the home helper (`tools/thumbs`, the
  same small server as seek previews) fetches it once with yt-dlp, the tool that does on
  a computer what NewPipe's extractor does on Android: audio as AAC, video as H.264 + AAC
  up to 720p, which every TV decodes. It is fetched the moment you save it, so the break
  starts instantly. *Show the video* plays it full screen with the break's countdown in a
  band below. The Docker image refreshes yt-dlp at every start.
- **Subtitles when nobody has your language.** When no addon or embedded track offers
  your preferred language, the player says so once and points to *Translate with AI*;
  with the new switch in Settings > Audio & subtitles it translates by itself.
- **Subtitles that will not sync are swapped.** When a file cannot be lined up with a
  file-matched one, the player tries up to three other files in that language before
  going back to the first and saying so. Files you pick by hand are never swapped.
  UTF-16 subtitle files are read correctly.
- **Smoother everywhere.** Catalog and source replies are parsed and cached on the
  network threads instead of in a frame, so a 300-source answer or ten rows arriving
  together no longer stall the picture; replies are handed to the screen within an 8 ms
  budget per frame, and anything slower is logged with its host for Diagnostics. When
  idle, the app now wakes the instant a key arrives instead of polling.
- **Hold OK for more, like Nuvio:** holding OK on any title (Home rows, Explore, Library,
  Search results, a title page) opens *Mark as watched / not watched*, *Remove from
  Continue Watching* and Library; Yellow, Menu or Info do the same. A short press still
  opens the title. Only those screens wait for the release; everywhere else OK acts the
  instant it is pressed, and a remote that never reports releases keeps instant OK.
- **From the Nuvio port, made ours:** *From the start*
  next to Resume; recent searches; sources in your audio language (or MULTI / DUAL)
  first, with a switch in Settings > Sources; *Autoplay waits for slow addons* (3 s to
  every addon, 8 s by default); addon notice rows ("Join our Discord") never autoplay;
  links that failed are sorted last for six hours; in your language, an audio track the
  TV surely plays (not DTS or TrueHD) is chosen first; a *No internet* pill when nothing
  gets through; a notice after a crash pointing to that session's log; and a once-a-day
  check that tells you when a newer version is in the Homebrew Channel.

## 0.6.3

- **Subtitles that fit the file.** The player fingerprints the video the way
  OpenSubtitles does (its size plus the first and last 64 KB) and asks the subtitle
  addons for files matched to that exact file; they are listed first and marked
  *matched to this file*. When you pick any other subtitle, its cue timings are
  compared with a matched one (same language if there is one, any language otherwise)
  and the offset is applied by itself: "Subtitles synced to the file (+1.3 s)". The
  Subtitles menu has an *Auto-sync* row with the status, and Settings > Audio &
  subtitles has the switch.
- **AI assistant (optional, your own free key).** Settings > API keys > AI assistant
  takes a Groq, OpenRouter or Google Gemini key (all have free tiers), or any
  OpenAI-compatible endpoint such as an Ollama box on your network. It unlocks:
  *Ask AI* on a title page (a spoiler-free nutshell, who it is for, what a parent
  should know, three similar titles, cached for a month); describing what you feel
  like watching in Search ("something funny with robots": the model names titles,
  your addons find them, marked with a star and listed first); and *Translate to
  <your language>* in the player's Subtitles menu when nobody offers it, translated
  in batches around the playhead so it starts within seconds. Nothing is sent
  anywhere until you turn it on, and the key goes in from your phone like the others.
- **Memory saver that watches the TV.** Automatic mode no longer just looks at the
  RAM size at start: the app samples free memory and its own footprint every few
  seconds, turns the saver on after ten seconds of tight memory (fewer rows and
  posters, artwork budget cut to 32 MB, then 16 MB if it stays tight), answers the
  system's low-memory warning at once, and turns it off again after two minutes of
  room. Settings > Interface says why it is on; About shows the TV's memory.
- README rewritten around what the app does today, with fresh screenshots.

## 0.6.2

- **Picture-in-picture is off by default.** Back in the player stops playback and
  returns to the title, as in the official apps. Settings > Playback >
  *Picture-in-picture on Back* brings the corner player back.
- **Cast photos without a key** when the meta addon carries them (Cinemeta's
  `app_extras.cast`), with character names. A TMDB key still adds the age rating and
  fills gaps; a key can also be baked into the build from a CI secret
  (`TMDB_API_KEY`), which is how apps like Nuvio work without asking the viewer.

## 0.6.1

- **Take breaks** (Settings > Take breaks). Pick a schedule: Islamic prayer times (Fajr,
  Dhuhr, Asr, Maghrib, Isha computed on the TV from your location, with the usual
  methods and Hanafi Asr), Christian Angelus, Jewish Shacharit / Mincha / Maariv from
  sunrise and nightfall, Hindu Sandhya at sunrise, noon and sunset, Sikh Nitnem, Baha'i
  prayer windows, Buddhist morning and evening chanting, or only your own times. Add a
  stretch reminder every so many minutes of watching, a bedtime that stops playback,
  and up to three times of your own. A banner warns a few minutes ahead; at the time the
  picture pauses and a calm page shows the moment with a chime, or a recording you
  provide (an adhan at the start, a dua at the end), then playback continues by itself.
  The location comes from a tap on *Find my location* (ip-api.com) or typed as
  latitude, longitude; sun times are computed offline, so no account and no key.
  See docs/BREAKS.md for how each schedule is derived.
- **Type it on your phone**: every text field in Settings (API key, server addresses,
  recording URLs, location) shows a QR code. Scan it, paste on the phone, Send, and the
  text lands in the field and saves. The page is served by the TV itself on your home
  network (port 11471) and only accepts text while a field is open.

## 0.6.0

- **API keys** (Settings > API keys): add your own TMDB key and titles get cast photos
  with character names (details page and the paused panel) and an age rating chip
  (PG-13, TV-MA, 15, …) next to the IMDb score. Nothing is asked of TMDB without a key.
- **Animated screen changes**: pages slide and settle when you open and leave them, the
  player's side menu slides in, settings panes glide when the category changes. Off with
  Settings > Interface > *Animated screen changes*, and off by itself on Light effects.
- **Memory saver** (Settings > Interface > *Memory use*): Automatic turns it on for TVs
  with under 2 GB; it keeps 48 MB of artwork instead of up to 192, loads only the rows
  next to the focus, shortens rows to 40 titles and stops Explore at 140.

## 0.5.7

- The centre of the picture stays clear: the -10 s / +10 s marks now appear only as a
  flash on the side you seek towards, and go away at once.
- The controls lock is gone.

## 0.5.6

- **Seek previews, your own way**: `tools/thumbs/server.py` (ffmpeg, one file, or the
  Dockerfile beside it) serves a frame for any time in the stream; point Settings >
  Playback > *Seek preview server* at it and the player shows the picture above the
  time bubble while you seek, fetching neighbours ahead. Nothing leaves your network
  and no key is needed; without a server the player behaves as before.
- **Saved sign-ins**: every account you sign into is kept; Settings > Account lists the
  others as *Switch to …* (OK switches without a password, Left forgets one).
- **Parental PIN** (Settings > Parental): a four-digit PIN set on a keypad; it can lock
  Settings and hide catalogs of addons flagged adult. Number keys on the remote work.

## 0.5.5

Player extras from the best mobile players.

- **Centre controls**: -10 s, play/pause and +10 s sit in the middle of the picture while
  the controls show; the side one lights up as you seek with Left/Right.
- **Second subtitles**: the Subtitles menu's first row chooses whether you are picking the
  main subtitles or a second language; the second file draws above the first, a size
  smaller. Any addon file can be the second track.
- **Forced subtitles**: when subtitles would stay off because the audio is already your
  language, a forced track in that language (signs, foreign lines) from the file header
  is turned on by itself.
- **Lock controls** (player bar): the remote is ignored until Back is pressed three times,
  for curious children and sleeping cats.
- **Sources menu** groups the links by addon; the **Episodes** panel shows thumbnails.

## 0.5.4

Settings you can find your way around, and a Home that is yours.

- **Settings redesign**: categories down the left (Account, Playback, Audio & subtitles,
  Sources, Interface, Home rows, Addons, About), their rows on the right. Up/Down picks
  a category and the pane follows; Right enters the rows, Back returns.
- **Home rows**: show or hide any row (Continue Watching, My Library, each catalog) and
  put them in your order. OK toggles, Left/Right moves; Home follows at once.
- **Addon order**: Left/Right moves an addon up or down in Settings > Addons. The order
  is pushed to your Stremio account, so sources list in that order everywhere.
- **Hide watched titles** (Settings > Interface): catalogs on Home and in Explore skip
  titles the library marks watched.

## 0.5.3

Side navigation, and three new places to find things.

- **Side rail** on Home: Home, Explore, Search, Library, Calendar, Settings and the account
  as icons down the left; Left from the first card opens it with labels, Right or Back
  returns to the content. The top-right pills are gone; the clock stays.
- **Explore**: every catalog addon's lists by type (Movies, Series, …), list and genre,
  as a poster grid that loads more as you scroll. Catalogs that need a genre (Cinemeta's
  by-year lists, for example) start on their first option.
- **Library**: the whole library as a grid with All / Movies / Series / Continue watching
  and a Recent / A to Z sort; progress bars on titles in progress.
- **Calendar**: episodes of the series in your library from the last two weeks and the
  next month, by day, with thumbnails; OK opens the series. Details come from the meta
  addon and are cached for twelve hours.
- **Subtitles menu** lists every file on its own row ("English", "English (2)", …) with
  the addon's name under it, the preferred language first; nothing is folded away.

## 0.5.2

The player grows the controls people expect from a TV app, and Home gets three looks.

- **Player bar**: Previous episode, Play/Pause, Next episode, Reload, Subtitles, Audio and
  speed, Aspect ratio, Sources, Episodes, Stats. Buttons are round icons; the focused one
  opens into a labelled pill. *Reload* restarts the same link where you are; *Aspect ratio*
  cycles Fit, Fill the screen (crops the letterbox, from the file's real picture size) and
  Zoom; *Episodes* lists every season with the one playing marked, OK jumps to another.
- **Speed** 0.5x to 2x in the Audio menu (Left/Right); the end time follows it.
- **Paused info**: a moment after pausing, the top-left shows *You're watching*, the logo
  or title, the episode, IMDb rating, year and runtime, the synopsis and the cast.
- **Badges**: *1080p*, *HDR*, *Dolby Digital+ 5.1* next to the title, from the file's header.
- **Clock everywhere**: the time sits in the top-right of every screen and hides during
  playback (it comes back with the controls, together with *ends at*). Settings > Playback
  > Clock chooses 12-hour, 24-hour or off.
- **Home screen styles** (Settings > Interface > Home screen): *Cinematic* (the focused
  title fills the backdrop, as before), *Showcase* (a featured carousel from the first
  catalog with page dots and a *View title* button, turning every 8 s; the rows sit below)
  and *Compact* (no hero, rows from the top, one line about the focused title).
- *Subtitle style* moved into the Subtitles menu as *Style and size*.

## 0.5.1

The TV checks the file, not just its name.

- **Pre-flight check** (Settings > Playback > *Check sources before recommending*, on by
  default). Before recommending, the app fetches the first 2 MB of the three best
  candidates, one at a time, and reads the real container header: Matroska tracks,
  chapters and colour metadata, or MP4 `moov`. The recommendation card then says
  *Verified: HEVC 1080p 10-bit · Dolby Digital+ 5.1 · 3 subtitle tracks · 14 Mbit/s
  (this link gives 85)*; the same fetch measures the link's download speed, so a file
  faster than the link is marked *Too fast for this link* and loses the recommendation,
  and a dead link is marked *Link not responding* and dropped. A name that lies about
  its codec or audio is corrected by what the file says.
- **WebP posters decode.** metahub serves newer titles' posters as WebP, which the TV
  cannot read; libwebp's decoder (BSD, 1.4.0) is now built into the app, so those
  posters appear like any other.
- **Subtitles menu** groups files by language (Left/Right picks another file of the same
  language), puts your preferred language first, scrolls to the focused row, names
  Brazilian Portuguese properly, and has a *Subtitle timing* row for the offset.
- **Sources menu** gains *Browse all sources*, which leaves the player for the full list
  (with the recommendation), so a playback started from *Resume* can still switch.
- **Corner player** offers *Full screen* and *Close* buttons when selected (Left/Right,
  OK), for remotes without a Stop key.
- **Live scrub** (Settings > Playback > *Show the picture while seeking*): while Left or
  Right is held the video jumps along every third of a second, so the picture is the
  preview. **Chapters** from the file's header appear as marks on the timeline and in
  the seek bubble; Rewind/Forward jump chapter to chapter (and 60 s without chapters).

## 0.5.0

- **Recommended source.** Every source is read for its resolution, codec (H.264, HEVC, AV1),
  HDR or Dolby Vision, audio (AAC, Dolby Digital(+), DTS, TrueHD, Atmos), size, bitrate,
  cached/debrid state, seeders and release type, then scored against a profile of this TV:
  what LG's own tables say for its webOS generation (no DTS or TrueHD on recent sets, AV1
  from webOS 5), corrected by experience: a source that fails, or triggers the TV's
  "can't play this audio" message, teaches the app to avoid those tags; one that plays for
  90 seconds without a stall marks them fine. The best playable source appears as a card
  above the list with a one-line summary; rows carry a *Recommended* chip or a concern
  ("DTS audio: may be silent on this TV", "Not cached: needs a download first").
- Settings > Playback: *Sort sources by* (Addon order, which stays the default; Best for
  this TV; Highest quality; Balanced; Smallest size), *Resolution for recommendations*
  (4K, 1080p, 720p) and *Play the recommended source automatically*.
- **Synopsis for every title.** Continue Watching and Library items arrive from the account
  without one, so the Home hero fetches the title's details on focus and keeps them; the
  synopsis also grew from three to four lines.

## 0.4.8

Fixes from the first evening on a real LG TV (QNED, webOS). The app installs from the
Homebrew Channel, signs in, browses, plays debrid streams and does picture-in-picture.

- **Sources follow your addon order** (the order in your Stremio account), the way the
  official apps list them. Answers used to be listed in the order they arrived, so a slow
  first addon ended up far down.
- **Player control bar.** Down moves from the timeline to a row of buttons: Audio,
  Subtitles, Subtitle style (style and size, previewed live), Sources (switch to another
  source at the same position), Next episode and Stats. Left/Right move along the row,
  OK opens, Up goes back to the timeline. The colour-key shortcuts still work.
- **The corner player is selectable.** Press Down past the bottom of any screen and the
  corner player takes the focus (white ring); OK brings it to full screen, Stop closes it.
  Play/Pause still works from anywhere.
- **Posters that fail** (WebP images, which the TV's decoder cannot read, or dead links)
  fall back to metahub's JPEG for IMDb titles, and the reason is logged in Diagnostics.
- **QR sign-in fixed.** The code check now sends `type=Read` the way Stremio's own apps
  do; without it the service answered "Invalid or expired token" and the app gave up
  (and hid the QR code) right away. An error from the service while waiting now means
  "not approved yet": the QR stays up and the app keeps checking, and after five minutes
  it quietly asks for a fresh code.
- The sign-in screen shows the exact address the service returns for entering the code.

## 0.4.6

- **Picture-in-picture.** Back in the player now parks the video in the bottom-right
  corner and keeps it playing while you browse the episode list, Home, Search or
  Settings. Play/Pause brings it back to full screen; Stop closes it; picking the same
  episode again also brings it back, and starting anything else replaces it. Automatic
  intro skipping and progress sync keep working in the corner; when the episode ends
  there, the corner closes with a note. Stop (or Back on the error card) still stops
  playback outright.

## 0.4.5

- **Subtitle styles** (Settings > Playback > *Subtitle style*): Soft shadow, Bold outline,
  Classic yellow, or Black box, with a live preview while the size or style row is
  focused. Applies to subtitles from addons (SRT/VTT); embedded subtitle tracks are drawn
  by the TV's own player and cannot be styled by apps.

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
