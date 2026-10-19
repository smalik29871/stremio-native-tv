# Changelog

## 0.10.9

- **Never stuck on a dead source.** A source that stops mid-film is replaced at the same
  moment, with the same audio language and subtitles. The next source is checked while
  you watch, so the switch is quick. When the buffer keeps draining, a lighter copy
  takes over before playback stops. Play tries the top three sources at once and
  starts the first that answers. If every source fails, the addons are asked again.
- **Source health.** Servers and addons that keep failing on this TV, at this time of
  day, drop down the list and are marked "Often fails in the evening".
- **Trailers as Nuvio plays them.** The way Nuvio's extractor does it: YouTube's API key
  from the watch page, every script-free client, mirror servers, 10 MB range pieces.
  When the addon has no trailer, TMDB's is used.
- **YouTube from addons plays here.** YouTube entries (YouTubio, trailer addons) stream
  in the app instead of saying "not supported".
- **Subtitles that fit the video.** Among files in your language, the one that ends with
  the video, has a full set of lines and lines up with the file-matched one is picked,
  with the reason shown.
- **OpenSubtitles.com built in.** Searched by title and your file's fingerprint, release
  names compared with your video; only the chosen file is downloaded. Optional account.
- **SimKL sign-in fixed**, and a refused sign-in now shows the service's reason.

## 0.10.8

- **Trailers play like Nuvio's.** The TV asks YouTube as its phone apps do and streams the
  trailer at once (the HLS playlist when there is one) instead of downloading it first.
  If YouTube refuses, the download and then the TV's YouTube app take over.
- **Subtitles stay in sync.** Matching a subtitle to the file now also corrects frame-rate
  drift (23.976 / 24 / 25 fps releases) and cuts (a scene missing from one cut). A timing
  fix you make by hand is remembered for that subtitle file and comes back next time.
- **Source rules.** Settings > Sources: the largest file to pick, avoid HDR or Dolby
  Vision, cached sources only, and words to avoid (CAM, HDTS, 3D). Sources that break a
  rule stay in the list, marked, but are never picked for you.
- **Franchises in order.** A film's title page lists its whole series by release date
  (TMDB collections), the current one marked; OK opens another.
- **What this TV can play.** Settings > Sources shows which formats play here and why,
  and can forget what it learned.
- **From your phone.** Play a video link sent from your phone, and back up or restore
  your settings through the phone page (your Stremio sign-in is not included).
- **Trakt, SimKL, MDBList.** Trakt signs in with a code, scrobbles what you watch and
  adds your watchlist and recommendations as Home rows. SimKL adds what you finish to
  your history. MDBList adds Rotten Tomatoes, Metacritic and Letterboxd scores to
  title pages. The app ids can be built in or entered in Settings > API keys.

## 0.10.7

- **Trailers when YouTube says "sign in".** YouTube sometimes refuses the TV's own
  download ("Sign in to confirm you're not a bot"); the trailer now opens in the TV's
  YouTube app instead, which is signed in and trusted. Back returns to Stremio.

## 0.10.6

- **No "Buffering keeps stopping" from seeking.** Rewinding or skipping makes the player
  fill its buffer again; those pauses, and any while paused, no longer count towards
  offering a lighter source. Only buffering during plain playback does.

## 0.10.5

- **Explore's list and genre rows scroll properly.** Moving along a long row no longer
  snaps it back to the start: it stays put while the focused pill is in view and moves
  just enough to keep it there.

## 0.10.4

- **Two new themes.** *Mist*: Glass's rounded shapes, soft colours and white focus on a
  lighter slate background, without the blurred artwork, panel sheen or shadows, so it
  draws less and moves quicker. *Sunset*: warm plum with a sunset haze across the bottom,
  coral, gold and flamingo accents, built just as lightly. Settings > Interface > Theme.
- **The resume card with the arrow keys.** Up from the top of Home focuses the island's
  resume card; OK opens the title ready to resume, Down or Back go back to the rows. The
  blue key still works.

## 0.10.3

- **The clock island says when an update is out:** a dot and "Update 0.10.x" beside the
  time, until it is installed (Settings > About > Update, or the Homebrew Channel).

## 0.10.2

- **Updating when the TV won't let the app install.** On TVs that refuse the app a call
  to the Homebrew Channel's installer, the update screen now says so plainly and OK opens
  the Homebrew Channel, where Stremio (native) > Update installs the new version.

## 0.10.1

- **Updates come from a new address.** Releases now live in a public repository of their
  own, [stremio-custom-lg-tv-releases](https://github.com/smalik29871/stremio-custom-lg-tv-releases);
  the app checks there from this version on. In the Homebrew Channel, replace the old
  repository with
  `https://github.com/smalik29871/stremio-custom-lg-tv-releases/releases/latest/download/repo.json`.

## 0.10.0

Ahead of Nuvio: everything a side-by-side found missing, kept as light as the rest.

- **A colour from the artwork.** A new accent choice, *From the artwork*, for every theme:
  focus, highlights and progress take the colour of the title you're looking at and ease
  between titles; grey artwork falls back to the theme's own colour.
- **Title pages go deeper.** Down from the buttons (or the episodes) scrolls to *About*:
  the full synopsis, who directed and wrote it, release date, country, awards, a ratings
  card with your progress, the cast as photos with their roles (OK searches for their
  other titles) and *More like this* from the same genre (OK opens it).
- **A lean-back pause.** Paused and left alone for six seconds, the controls step aside
  and the title, synopsis and cast stay with a quiet "Paused · time left" line. OK plays
  on; any other key brings the controls back.
- **Player menus start where you are.** Audio and subtitle menus open on the track that
  is playing, and in the episodes menu Left and Right jump a season.
- **When buffering keeps stopping**, the player offers the best source below the one
  playing (by its real resolution when the file was checked): OK switches and carries on
  from the same moment, Back keeps watching. Once per video.
- **Seeking fits the video.** Holding Left or Right still speeds up (now to two minutes a
  press on a film) but never moves more than a sixtieth of the video at a time, and the
  side mark shows how far each press went.
- **Trailers.** Title pages with a trailer get a Trailer button: the TV fetches it from
  YouTube once (the island shows it coming) and plays it in the player. Watching it
  saves nothing against the title: no progress, no resume, no Continue Watching.
- **Find a setting.** Up from the first category reaches *Find a setting*: type a word
  and the matching settings are listed with their category (names first, then what
  their help says); one match opens straight on its row, ready to change.
- **Sliders for scales.** Subtitle size, screensaver delay, reminder and break lengths,
  addon wait and the resolution limit show a slider under the value: where you are,
  and how far there is to go.
- **A launch animation.** The Stremio mark grows in at the centre and settles into its
  place in the rail as Home appears. Any key skips it; it stays off with Light effects
  or without animated screen changes.
- **Who's watching?** With more than one Stremio account saved on the TV, the app opens
  on a profile picker: a tile each in its own colour with its initials. OK signs that
  profile in (its library, addons and progress); Back keeps the one signed in. Settings >
  Account can stop asking.
- **Your stats.** Library has a *Your stats* tab: time watched, titles started and
  finished, films against series, your most watched titles with their hours, and a
  square-a-day chart of when you watch, with your busiest weekday and longest run.
- **Your lists.** *Save to a list…* in any title's options (hold OK or Yellow) keeps it
  in Favourites, Watch later, With the family or Rewatch. Each list becomes a row on
  Home (newest first, movable in Settings > Home rows); each profile has its own.
- **Moods in Explore.** *Moods* in Explore's type row offers Feel-good, Edge of your seat,
  Mind-bending, Date night, Family night, Spooky, Epic and True stories: each mixes three
  genres from your own catalogs, films and series together, the best matches first.
- **A calmer Home hero.** Scrolling quickly along a row no longer swaps the artwork for
  every card passed; it changes once the focus rests. The Showcase page dot fills until
  the carousel moves on.

## 0.9.0

A polish release: a richer Settings, two new themes, a clock island, smoother motion
and fixes to focus and player keys found in a review against Nuvio.

- **Settings, explained and previewed.** Rows are grouped under headings (Languages,
  Subtitle look, Look, Motion, Updates…) with a line of help under each one. Inside a
  category the list shrinks to its icons and a panel beside the rows draws what the
  focused setting changes (home layouts, subtitle look, clock, card details, hidden
  watched titles, blurred stills, effects, OLED black, resolution limit, source order and
  checks, addon wait, skip intro, autoplay, picture-in-picture, seeking, screensaver,
  transitions, breaks, languages, memory, subtitle sync, P2P, updates, the PIN), then
  its name, help, current value and keys. Switches stay readable on the focused row.
- **Themes: Glass and Metro.** Duotone is replaced by **Glass** (frosted panels over the
  last artwork, a white focus glow) and **Metro** is new (flat colour tiles, square
  corners, a white tile border for focus, light lowercase headings in Open Sans). A
  saved Duotone choice opens Glass.
- **Clock island.** The clock pill grows into notices (they used to pop up at the
  bottom), progress, and a *Resume · 32 min left* card for the title you just left mid-way;
  Blue opens it from Home, Explore, Library, Calendar or Search.
- **Motion.** Titles open out of the card you pressed and shrink back into it on Back;
  rows scroll on a spring that eases in and out; the focused card dips on OK; the end of
  a row nudges; cards rise in one after another when a row arrives; the Settings preview
  rises into place. No offscreen buffers; Light effects turns it all off.
- **Focus.** Right from the side menu returns to the row you left, and coming back from a
  section puts focus on the content (the menu used to stay open, so the next Back closed
  the app); leaving takes two Backs. Back goes content, filters, then the side menu on
  Explore, Library and Calendar too. Focus follows its item when lists re-sort (sources,
  Continue Watching, Library, Home rows). Search results take the focus when they
  arrive. Settings: Left goes back to the categories except on choices; each category
  remembers its row; forgetting a saved sign-in asks first. Season tabs scroll.
- **Player keys.** OK skips the intro only while the controls are hidden (Up focuses the
  Skip button otherwise); the button row starts on Play and remembers its button, so the
  first Down then OK no longer starts the previous episode; autoplay waits while a menu
  is open; OK lands a pending seek and Back cancels it; Down below the buttons hides the
  controls; Down on the Up Next card brings the controls; a held OK presses once.
- **Up Next.** OK on the card plays the next episode (it used to pause).
- **More colours.** Ten accent colours per theme (four new in each), and a new
  **Background** setting with four tints per theme (Signal: Graphite, Navy, Forest, Plum;
  Marquee: Ink, Sepia, Velvet, Night; Glass: Dusk, Aurora, Rose, Slate; Metro: Black,
  Charcoal, Midnight, Wine), shown in the preview as you pick.
- **Sign-in and settings survive power-offs on webOS 10.** Newer TVs run the app as its
  own user, for whom every folder it tried was read-only or in RAM, so everything was
  lost when the TV powered off ("store: … is in RAM" in Diagnostics). The app now also
  tries the developer partition and internal storage, both on flash.
- **Screensaver styles:** Artwork (as before), Poster wall, Clock (big and dim, kind to
  OLED panels) and Floating logo, with a preview in Settings > Interface.
- **Fixes from a final sweep** (code review plus every scripted run under address and
  undefined-behaviour sanitizers): a heap read past the end in the font baker when a
  face lacks glyphs in several ranges (stb_truetype, patched); OK kept working on remotes
  that never send key releases; the sources list no longer moves its focus by itself
  while addons answer; a seek waiting to land beats Up Next and Skip for OK, and Back
  puts the picture back even with live preview; a stale Up Next card no longer turns OK
  into "next episode" after rewinding; Settings acts on the focused row's own kind
  (not the last one drawn) and never leaves focus on a heading; the first-run setup no
  longer zooms out of the corner; Blue opens only a Resume card that is on show; stored
  settings out of range are reset instead of indexing past a list.
- **The TV's own Settings button no longer pauses the video.** Its overlay only takes
  the remote; the app is still on screen, so playback carries on (progress is saved).
  Leaving for Home or another app still pauses. Window events are written to the log.

## 0.8.8

- **Updates ask first.** When a new release is out, the app waits until nothing is
  playing and shows a popup: *Update now* or *Later*. Nothing installs until you choose
  Update now (0.8.7 started by itself after a 10-second countdown). Later asks again the
  next time the app starts.
- **Settings > About > Update** still checks or installs right away; *Ask me when an
  update is out* turns the popup off (you then get a short notice instead).

## 0.8.7

- **Updates install themselves.** The app checks for a new release at start and every
  six hours (it used to check once a day and only tell you). When there is one, it
  waits until nothing is playing, shows a 10-second countdown (OK: now; Back: not now,
  it asks again at the next start), then asks the Homebrew Channel to download the
  package, check its SHA-256 and install it, with progress on screen. webOS closes the
  app while it installs; open it again from the Home screen.
- **Settings > About > Update** checks or installs right away, and *Update
  automatically* turns the automatic part off (you then get a notice instead).

## 0.8.6

- **Sign-in and settings survive the TV losing power.** Settings were saved by writing a
  new file and renaming it over the old one, but nothing made sure the data had reached
  the TV's flash first. A power cut right after a save could leave `settings.json` empty
  or cut off, and the app then started as if new: the welcome screen, signed out, every
  preference gone, and the next save made it permanent. Now:
  - the file is forced to flash before it replaces the old one, and the copy before it
    is kept as `settings.json.bak`;
  - an empty or damaged `settings.json` is recovered from that copy at start (Diagnostics
    says so), and the damaged file is kept aside instead of being overwritten;
  - saving happens on a background thread, so the extra flash write never stalls a
    frame, and everything pending is written when you press Home or switch apps.
- If the data folder turns out to be on a RAM disk (wiped at power-off), the app uses a
  persistent folder next to itself instead and logs a warning.

## 0.8.5

- **P2P fallback for TorBox, on the TV itself.** When TorBox has no cached copy of a
  title (or TorBox is down), torrent sources from your addons now play on the TV through
  a small engine bundled with the app. Nothing to host and nothing to pay: it keeps the
  stream in a RAM buffer (96 MB on small TVs, 192 MB otherwise; nothing is written to the
  TV's storage), caps uploads at 256 KB/s and stops by itself when nothing plays.
  - These sources show a *P2P* badge with their seeders and rank after every cached or
    direct link. Uncached TorBox entries ("needs a download first") now rank after them,
    because they cannot play yet.
  - If TorBox links keep failing, the player goes to the P2P source after two tries.
  - While it plays, the source line shows peers and speed.
  - Peers see your IP address while a P2P source plays. Settings > Sources turns the
    fallback off.
- **Back to TorBox when it has the title again.** While a P2P source plays, the player
  asks your addons every two minutes; as soon as one offers a cached copy at least as
  sharp, it switches to it at the same position, says so, and frees the P2P buffer. If
  that copy fails, P2P is next in line again.
- Torrent sources to play P2P need an addon that lists them, such as Torrentio added a
  second time without a debrid key.
- The app is about 6.5 MB larger (the engine).

## 0.8.4

- **Subtitle size, style, position and timing reach subtitles inside the file.** The
  player's Subtitles menu only changed subtitles from your addons, which the app draws
  itself; tracks inside the video are drawn by the TV, and nothing told it about your
  choices. The player now passes them to the TV's own subtitle renderer: size, text
  colour (classic yellow), outline or shadow, the black box, position, and timing
  (Left/Right on *Timing* now works for these tracks too). The TV starts a fresh
  pipeline for every video, so the style is sent again after each load and each
  subtitle track change.
- **"Mark as unwatched from here on" is always in an episode's options**, not only
  when a later episode already looked watched (and it works on the last episode too).
  The range option reads *Mark as watched up to here*.

## 0.8.3

- **Caches no longer pile up.** Twelve seconds after start (on a background thread) and
  whenever a break recording changes, the app tidies its data folder:
  - addon replies (catalogs, every search) older than a week go, and the rest is kept
    under 16 MB, newest first;
  - recordings no break uses any more (a replaced upload, the download of an old
    YouTube link) are deleted;
  - half-written files left by a crash or a lost connection (`.tmp`, `.part`) go;
  - font atlases and YouTube player data from older versions go after 60 and 30 days.
- The artwork cache (96 MB on disk) now trims itself during long sessions too, not only
  at start, and artwork that is still being shown counts as recent, so the posters you
  see every day are not the ones thrown out.
- Seek-preview pictures stay out of the artwork disk cache: they would have pushed
  posters out.
- The log file starts again once it passes 4 MB, so days of uptime cannot fill the
  TV's storage. Diagnostics keeps showing the recent lines.

## 0.8.2

- **Seek previews without a computer.** The picture above the timeline while you seek
  is now made by the TV itself. It reads the video file's keyframe index (Matroska Cues
  or MP4 sample tables) once, downloads only the keyframe at or before the position
  (one small range request), decodes it with a bundled FFmpeg that has nothing but its
  H.264 and HEVC decoders (8 and 10-bit), and shows it scaled down. HDR frames get a
  lift so they do not look grey. The home helper is optional now: when it is set, the
  previews come from it as before. Fragmented MP4 and files without an index show no
  preview; Diagnostics says why (`frames:` lines).
- The app is about 0.8 MB larger (FFmpeg's two decoders).

## 0.8.1

- **Watch progress reaches your account, including when the app closes.** Three
  problems made other devices (and this TV, after a restart) show an older position:
  - Closing the app with a film playing never saved the last position, and progress
    still waiting to be sent was dropped as the app shut down. Now the player saves on
    the way out, saves again the moment you press Home or switch apps, and the app
    waits up to 4 s for the account to take it.
  - The dates were sent as numbers, but the official Stremio apps read `_mtime`,
    `_ctime` and `lastWatched` only as ISO dates, so they could not use what this TV
    wrote. They are ISO dates now.
  - At start the account's library replaced ours wholesale. Now a title changed on
    this TV later than on the account (the TV was switched off before it could send)
    keeps this TV's position, which is sent again.
- **YouTube's "Sign in to confirm you're not a bot".** The TV now asks YouTube the way
  yt-dlp does today: it first opens the video's page like a browser to get a visitor
  id (YouTube distrusts requests without one), then asks as YouTube's visionOS app,
  which needs no player script and no proof-of-origin token. YouTube's TV app and its
  embedded player follow if that is refused. Diagnostics shows which one answered.
  When every one of them gets the bot check, the message says so and suggests sending
  the file from your phone instead.

## 0.8.0

- **Hold OK on an episode for its own options.** Holding OK on an episode used to open
  the series' options, so "Mark as watched" marked the whole show and nothing could be
  undone per episode. Now it opens a sheet for that episode: mark it watched or not
  watched, *Watched up to here*, *Not watched from here on*, the whole season either
  way, *Sources for this episode* and *Read synopsis*. Episode marks are kept on this
  TV and override what the resume point implies; ticks, spoiler blur and the episode
  line follow them at once.
- **You can see the hold working.** While OK is held, the line under the focused card
  (episodes, Home rows, Explore, Library, Search) reads *Hold for options* with a bar
  that fills; once it is full it turns to *Release for options* with a soft pulse, and
  letting go opens the sheet. A quick press still opens the title or episode as before.
- **A livelier options sheet** with an icon per action, items that arrive one after
  another, and the theme's colours (Signal's accent edge, Marquee's amber rule).
  Notifications take the theme's panel colour too.

## 0.7.9

- **No more stray strips while scrolling Home.** A row sliding off the top was cut
  hard at the edge, and the last strip of a Continue Watching card (its dark shade,
  progress line and episode number) floated under the synopsis like a glitch. Rows now
  fade out as they reach the edge and fade back in on the way down. The shade on a
  Continue card also follows the card's rounded corners (a square one showed past
  them, most in Duotone), and in Marquee its text and progress rise with the focused
  card.

## 0.7.8

- **YouTube links without the home helper.** Paste a YouTube link as a break recording
  (an adhan, a dua) and the TV saves it by itself: it asks YouTube for the video the
  way YouTube's own TV app does, solves the player's URL challenges with yt-dlp's
  solver running in a small JavaScript engine (QuickJS) built into the app, and
  downloads the audio, or a 360p video when *Show the video* is on, once. Breaks then
  play it from the TV, offline too. The first link takes up to a minute while the
  player is prepared; later ones only download. If the bundled solver stops working
  after a YouTube change, the TV fetches the newest one from yt-dlp's releases and
  retries. The home helper is now only a fallback. Links saved before this version
  are saved on the TV at the next break.
- **Diagnostics shows the TV's video decoders** (FFmpeg's libavcodec, libde265,
  OpenH264, GStreamer) next to the webOS version. This tells whether seek previews
  can be decoded on the TV itself, without the helper.
- The app is about 0.5 MB larger (QuickJS and the solver).

## 0.7.7

- **Subtitle settings where you need them.** The player's Subtitles menu now opens
  with the controls: timing (Left/Right moves the subtitles 0.1 s earlier or later,
  OK resets), size (now with *Extra large*), style, position (bottom, raised or top)
  and auto-sync, then Off and the tracks. Changes show on the video straight away and
  are remembered. The separate Style menu is gone.
- **Search opens clean.** The keyboard stays closed until you press OK on the search
  bar, so recent searches and results get the whole screen. With the keyboard open, a
  QR code beside it lets you type or paste on your phone; what you send is searched
  straight away. Done or Back closes the keyboard, Up from the results returns to the
  bar.
- The "No French subtitles, translate?" notice shows once per video instead of again
  each time the list refreshes.

## 0.7.6

- **No more false "No internet".** Slow addons and the 2 MB format checks that time
  out mid-transfer were counted as network failures, so a screen with 122 sources
  could show the red pill while everything worked. Now only requests that got nothing
  at all back (DNS, connection, TLS) count, and any data received resets it.
- **Better source picks.** "360p" was not understood and was scored like an unknown
  resolution, close to 1080p; "HDRip" and "HDTV" were taken for HDR. Both fixed. When
  a sharper source loses, the recommendation now says why ("The best 1080p source is
  not cached yet: it needs a debrid download first"), and Diagnostics lists the five
  best scores with their reasons.
- **Resume where you left off, next episode first.** A series you were watching shows
  *Resume S2 E14* even when the progress came from another device: it opens that
  episode's sources and starts at your position. The episode row starts with the
  focused (next) episode on the left instead of third.
- **AI that keeps working.** When the chosen model is retired or missing, the app asks
  the provider for its models, switches to the best available one and tells you.
  Answers from reasoning models are read correctly (their thinking is dropped and they
  get room to finish). Settings > API keys > AI model lists the models your key can
  use. *Ask AI* moved from the title page's buttons into the Hold-OK options.
- **Faster start, right memory, known TV.** The account library (hundreds of KB) is
  read and cached on a network thread instead of in a frame (39 ms on a QNED). The
  memory saver no longer treats 2 GB TVs as small (they report 1.6-1.9 GB). The webOS
  version is found on firmwares that hide /etc/starfish-release from apps, so the TV
  profile and Diagnostics show it.

## 0.7.5

- **Breaks as long as the adhan.** Settings > Take breaks > Break length has a new
  first choice, *Same as the recording*: the break ends when your recording finishes
  (and the closing dua after it, if you set one), and the countdown shows the
  recording's own time left. With the chime or silence there is nothing to follow, so
  such a break lasts one minute. A recording that never starts gives up after 20 s.

## 0.7.4

- **Details on a card that stays focused.** Rest on a poster for a moment (on Home,
  in Explore, Library and Search) and a panel slides open beside it: the title, IMDb
  rating, year, runtime and type, genres, the synopsis, and whether it is in your
  Library, watched or part-way through (with its progress). It opens to the left near
  the right edge, follows the theme (Signal's accent edge, Marquee's amber rule,
  Duotone's soft card), and fetches the full details only once you have paused, from
  the same cache Home and the title page use. Settings > Interface > *Show details
  when a card stays focused* turns it off.

## 0.7.3

- **Episode details where you can read them.** With the episode row focused, the
  space under the show's logo becomes that episode: *Season 1 · Episode 2 of 8*, the
  title, the air date ("22 Aug 2021"), the runtime, *Watched* or the minutes left, and
  three lines of synopsis, fading in as you move. The line at the bottom edge of the
  screen, cut off and half in the TV's overscan, is gone.
- **Subtitles you can tell apart.** Tracks in the video file are named by language
  ("Hungarian", not "hu") with what the file says about them: their name (SDH, Forced,
  Signs & Songs), forced, default and the format (text, or a picture format like PGS).
  Audio tracks get the same, with the channels. Addon files read "OpenSubtitles v3 ·
  option 2 of 3" instead of "English (2)". Sections say where each comes from.
- **Sources in your addon order, with what each one is.** The player's Sources list
  groups by addon in the order of your addon list (each header once), and every row
  shows the quality ("1080p HEVC · Dolby Digital+ · cached") over the release name,
  instead of the addon's name and server. Fallback still tries them in its own order.
- **A sharper, richer hero.** Backdrops and logos on the TV are decoded at up to 1280
  px instead of 512 (posters stay mipmapped at 512), so the hero is no longer a small
  image stretched three times. The picture stays opaque down to the rows, and the side
  fade and text shading are lighter, so it keeps its colour.

## 0.7.2

- **Opening a title from Search works again on the TV.** Search kept the TV's text
  input switched on, and on webOS pressing OK then opened LG's own keyboard, which took
  the remote's focus: OK on a result showed that keyboard instead of the title. The
  TV builds no longer ask webOS for its keyboard (the app's keyboard and the phone page
  do the typing); the desktop preview still takes typing from a real keyboard.

## 0.7.1

- **Prayer settings take effect at once.** Changing the calculation method, the Asr
  school, the schedule or a custom time updated the *Today* line, but the breaks
  themselves kept the old times until midnight: switching Asr from Hanafi to Standard
  in Toronto still paused at 4:45 instead of 4:01. The day's times are now recomputed
  right after any change, and a prayer that already had its break today is not
  repeated.

## 0.7.0

- **Three themes of its own** (Settings > Interface > *Theme* and *Accent colour*),
  switched on the spot, six accents each:
  - **Signal** (new default): graphite, a lime accent, sharp 4 px corners, focus as an
    accent frame with a hard offset block, capital titles, mono metadata
    (`2012 / 95 MIN / MOVIE`), numbered row headings (`01 ── POPULAR MOVIES ── 14`).
  - **Marquee**: warm ink and parchment, projector amber, condensed display titles,
    a light bar under the focused card, amber kickers over each row.
  - **Duotone**: artwork printed in two tones that blooms into full colour when
    focused, soft capsules.
  All three are flat (no blur, no full-screen glows), so they cost less to draw than
  the old look. The Stremio logo keeps its own colours in every theme.
- **Next episode readiness**: the player's Next button fills as a ring (or the focused
  pill fills left to right, "Getting it ready") while the next episode's sources
  arrive, and lights up fully with a pulse when it can start at once. The Up Next
  card's button reads *Preparing* until then. Opening the control bar starts the fetch.
- **Snappier**:
  - Baked glyph atlases are cached on disk, so later starts skip the font bake.
  - No extra sleep follows a swap that already waited for the display.
  - Detail opens with the meta Home already fetched (memory, and on disk for an hour).
  - API requests no longer wait behind poster downloads; artwork goes newest first.
  - Posters are read from disk on two decoder threads instead of inside drawing, and
    the disk-cache tidy-up moved off the main thread.
  - The library cache is written when nothing is playing, not every 30 s during
    playback.
  - Screen transitions clip the page underneath, and text fitting/wrapping walks
    each string once.
- See `docs/OVERHAUL.md` for the plan, the performance audit and what comes next.

## 0.6.5

- Cleanup of the 0.6.3 and 0.6.4 code: no behaviour depends on a draw call any more
  (the *Back online* notice moved to the main loop), memory-saver reasons are named,
  one flag tracks subtitles picked by hand, and a few stale or misplaced comments are
  fixed. Matching sources to your audio language no longer uses two-letter codes
  (" it " and " de " are English words) and is safe for languages outside the alias
  table. The home helper fetches a page link and its title in one yt-dlp call.

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
