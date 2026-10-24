# Changelog

## 0.10.63

- **Living letterbox**: when a film's black bars are part of the picture (scope films on a
  16:9 screen, 4:3 shows at the sides), the bars glow with a soft blur of the scene beside
  them, like YouTube's ambient mode but in full screen, changing gently every ten seconds.
  Subtitles move down into the bar when there is room. Settings > Playback > Living
  letterbox; light effects turn it off, and if a frame cannot be read the bars just stay
  black.

## 0.10.62

- **Motion**: opening a title grows its poster out of the card into the page (Back shrinks
  it back); the focused card glows in its poster's colours; pressed past the end of a row
  or grid, the card springs that way and settles; Library, Explore and Search posters ease
  in one after another; the title page's backdrop settles while its facts, buttons and
  episodes rise in; the player's controls fade in from the edges and the 10-second skip
  bounces the way it seeks. A slow frame slows a move down instead of jumping it, and
  light effects still turn all of this off.
- **Your own ratings**: Rate on the title page, and Rate this episode in the episode
  options (hold OK). Half stars and quick tags ("Loved it", "Great ending"), kept for each
  profile and sent to Trakt and SimKL when they are connected.
- **Soundtrack** on title pages: the albums of a film's or show's music with their covers
  and tracks; OK shows every track with a code to scan to listen on your phone.
- **Guest stars** lead the cast drawer for the episode playing; **paused**, a line from
  Trakt's comments on it; **long seasons** get chips of ten episodes beside the season
  tabs; **See all** closes the Library row on Home.
- **Four new clock faces** for the screensaver: Station (the Swiss railway clock),
  Liquid (digits that flow into the next), Weather (the sky outside now, with the
  temperature; from Open-Meteo, no key) and Aurora.
- **The TV's own screensaver no longer covers the app's** fireplace or clock.
- **A new loading screen**: the backdrop as a wash of colour, the title's logo centred,
  what is being opened and how it is going.
- **Storage**: smaller caches, room always kept for the TV's own apps, the cache clears
  itself when the TV runs low, and Settings > About > Storage shows what the app keeps and
  clears it.
- **Rows show their posters together** once they are in, instead of one by one.
- **Phones (Android)**: lists follow the finger and flicks glide; videos always play
  sideways, with picture in picture (its button, or Home while a video plays); brightness
  and volume by sliding up or down either side of the picture, double tap a side to skip;
  the app updates itself; on the sources list the tags no longer run over the addon's name
  and no row shows under the recommendation.

## 0.10.61

- **On LG TVs nothing changes in this version**: it is the groundwork for Cuelight on Android
  phones and TVs (below), tested on the same code.
- **Touch screens** (Android phones and tablets): tap anything to open it (a card, a button,
  a setting, a source, a key of the on-screen keyboard), tap beside a pop-up to close it,
  swipe to move, hold for options. Choice settings step back from their left arrow.
- **Phone screens draw larger**: the layout fills the screen's width, so text and cards are
  about a quarter bigger on a 20:9 phone; Home, Settings, sign-in and setup fit its height.
- **Phones held upright get their own layout**: a tab bar along the bottom (Home, Explore,
  Search, Library, Settings), a feature card and rows of cards on Home, title pages as one
  long page (Play, then the actions, the story, seasons and episodes, cast, more like this),
  Search with the phone's own keyboard and filters for films and series, Settings as a list
  with each category on a page of its own, first-run setup, sign-in (a button opens
  Stremio's page on the phone itself instead of a QR code), What's new, Diagnostics, the
  PIN pad, profiles, Calendar (from Library), breaks and the screensaver. The player too:
  the picture across the top, the time and a scrub bar to tap, play/pause and 10 s each
  way, Skip intro and the next episode, and lists for subtitles, audio and speed, sources
  and episodes. Turn the phone (or tap Full screen) for the whole-screen player.
- **Android: videos play** (Media3 ExoPlayer under the app's own player screen): play, pause,
  seek, speed, volume, the file's audio and subtitle tracks, buffering and errors.

## 0.10.60

- **Settings > Accounts**: every account and key in one place, grouped by what it does
  (Sync: Stremio, Trakt, SimKL; Streaming: TorBox, the streaming server; Subtitles:
  OpenSubtitles; Artwork and ratings: TMDB, MDBList; the AI assistant), each saying whether
  it is connected. It replaces Account and API keys; OpenSubtitles moved here from Audio &
  subtitles.

## 0.10.59

- **Cuelight no longer needs Stremio's servers.** A built-in catalog (films and series from
  TMDB: Popular and Trending rows, genres, search, full title pages with seasons, episodes,
  cast and trailers) takes Cinemeta's place. It uses IMDb ids, so every stream and subtitle
  addon finds the same titles. It needs a TMDB key (built into a release when the
  `TMDB_API_KEY` secret is set, or your own in Settings > API keys); without one, Cinemeta
  stays.
- **Stremio sync is optional** (Settings > Account > Sync with Stremio). With it off, your
  addons, Library and progress live on this TV. Turning it off keeps everything (it used to
  wipe the Library); turning it on adds this TV's own addons to the account. First-run setup
  now starts with "Use Cuelight on its own".
- Sign-in: text typed on a real keyboard goes into the field that has the focus.
- **What's new** shows once after each update (this list), and **Settings > About** now says
  what Cuelight is, with this version's notes under it.
- Artwork a title is missing comes from TMDB, not Stremio's image service, when TMDB is on.
  With an OpenSubtitles key, new setups ask OpenSubtitles directly instead of through
  Stremio's addon; turning sync off drops Stremio's Local Files addon (it needs Stremio's
  own server).

## 0.10.58

- The releases repository is now **cuelight-releases**. TVs that added the old address keep
  getting updates (GitHub forwards it), and the app's own update check now asks the new one.
- If the app crashes, its last log line says so: the signal, where in the code, and which
  screens were open. Diagnostics shows it under Problems in the previous session (Blue).
- Search: a search whose results come in slowly is kept in your recent searches too, not only
  one you open a result from.

## 0.10.57

- **The app is now called Cuelight.** A new name, mark and colours of its own: the stage
  manager's green "go" lamp with play inside it, mint on deep teal. The TV's app list, the
  Homebrew Channel, the start-up animation, the side rail, About and Diagnostics all say
  Cuelight. Nothing else changes: it updates in place as before, and your sign-in, settings,
  profiles and library stay. Cuelight works with your Stremio account and addons and is not
  affiliated with or endorsed by Stremio (About says so too).

## 0.10.56

- **Every theme can do everything.** Aperture was missing a few things the other themes have;
  it now has them too:
  - details beside a card that stays focused (Settings > Interface turns them off in any theme);
  - trailers that play on their own on Home and on the title page, inside its artwork window;
  - on the Sources page, holding OK to get a torrent onto your TorBox (with its progress and
    "Ready" state), and the Verified, Same release and seeders marks;
  - "Source 2 of 5" in the player.
- **A new Search page.** The search bar, suggestions and a compact keyboard (the alphabet in
  order, seven keys wide) sit down the left; the best match is a large card on the right with
  **Play**, **Details** and **Sources** (a series opens its episodes), and the other results
  follow below it, films and series under their own headings. Before you type, the left side
  lists your recent searches.
- **Suggestions without the wait.** They appear on every letter, from titles this TV already
  knows (your library, the titles on Home) and your recent searches, then from what the
  addons find; the search itself starts sooner after you stop typing.
- **Better AI search.**
  - Two words are enough when they ask for a kind of thing ("korean thrillers", "90s anime",
    "like Inception"); a title is still searched as a title.
  - Up to 16 picks, each looked up once (in Cinemeta when it is installed) rather than in
    every addon, so they arrive sooner.
  - It says what it understood ("Hindi comedies from the 1990s") and why the top pick fits.
  - Follow-ups in the left list ("Only series", "Newer ones", "Lighter in tone"): one press
    asks again with your request kept, no retyping.
  - Titles you have watched are marked and come after the ones you have not.
- **Search types with the TV's own keyboard.** OK on the search bar now brings up LG's keyboard,
  with its own suggestions; what you type fills the bar and the results update above it as you
  go. Its Enter searches, Back puts it away, and the next OK opens the title as before (the
  keyboard comes up only while you type; before 0.7.2 it stayed on and took OK from the results).
  Settings > Interface > Type with the TV's keyboard switches back to the app's keyboard.

## 0.10.55

- **Pick another source when resuming.** Resume plays the link you watched last; the title page
  now has **Other sources** beside it (films and episodes alike), listing every source, and the
  one you choose starts where you stopped. In the player, a video resumed from its saved link
  had only that one under Sources; opening Sources now asks every addon for the rest straight
  away, while it plays on.

## 0.10.54

- **Links that only open in another app are no longer listed.** Some addons list a source that
  only opens elsewhere (a Netflix page, say); the TV can't play those, so they are left out of
  the sources and never tried.
- **Get a torrent onto your TorBox from the sources list.** When a title is only available as a
  torrent nobody has cached, hold OK on it: your TorBox starts downloading it, and the row
  follows along (percent, speed, a bar along its foot; "waiting for seeders" when it has
  none). Once TorBox has it, the row says "Ready on your TorBox" and OK plays it from there,
  at TorBox's speed instead of the few peers'. Leave and come back later: it carries on and
  shows as cached. Needs a TorBox key (Settings).
- **Suggestions while you type in Search.** Like the TV's own keyboard: a row above the keys
  offers your matching recent searches and the titles found for what you have typed so far.
  Up from the top row of keys reaches them, OK searches that title.
- **The title you typed comes first in Search.** Results used to be shown in the order the
  addons answered, so an addon that answers every search with its whole catalog (new
  releases, say) pushed the real match far down. Now the exact title leads, then titles that
  start with what you typed, then those with all its words, then some; titles that share no
  word with it go last. Movies or series, whichever group holds the best match, comes first.
  The result you are on stays focused while later answers arrive.

## 0.10.53

- **A source that dies mid-film is replaced without a pause.** With Load ahead on, the app
  notices a source has stopped long before the picture would, and acts while minutes of video
  are still in hand:
  - If another of your sources is the very same file (the same release from another service,
    or the addon's own link again after the address it led to expired), the download simply
    carries on from it at the same byte. The player never notices: no pause, no reload. A
    copy is only used once it is proved identical, byte for byte against what is already
    loaded, never by its name alone.
  - If there is no identical copy, it keeps retrying the source while the buffer plays, so a
    short outage passes unseen. Only when about 20 seconds are left does it switch to the
    next source at the same moment (a brief reload instead of a stall).
  - The lighter-copy switch for a connection falling behind now counts the whole buffer, not
    only the TV's own 30 seconds, so it steps down in quality only when the minutes in hand
    are really running out.
- Load ahead always keeps at least 16 MB ready, even when a source's size suggests a very low
  bitrate (it could stall before the first picture).

## 0.10.52

- **Load ahead: up to 2 minutes of the video kept ready.** The TV's player only holds about
  30 seconds; now the app downloads ahead of it into the TV's memory, so a slow patch or a
  dropped connection doesn't stop the picture. If the source drops, the download picks up
  from the byte it stopped at. The "ahead" pill shows minutes.
  - Its size follows the TV's memory as it plays. Memory is taken in 4 MB pieces as the
    download fills them and given back as soon as they have been watched, and every few
    seconds it checks how much the TV has free: a fifth of that, at most 192 MB (64 MB on a
    1 GB TV). With more free it reaches further ahead, with less it holds less, and with too
    little (or a low-memory warning from the TV) it lets go entirely and the video carries
    on straight from the source. Nothing goes to the TV's storage (some have under 1 GB in
    all).
  - It gives its memory back when the app goes to the background, when you stop, and when
    the app closes. Closing never waits on a slow or stuck server: every download stops
    within a second (a stuck source used to hold closing up for 7 seconds).
  - If the source keeps failing, the buffer lets go and the video carries on from the
    source; if the source itself refused the file, the next source is tried at once instead
    of the same dead link a second time.
  - Seeks to the very end of a file, a quick check by the TV's player while another read is
    under way, and odd byte requests are all answered correctly, from the buffer or the
    source.
  - Settings > Playback > Load ahead turns it off.

## 0.10.51

- **Subtitle files really as big as the video's own.** Measured on the TV, its Medium is about
  80 px type, not the 56 px 0.10.50 used. Files are now drawn at 64, 80, 100 and 120 px for
  Small to Extra large, and long lines wrap onto a second line instead of being cut short
  with "...". The Settings preview shows them in proportion.
- **No sample line over the video's own subtitles.** Changing Size or Style in Look and timing
  showed "Subtitles will look like this" on top of the TV's subtitles; it now shows only when
  the app draws the subtitles.

## 0.10.50

- **A video's own subtitles first again.** 0.10.49 put a subtitle file matched to the video
  ahead of the video's own track, for your style; but such files can run a little early or
  late, where the video's own track is always in sync. The video's own track is first again;
  "Subtitle files over the video's own" (Settings > Audio & subtitles) brings the files first
  for those who want their style more than exact timing.
- **Subtitle files as big as the video's own.** At the same setting the TV drew a video's
  own subtitles a step bigger than the app drew files from addons; files are now drawn that
  size too (Small to Extra large: 40, 56, 72 and 96 px on a 1080p screen), so switching
  between the two keeps the size. (The TV draws its own inside the picture, in its own font,
  so they can still look a little greyer.)

## 0.10.49

- **AI search keeps to what you ask for.** "Bollywood comedies from the 90s" now brings
  Hindi comedies of the 90s, not every title with one of your words in it:
  - The assistant is told to respect language or film industry (Bollywood, Tollywood,
    K-drama, anime), country, era, genre and film or series exactly, and suggests up to 12.
  - Each suggestion is found by its name, its year and whether it is a film or a series, so
    a remake or a namesake no longer stands in for it.
  - A request no longer searches your addons word by word as well (that brought unrelated
    Korean and English titles); it does only when the assistant has nothing.
  - With a TMDB key (Settings > API keys), the assistant's filters (original language,
    country, genres, years) also fetch TMDB's most popular titles that fit, after its picks.
- **Your subtitle style on more videos.** When an addon has a subtitle file matched to the
  video you're playing (the MATCHED chip), it is now chosen over the subtitles inside the
  video. The app draws those files itself, so size, style, colour, position, timing and lines
  meant for the top all apply; the TV draws a video's own subtitles its way and may ignore
  them. A video's own track you choose for a show is still remembered and kept.

## 0.10.48

- **See the subtitles while you change them.** Opening Look and timing in the Subtitles
  drawer turns the drawer into a narrow panel on the right with every setting, the
  subtitles moved a little left beside it, and the controls hidden. When nobody is speaking a
  sample line shows the size, style and position. Back brings the drawer back; the two
  slide and fade into each other.
- **Skip intro skips on the first press.** With the controls on screen, OK paused the video
  instead (the button needed Up first), so a second press was needed to carry on. Now OK
  skips whenever the button shows, unless you have gone down into the controls, and the
  episode plays on even if it was paused.
- **Subtitle lines marked for the top go there.** A line written to sit above the show's
  own credits or signs ({\an8}, or WebVTT's line:0) now goes to the top, under the title
  while the controls show, and two lines shown at once both appear. This is for subtitle
  files from addons; the subtitles inside the video are drawn by the TV itself, which shows
  such marks as text: choosing an addon file once in the drawer is remembered for the show.
- **A smaller Subtitles drawer.** It covers less of the picture while you pick a language.
- **The UI scenarios run on GitHub.** Every push runs them; a release waits for them.

## 0.10.47

- **Subtitle settings are easy to find.** In the player's Subtitles drawer, Look and timing
  (size, style, position, timing, auto-sync, second subtitles) is now at the top, above Off,
  instead of under every language. It opens on your language as before; Up twice reaches it.

## 0.10.46

- **A more accurate recommended source.**
  - Names are read more carefully: "1080p.UHD.BluRay" and "4K Remastered ... 1080p" are
    1080p; Comet's ⬇️ means not cached; Atmos on a streaming release is DD+ (TrueHD only on
    discs); screeners, PreDVDRip, HQCAM, R5 and telesyncs named WEB are cinema copies;
    10-bit H.264 (no TV decodes it) and burned-in subtitles count against a source.
  - Dubs: with an audio language set, a source that names only other languages (in words,
    or Torrentio's flags) counts against it; a flag of your language counts for it.
  - Dolby Vision with an HDR10 layer plays as HDR on a TV without Dolby Vision, so it is no
    longer treated as Dolby Vision only; a check of the file no longer turns HDR off when
    the file just doesn't say.
  - A link that failed in the last hours is recommended only when nothing else is; a
    source cached on TorBox counts like any cached copy, not above a better one.
  - The next episode keeps to the same release only while that copy works: not an
    uncached or failed one over a cached one.
  - The connection speed is measured from the file's first byte (not the wait for a debrid
    link), so 4K files no longer look too heavy for a fast connection; the exact file size
    an addon gives beats one in the text (often the whole torrent's), and a file far too
    small for its resolution counts against it.
  - The TV profile needs two failures before it avoids something (three for HEVC and 4K),
    and features this TV already played are not blamed.

From an audit of whole evenings of use rather than single features:

- **Still watching?** After three episodes have started by themselves with no key pressed,
  the next one waits for an answer at the end of the episode: OK plays it, Back stops.
- **Watched means watched.** An episode is marked watched when you played it to the end,
  not when you seek there to peek (Trakt and SimKL too), and on the title page only the
  episodes you finished are ticked, not every one before where you are. Shows watched
  elsewhere keep their ticks, in every season, until their first episode is finished here.
  Moving on from the credits with Up Next finishes the episode.
- **Nothing gets stuck waiting.** A next episode with no source that may start by itself
  says so and lets the screensaver come, instead of spinning all night; an episode that
  ends with a menu open goes on to the next; OK on a preparing Up Next card is kept even
  with autoplay off.
- **The update prompt stays out of the way.** It no longer opens over a list starting a
  video, the screensaver, search or settings; *Later* is the first choice, and an OK
  pressed as the prompt opens is not taken as an answer.
- **The TV's Home and breaks.** A video paused while it loaded (the TV's Home, a break)
  stays paused; breaks, chimes and the fireplace screensaver don't start while the app is
  behind the TV's Home; a break during loading or a reconnect comes back to the right
  spot, and bedtime with a recording leaves the film ready to play, paused. TorBox
  answering during a break waits for it to end.
- **Your place survives hiccups.** Reload, a source picked in the player or Yellow during a
  reconnect go on from where the film was, keeping the audio and subtitles you chose;
  Resume on the title page opens the source that actually played; leaving while a source
  loads still saves the spot; progress is written to the TV every five minutes while
  playing (with no account it was only kept in memory).
- **Restart** plays from 0:00, even with the episode playing in the corner.
- Smaller: Play on a source list plays that source; a quick double OK no longer starts a
  source unseen; the corner player no longer takes Play from a title page or from a
  break, and stays off the screensaver; a show playing in the corner keeps its Continue
  Watching place while you browse; Back after Up Next returns to the title page; the TV's
  volume overlay no longer tells Trakt playback stopped; Play's fallback never picks a
  file named for another episode; a prefetch made with other addons (or another profile)
  is not reused; the stretch reminder counts playing time only.

## 0.10.45

- **Fixed: the next episode could fail to start, or the app could close.** When the next
  episode started from a source race, the screen it replaced was cleared a moment later
  and called off the new episode's start; that could also free what the player had just
  been given, closing the app. A start is now only called off by the screen that made it,
  and a source found while something covers its screen waits for the screen to be back.

## 0.10.44

- **Up Next waits until the next episode is ready.** Its button reads *Preparing*, filling
  as the next episode's sources arrive and its best source is checked, and turns into
  *Play now* when it can start. OK while it prepares starts it the moment it's ready
  ("Starts once ready"), and a countdown that runs out first waits on the card while the
  credits play, so the source list no longer shows in between.

## 0.10.43

- **The next episode starts even when the screensaver is due.** When the Up Next
  countdown ended, the app's screensaver came on at that instant (nothing was playing
  for a moment, and no key had been pressed for an hour), and closing it left the
  sources waiting for a choice. The screensaver now waits while the next episode is on
  its way, the TV's own screensaver is held off then too, and if anything else covers
  the sources meanwhile, the episode still starts by itself.
- **Sources that need request headers now play.** Some addons hand out links that only
  answer when asked with a particular header (a Referer, a cookie), which the TV's
  player can't send. Those now play through a small relay inside the app: the player
  opens a local address, and the relay fetches the source with its headers. Only this
  TV can reach it, and its address never carries the link or a key. It is used for
  those sources alone, and when it doesn't get one playing the link is opened directly,
  as before.

## 0.10.42

- **Sources start faster.**
  - Resting on an episode or a film now also checks the source Play would start, so
    Play has nothing left to check.
  - A source checked in the last minute plays without the race to see which link
    answers first.
  - The player opens the file a debrid link redirects to (found by that check moments
    before), skipping the hops; if that address has expired, it opens the link itself.
  - Play no longer waits for slow addons once the best source so far is ready on
    debrid or your TorBox at your quality cap. Their answers still join the list.
- **How long each start took** is on Diagnostics > Speed: the last five, from the
  press to the first frame, split into waiting for sources, the check, the race and the
  player.
- **Card details wait longer.** The panel beside a focused card opens after 1.5 s instead
  of 0.6 s, so moving along a row no longer opens it on the way.

## 0.10.41

- **OK on an episode shows its sources, and nothing starts until you choose one.** It
  used to start the best source by itself after a few seconds, so a list you were still
  reading could start playing. OK on a series' main button does the same for its episode.
  The remote's Play button still starts the episode at once on the best source.

## 0.10.40

From a third review:

- **No guessing between episodes in a TorBox pack.** Two unnamed files of a like size
  played the larger; that could be the wrong episode, so another source plays instead.
  A torrent with one video, and the file an addon points to, are checked too: named for
  another episode, they don't play.
- **Late subtitle downloads are dropped.** Only the newest download for each subtitle
  slot is used, so a file asked for earlier (or before subtitles were turned off) can't
  replace the current one; the timing reference and the best-fit comparison belong to
  the source they were made for.
- **The start race reads enough to tell a page from a file** (4 KB, not 2 bytes), and
  plain-text answers ("Not found", "Link expired") and pages served with no type lose.
- **A failed source teaches the TV profile only when one feature explains it.** One
  needing several (say Dolby Vision and TrueHD) is a failed link, not two bad decoders.
- **Subtitle matching by file hash needs both ends of the file.** A server that ignores
  the request for the end sends the start again; that hash looked right and matched
  nothing, and now the hash is left out instead.

- **Updates are checked when the app opens.** The first check came 20 seconds after a
  start, and none when the app came back from the TV's Home (webOS keeps it running
  behind it). It now checks a few seconds after opening and again each time it's back on
  screen, and a "Later" is asked once more then.

- **A source list you're choosing from no longer starts by itself.** OK on an episode
  opens its sources and starts the best one after a moment; moving in the list now
  stops that, and the header says so ("Up or Down to choose"). A choice made while the
  quickest of the top sources was still being found was dropped and the automatic one
  played; the choice now wins, and Back calls the search off too.

From a review of how sources are chosen and played:

- **Answers have a size budget.** A check asks for a few kilobytes of a file, but a server
  may ignore that and send the whole file; it was all kept in memory. Every answer now
  stops at its budget (what was asked for, or 64 MB at most).
- **One gate for every automatic choice.** The recommendation, Play, the next episode, the
  sources queued behind the one playing, the start race and asking the addons again each
  had their own rules, some only lowering a score. Now a source that is a notice, cannot
  play here, does not answer, breaks your rules or is named for another episode never
  plays by itself (it stays in the list, marked, for you to pick). Episode ranges
  ("S01E01-E08") count as holding their episodes.
- **A season pack never plays a file that says it is another episode.** When the pack
  names its episodes and this one is missing, or several files could be it and nothing
  tells which, the next source plays instead of the largest file.
- **Late answers about an earlier source are dropped.** Switching source now starts a new
  attempt; a file header, file hash, matched subtitles, TorBox link or backup check that
  arrives for the source before is ignored instead of applied to the new one.
- **Audio this TV can't decode is fixed by itself.** Instead of asking you to pick another
  track, the player switches to one (the same language first); when no track plays, it
  moves to another source at the same moment.
- **The TV profile learns only from decoder failures.** A network drop, an expired link or a
  TorBox error no longer counts as "this TV can't play HEVC / DTS / ...".
- **An error page is not a video.** A link answering 200 with an HTML or JSON page ("link
  expired") loses the start race and fails its checks.
- **What an addon says about its stream is kept:** the request headers it needs, the file's
  hash (subtitles matched to that exact file, without working it out) and the subtitles it
  sends with the stream. The TV's player can't send request headers, so a source that asks
  for them is checked as the TV will open it (without) and ranks after the others.
- Also from a second look: a number after the episode in a name ("24 S01E01 12 00 A M") is
  no episode range; the switch back from P2P to a cached copy goes through the same gate;
  subtitles matched to one source's file no longer count as matched after a switch; a
  YouTube file cut short is not saved as whole; the TV profile learns from Tizen player
  errors again; a pack with a single stray "other episode" file, or two unnamed files,
  plays as before.

## 0.10.39

- **A Cast button in the player.** The full cast opened only with the Info key, which most
  LG remotes do not send to apps (the Magic Remote has none; on the standard remote the TV
  can keep INFO for itself), so "Info for all" led nowhere. The control bar now ends with
  a Cast button, and the paused screen points to it. The standard LG remote's INFO key
  (it arrives as key 499) and GUIDE open it too, and its Stop key (120) now stops.
- **No error card while the player can still fix it.** When every source in the list had
  failed it showed "This source can't be played" at once, though it then asked the addons
  for fresh links after 8 seconds. Now it asks after 2, shows what it is doing
  ("Reconnecting", "Finding a fresh link", "Trying another source") instead of an error,
  and the card only comes up once every source, retry and fresh link has failed. A link
  that fails to open with a network error (code 300) is tried once more first; switch
  notices no longer show the raw error (it stays in Diagnostics).
- **Your TorBox offers only this episode of this show.** The show's name was looked for
  anywhere in a file's name, so "Ted" took Ted Lasso's S01E05; it must now be the whole
  name (followed by a year, the episode, a quality tag or nothing). And a file numbered as
  this episode but titled as another one of the season (services number some episodes
  differently) is left out: "Ted.2024.S01E05.Subway.Bicycles..." is not "Desperately
  Seeking Susan". A file of another episode inside a TorBox item named for this one was
  already left out in 0.10.38.

## 0.10.38

- **No more freeze while sources come in.** With many addons answering (a few thousand
  sources), the list was re-sorted with a comparison that looked through the TorBox and
  dead-link lists every time, for every answer: up to six seconds of a frozen screen on
  the TV. Each source's rank is now worked out once and the list sorted in one go
  (4,000 sources: from about 930 ms to under 20 ms on a desktop, several times that on
  a TV). A sort that still takes long is written to Diagnostics.
- **The Diagnostics QR code scans easily now**: instead of packing the log lines into a
  dense code (hard for a phone to read off a TV), it is a short link to this TV, and the
  phone opens the whole log as text to copy. It works on the same Wi-Fi, only while the
  code is on screen, with a new random link each time; without the phone server the old
  code (smaller now) is shown.
- **The next episode is ready sooner**: its sources are fetched from 60% of an episode
  (or its last 15 minutes) instead of the last 5, so Next pressed early starts quickly
  too; and the freeze above no longer hits when it takes over the fetched list. When
  it starts, Diagnostics says whether its sources were ready.
- **Home shows the scores too**: the hero has the same strip of score pills as title
  pages (Tomatometer, Popcornmeter, Metascore, Roger Ebert, Letterboxd, Trakt, TMDB and
  MDBList's ring), the genres moving into the facts line.
- **The right episode from a season pack.** A torrent holding a whole season, opened
  through TorBox with no file named by the addon, played its largest file, which could be
  another episode (episode 1 playing episode 2). It now plays the file named for the
  episode. Your own TorBox library also matches each file's name, not the pack's.
- **The next episode stays on the release you picked** when the files are named
  "Show - S01E02.mkv", as in many season packs: the release group (PSA, NTb...) is read
  from the torrent's name then, and it counts from any addon that lists it.
- **Season packs: the right file however it is named.** Besides S01E02 and 1x02, the
  file is found by "S01 E02", "Season 1 Episode 2", a season folder with "02 - ..." or
  "Episode 2" inside, the episode's title, or (anime) its number counted from the first
  episode; a file named for another episode is never taken for it. When the names
  leave several (a disc rip's title_t00, title_t01...), your AI (Settings > API keys) is
  shown the list and picks; without it, or if it is unsure, the largest file plays.
- **Update now works on more TVs.** The app asked the Homebrew Channel to install the
  update over its own system-bus connection, which webOS can refuse (the app's
  permissions do not name that service): "Update failed". When that happens before
  anything downloads, the same request now goes through luna-send-pub, the public tool
  any app may run, and its progress shows as before.
- **Fixes from a review of this release:** a video's own subtitle track picked by hand is
  remembered by its real language (an unlabelled one could turn subtitles off for the
  rest of a series); reconnect tries follow one another even when one fails; anime and
  TMDB episode ids are read right when choosing a season pack's file, and the AI only
  chooses among equally named files (its answer kept); the next episode's answers are
  renewed before its file is warmed, not after; a TorBox source made ready ahead is
  matched by its own link; the Subtitles sheet keeps its language while files arrive and
  is no longer rebuilt every frame; the screensaver may come over an error card again;
  the TV's screensaver request is subscribed to once; the update progress reads the
  installer's latest replies.
- **The TV's own screensaver stays off during films.** A video played under the app's
  window does not count as "playing" to the TV's power service, so LG's screensaver could
  come on in the middle of a film (about every 20 minutes). The app now answers the TV's
  screensaver request (as nuvio-native-legacy does): held off while a video plays, loads
  or stalls (also when the pipeline reports a stall as a pause), let through when you
  paused it, it ended, or nothing plays.
- **The next episode is prepared early.** Its sources were asked for at 60% of the episode
  (or in its last 15 minutes) or when the control bar was open, and the answers counted
  as fresh for only 10 minutes, so on a long episode they could be gone by the end. Now
  they are asked for half a minute into the episode, kept for hours, and asked for again
  in the last 8 minutes when over half an hour old, so the links are fresh for Up Next.
- **A dropped connection reconnects to the same source.** After a video has played, a
  network error (webOS codes 300 and 40000-49999) reloads the same source at the same
  moment after 3, 10 and then 25 seconds, before moving to another source; a source is
  not given up because the Wi-Fi blinked.
- **The screensaver no longer covers a video.** It came on whenever the video was not
  reported as playing: stalled (webOS reports a stall as a pause) or between two sources
  too. Now it only comes over a video you paused yourself; nothing else but a break takes
  over the player, full screen or in the corner.
- **The next episode always starts by itself.** It waited for an addon to answer before
  checking whether the wait was over, so with an addon gone quiet it never started; and
  it only looked at the top source, so a notice or a link this TV cannot open at the top
  stopped it. Now the wait is checked all the time (20 seconds at most, even set to wait
  for every addon) and the best source that can play here starts.
- **Two sources stand by.** While a video plays, the next source in line was checked so a
  switch is quick; now the one after it is too (one after the other, never competing with
  the video). A torrent on TorBox among them, skipped before, is made ready on TorBox
  ahead (its file found and linked), so when the playing source stops the next takes over
  at once from the same moment; with a backup known to work, the switch comes after 12
  seconds of stalling instead of 20.
- **Subtitles grouped by language.** The player's Subtitles sheet now lists languages on
  the left (yours first, then the ones in the video, then the rest, each with how many
  choices it has) and that language's choices on the right: the video's own tracks
  marked IN THE VIDEO, addon files with their format (SRT, VTT...) and MATCHED when made
  for this file, and the AI translation under the language it makes. OK on a language
  takes its first choice; Right shows them all. Timing, size, style, position, auto-sync
  and second subtitles moved to "Look and timing" at the end of the list.
- **Subtitle colour, for accessibility** (Settings > Audio & subtitles): the caption
  colours TVs offer (white, yellow, cyan, green, magenta, red, blue, black) over any
  style; blue and black letters get a light outline or box. It reaches the video's own
  subtitles too, and the player's subtitle menu shows it next to the style.
- **Four more subtitle styles**: Bright (heavier letters in a wide dark outline, for
  bright pictures), Raised, Soft box (a light see-through box) and Cyan. They apply to
  the video's own subtitles too, and those are now set to full opacity, so a TV that
  starts them dimmed shows them white.
- **The video's own subtitles win again** when the file's only text track gives no
  language ("und"): it is picked over an addon's, and remembered as your choice.
- **No more `<font>` and `<i>` tags in subtitles**: tags written out as text (escaped,
  as some files carry them) are removed too.

## 0.10.37

- **Title pages show the scores and the advisory too**, not only the paused screen: the
  Tomatometer, Popcornmeter, Metascore and Letterboxd marks beside the genres, and a
  line of what the title contains ("Violence Severe · Profanity Moderate").
- **The trailer's "Sound off" pill moves out from under the island**: it sits just below
  the clock and slides down when the island grows into a notice ("Press Back again to
  leave"), instead of being hidden behind it.
- **The ratings, redone**: on the paused screen one see-through card on the right, level
  with the details on the left: MDBList's score as a ring on top, then the scores in two
  columns (mark, name, value) and Trakt's numbers under "On Trakt". Title pages show the
  scores as a strip of pills, the genres moving into the facts line. When the cast row
  runs out of room it ends on "+4 more · Info for all" (the Info key opens them all).
- **More scores**: Roger Ebert (a gold star, out of 4), Trakt's and TMDB's ratings, and
  MDBList's own blend of them all as a ring that fills in green, yellow or red.
- **The left menu is centred**: its sections sit in the middle of the screen, between
  the logo and the account button, instead of bunched under the logo.
- **The Popcornmeter shows**: MDBList calls the audience score "tomatoesaudience",
  which the app did not read (it looked for "popcorn"); both names count now.
- **The film comes back properly after a break.** A paused film no longer restarts from
  0:00 after the azaan: it returns to where it was, still paused. The screensaver no
  longer opens over a break (it froze the countdown, so the film never came back by
  itself), and a fireplace video waits for the break to finish instead of replacing it.
- **Resume points survive a dead link.** When the first source fails before it plays
  (an expired debrid link, a timeout), the next one starts at your resume point instead
  of the beginning, so the account's progress is no longer reset to the start.
- **No more sudden exits** when the TV's player hangs up on the app's own server, for
  example leaving the YouTube screensaver.
- **Trakt and SimKL renew calmly.** A renewal that fails (offline, the service down) waits
  2 minutes, then longer, instead of retrying every frame; a token that lasts a day is
  renewed once, halfway through, instead of every second.
- **Profiles stay apart.** A library reply for the previous profile no longer lands in
  the new one's file (from where it was merged into and uploaded to that account), and
  progress kept from this TV is saved to flash.
- **Fewer crashes on Back:** replies held over to the next frame can be cancelled, and
  Search drops the replies for a closed search or an older query, while a search left
  open under a cast member's films still gets its own (its spinner no longer runs on).
- A fireplace video that keeps stopping gives way to the drawn fire after a few restarts.
- Longer audio and subtitle language tags (e.g. `zh-Hant-TW`) are kept on a source switch,
  and repeated retries reset once a source has played well for a while.

## 0.10.36

- **What critics and viewers make of it, in the paused screen's corner**, as score
  badges with their own marks: Rotten Tomatoes' Tomatometer (a tomato, or a green
  splat when rotten) and Popcornmeter (a full popcorn bucket, or tipped over), the
  Metascore in Metacritic's green, yellow or red box, and Letterboxd's three dots (with
  an MDBList key); below them Trakt's viewers: a live dot for how many are watching it
  right now, how many have watched it, and how often.
- **Content advisory when a video starts**, as streaming services show: a line for
  each thing the title contains and how much ("Violence · Moderate", "Nudity · Mild":
  violence, profanity, frightening scenes, nudity, alcohol and drugs), from IMDb's
  parents guide, strongest first, in the top-right corner. It appears once the starting controls step aside and fades after a few
  seconds; Settings > Player > Content advisory turns it off.
- **The remote's Play button works on LG webOS 10**: it arrives with a code the app
  didn't know (found with the new unknown-key log in Diagnostics); it now plays on
  title pages and resumes a paused video.

## 0.10.35

- **Four new screensavers** (Settings > System > Screensaver style), drawn live and
  light enough for old TVs, always moving so OLED panels are safe:
  - **Aurora**: northern lights rippling over a starry sky and a line of pines.
  - **Rainy window**: your titles' artwork blurred behind wet glass; drops gather, dry,
    and run down in fits, each showing the view upside down as real drops do.
  - **Now showing**: a cinema's velvet curtain and a poster frame lit by chasing bulbs;
    your titles go up one after another, with what's new ("2 new episodes are out") or
    where you left off.
  - **Warp**: flying through a starfield past drifting nebulae.
- **Six new clock faces** for the Clock screensaver: **Segment** (a red LED alarm clock,
  the unlit segments faintly there, a PM lamp), **Dot matrix** (amber LEDs like a
  departure board, the seconds running along the bottom), **Neon** (a pink sign on a
  brick wall that now and then flickers), **Rolling** (odometer drums, the seconds rolling
  over), **Orbit** (the hour, minute and second as planets round a glowing sun) and
  **Daylight** (the sky of the hour, dawn to night, with the sun or moon crossing it).
- **The paused panel says more and sits centred**: it is centred on the screen
  (clear of the paused line), shows this episode's own synopsis, the genres, the picture's quality (4K, 1080p, HDR,
  Dolby Vision) and what's up next; the paused line adds when it would end if played on.
- Signed out, the default addons keep their listed order instead of the order their
  manifests answered in, so Home's rows and the Sources panel's addon tabs no longer
  change places between launches.
- **The remote's Play key resumes playback** on LG TVs whose remote reports Play only by
  its key position (Pause worked, Play did nothing). Keys the app doesn't recognise are
  now written to the log (Settings > Diagnostics) so any other button can be mapped.
- **Updates on TVs that won't let apps install**: after the TV refuses the Homebrew
  Channel installer once, the app stops offering an Update now that can't work. The
  popup (once per version) and Settings > About > Update show three steps to update from
  the Homebrew Channel instead, with OK there to try installing from the app anyway.

## 0.10.34

- **Episodes play straight away** from a series' page: the main button reads "Play S1 E1"
  (the episode in focus below it) and starts it, and OK on an episode card starts that
  one, on the recommended source. Back from the video returns to its sources; holding
  OK on an episode still offers the list of sources.
- **The remote's Play button plays** on a title page: the episode in focus, or what the
  main button does (Play, Resume) for a film, starting at once. A trailer behind the
  artwork there no longer takes the key for its sound.
- Playing straight away waits only for the best source's link check (at most 5 s), not
  for every check.

## 0.10.33

- **Faster on old TVs**: the network workers now share DNS answers and TLS sessions
  (a server one has reached is resumed by the others without a new handshake, the
  slowest part of a request on an old TV's CPU), keep DNS answers for ten minutes and
  keep idle connections alive for reuse.
- **Memory optimizer**: with the memory saver on (1 GB TVs, or when memory runs short)
  posters decode at half size, about a quarter of the memory each and still sharper than
  their cards; while a video fills the screen the artwork cache drops to 24 MB so the
  TV's decoder has room, and comes back after; freed memory is returned to the system
  instead of being kept by the app. TVs with room keep everything as before.
- Aperture shows a title's tagline from TMDB (with a TMDB key) on the Home hero and the
  title page, as its mockups do: "Fear can hold you prisoner. Hope can set you free."
- Aperture's artwork drifts slowly across the hero and the title page (a minute each
  way, about eight frames a second; off with Light effects or low memory).
- Aperture's corner clock is bare type, without the pill, on every screen and in the
  player; notices and progress still take the pill.
- Aperture's Up Next card: a coral "Up next", the episode in the serif, the countdown
  as a thin line under the still, and an ivory Play now.

## 0.10.32

- **Colour palettes** (Settings > Interface > Look): six ready-made pairs of an accent and
  a background, switched with Left/Right and shown as swatches on the row; Accent and
  Background below still mix your own. In Aperture the preview beside them follows its
  mockup: "Live preview", a small Home with the title over the artwork and the tab bar,
  and the palettes as tiles with the one in use framed.

## 0.10.31

- Fix an out-of-bounds read when a title from an addon ends partway through a UTF-8
  character: upper-casing it (Aperture titles, Marquee and Signal headings) read past
  the end of the text.

## 0.10.30

- **Aperture now matches its design mockups**, set in a new serif (DM Serif Display):
  Home opens on full-bleed artwork with the title in large serif capitals, the year /
  runtime / genre line, Watch and More details, and rows of landscape stills titled in
  spaced serif capitals with a "02 / 14" counter. Title pages share that composition,
  with the cast as portraits on films, season tabs underlined in coral and larger
  numbered episodes. The player shows the title in spaced capitals, the time, a coral
  line and the time left on one row, centred controls named under the focused one,
  chapters as dots, and a lower-left card when paused; drawers, Search ("Find your next
  film", borderless keys) and Settings take serif headings and an outlined focus.
- **New episodes come back to Continue Watching**, as in Stremio's own apps: a series
  you had caught up on returns to the front of the row when an episode airs after you
  last watched, with a "New episode" (or "3 new episodes") badge, and a bell with the
  count on its poster in My Library. Its title page opens on that season with "Play
  S2 E5 · New". Removing it from Continue Watching keeps it away until a later
  episode airs; shows with notifications turned off in Stremio are left out.
- **Back after a while**: resuming something left more than a day ago starts 10 s
  earlier (30 s after a week), and a series left for two days or more opens with a
  "Previously" card holding the last episode's summary (OK or Back puts it away).
- **Blue replays the last 10 seconds**, with subtitles switched on for just those
  seconds when they were off; they go off again once you are past where you pressed.
- **Info shows who's in it**: a strip of the cast with photos and characters over the
  picture, while it keeps playing (TMDB's cast with a key, else the addon's).
- **Scenes** on the control bar: a rail of the file's chapters (or every 5 minutes)
  with previews; OK jumps there. Its first card marks the current moment, and marked
  moments stay with the video, show as dots on the timeline, and Yellow removes one.
- A health chip next to the quality: where the source plays from (Cached, Direct) and
  how many seconds are loaded ahead, amber when that runs low.
- In the last ten minutes, once the next episode's sources are in, the first
  megabytes of the one Up Next will most likely play are fetched, so it starts faster.
- Films with a scene during or after the credits (TMDB's keywords) say so when the
  credits start, and Up Next waits for the very end.
- Second subtitles chosen for a show come back on by themselves for its other
  episodes, for watching with two languages.
- **Night mode** (Settings > Playback): Off, 11 pm to 6 am, or Always. Videos play
  quieter and subtitles one size larger.

## 0.10.29

- Add the selectable Aperture prototype: charcoal and ivory editorial Home and
  title pages, compact source comparisons, quiet player controls, a pause title
  card, cached seek filmstrip, and subtitle/audio side drawers. Existing themes,
  remote actions, playback, and saved theme IDs are preserved.
- Add theme contract tests and a fixture-backed native journey with screenshot
  artifacts, including Back navigation, reduced motion, and low-memory seeking.
- Aperture's Sources page shows the same badges as the other themes (quality, size,
  cached), "In your TorBox" / "From your TorBox", and the actual reason a source
  cannot play here ("needs a streaming server", "opens in an external app") instead
  of one generic line. "Too fast for this link" now reads "Heavier than this
  connection" everywhere.
- Aperture's player: the focused control's name moves next to the hint at the right,
  so it no longer covers the elapsed time; plainer headings ("Home", "Paused",
  "Choose a source").
- Settings says "Not in the Aperture theme" beside background trailers and card
  details, which that theme does not use, instead of showing switches that do nothing.

## 0.10.28

- **A new Sources sheet during playback**, laid out after nuvio-native-legacy's: a tall
  card on the right while the video keeps playing beside it. The source this TV should
  play is pinned first, then 4K / 1080p / 720p / the rest (HDR before SDR, the largest
  file first) under their own headings; each row shows the quality in words, its file
  size in big figures and the addon, the playing one carries a moving equaliser, and
  the focused row opens to show its file. Addon tabs with counts narrow it (Left/Right
  anywhere), and Find more / Close sit at the top (Up from the first row).

## 0.10.27

(0.10.26 never reached the Homebrew Channel; its changes are here.)

- **A safer account repair.** An addon that misses one start (slow network at boot,
  rate limits) is asked again on later starts and saved once it answers, instead of
  staying blank. Manifests that leave out optional fields (description, catalogs) are
  accepted, so a working addon is never blanked for that, and they no longer block saving
  your addons. A repair for one profile can't swallow another profile's addons, and a
  field stored with the wrong type is replaced instead of duplicated.
- **Trailers keep their hands off.** Pressing Play while IMDb was still answering for a
  background trailer could start the trailer over your film; a trailer now starts only
  if no other video began meanwhile.
- **TorBox sources fail cleanly.** When TorBox can't open the last source, the error
  card shows instead of an endless "Getting it from TorBox", and whatever played before
  stops while TorBox answers.
- **No wait for addons that stay down.** When only addons left blank earlier are asked
  again, your collection is used at once; those that answer are saved back without
  undoing any change you made meanwhile. Blank ones carry a note in the official apps
  ("did not answer when this account was repaired"), and an addon that is simply empty
  is never mistaken for one.
- **Switching to TorBox mid-film keeps your place.** While TorBox opens a source, the
  one playing keeps playing (only a trailer is stopped), so a TorBox failure carries on
  from the same moment; P2P keeps playing until TorBox answers.
- **Less waste.** A title nobody serves is no longer asked for its sources every frame,
  late source answers no longer leak memory, and the old-protocol OpenSubtitles entry
  stays in your account without being asked on every playback.

- **OpenSubtitles back in the official apps.** Stremio's own OpenSubtitles addon still
  sits in many accounts on its old protocol, with no manifest to fetch, so 0.10.25's
  repair kept it with empty lists and the official apps stopped asking it for
  subtitles. Its manifest is now written out in full, and accounts repaired by 0.10.25
  are fixed again on the next start.
- **Pictures sooner at launch.** Home's artwork no longer waits behind your addons'
  answers: two of the network workers take pictures first, so twenty-odd addons
  answering slowly at launch don't hold every poster back. A third decoder joins, and
  tidying the artwork cache (reading thousands of file dates) waits until the launch
  has settled instead of taking a decoder away while Home is filling in.

## 0.10.25

- **The account repair always finishes.** 0.10.24 saved nothing back to your account
  when one of your addons didn't answer, so the official apps stayed broken. Now an addon
  that doesn't answer keeps its place with empty lists (reinstall it to use it again),
  the rest get their full manifests, and the whole list is saved. The TV says when the
  repair is saved, and says so when saving fails.

## 0.10.24

- **Your account's addons repaired.** When you installed, removed or moved an addon,
  this app saved your Stremio account's addon list with only each addon's name and id
  instead of its whole manifest. Stremio's own apps then couldn't read the list at all
  ("AddonsPulledFromApi: Serialization error ... manifest: missing field"), and this app
  showed no catalogs and "No installed addon provides details". On start, the app now
  spots such a list, fetches every addon's manifest again from its own address and saves
  the list back whole, which fixes the official apps too. If an addon can't be reached,
  nothing is saved and it tries again next start.
- **It can't happen again.** Addons are saved to the account with their full manifest,
  and a list with an incomplete one is never sent.
- **Addons load even on a slow start.** A failed addon list is asked for again after a
  few seconds instead of leaving the app empty; an empty answer never replaces a working
  list; a title page whose addons haven't come in yet waits for them, and asks Cinemeta
  directly for IMDb titles.

## 0.10.23

- **A familiar look.** A new theme, Streaming, is now the default and every install
  moves to it once. It looks like the big streaming apps: near-black background, plain
  sentence-case type, softly rounded cards that grow inside a white outline when
  focused, white buttons with black text in focus and grey ones otherwise. The old
  themes are still in Settings > Interface > Theme.
- **Trailers play on their own.** Stay on a title for a moment (2.5 s on the title
  page, 4 s on Home's hero) and its IMDb trailer plays behind the artwork. The artwork
  fades into the trailer and comes back when it ends. It starts muted; Play/Pause turns
  the sound on and off, and a badge under the clock says which. Settings > Interface >
  "Trailers play on their own" can be Off, Muted (the default) or With sound. Play, the
  Trailer button and the screensaver take over from it cleanly.
- **TorBox for your sources.** With your TorBox API key set, every torrent-only source
  is checked against TorBox's cache. The cached ones are marked "From your TorBox" and
  play from TorBox instead of peers: instant, full speed, and nobody sees your address.
  A torrent TorBox lacks still plays P2P, and TorBox starts downloading it so the
  player can switch over when it's ready.
- **My TorBox videos.** Settings > API keys > My TorBox videos lists everything in your
  TorBox (torrents, usenet, web downloads), and OK plays it.
- **Your TorBox copy first.** When your TorBox already holds the title (a film by its
  name and year, an episode by its name and SxxEyy), it heads the sources as "Your
  copy", is the recommended one, and Play starts it straight away with no race against
  other sources. It opens through TorBox's direct file link, one hop less than before.
- **Quicker, smoother starts.** Resting on a title page (or an episode) asks the addons
  for its sources ahead, so Play opens on them already in. While a film starts, the
  screen shows its artwork, logo and what is happening (Starting, Getting it from
  TorBox, Finding peers) with a progress line, then fades into the picture instead of
  a black screen with a spinner.
- **Keys stay out of the log.** Link tokens are written as `token=***` in app.log.

## 0.10.22

- **Your own videos from TorBox.** Add your TorBox API key in Settings > API keys, then
  "Choose from my TorBox" under the fireplace video and the break recording lists the
  videos in your TorBox. A pick plays through TorBox's permanent link, which always
  forwards to a fresh copy, at full quality; nothing expires and your key is never
  stored inside the link.

## 0.10.21

- **Trailers from IMDb, in 1080p.** For any title IMDb knows, the Trailer button now
  plays IMDb's own trailer: plain 1080p files with lower qualities behind, no YouTube
  and no bot check. When IMDb has none, the YouTube trailer plays as before. The button
  shows on every IMDb title.
- **1080p shown as 1080p.** The player's badge read cinema-wide 1080p films (1920x800)
  as 720p; it goes by width too now.

## 0.10.20

- **YouTube test in Diagnostics.** A new YouTube tab asks for a known video the way
  trailers are fetched and shows each way to get it on this TV and network: the
  visionOS app and its high-quality files, YouTube's TV app and its files, YouTube's
  own playlist and the file with sound, each with YouTube's reason when it refuses,
  then the best that plays now. Play opens the test video.
- **YouTube's refusals show in Problems.** Why a trailer came out at low quality (the
  bot check, a token wanted, copy-protected or SABR-only formats) is now listed there.
- **No phone-app files.** YouTube's Android and iPhone apps' high-quality files always
  want a token now; when visionOS offers none, the TV app's are asked for at once.

## 0.10.19

- **Sharper YouTube again.** YouTube started refusing the high-quality files the app
  asked for (403). The app now checks them before playing; when they are refused it
  asks as YouTube's TV app and solves its links, and when those are refused too it
  starts YouTube's own playlist at once instead of after two failed tries.
- **Sources panel like the Sources page.** While watching, each source shows the same
  badges (4K/1080p, Dolby Vision/HDR, codec, audio, cached, size), the recommended one
  is marked, and the file name sits underneath. Picking a source yourself now always
  keeps the recommended one as its first fallback.
- **Continue Watching keeps the series.** When an episode ends and the next isn't
  played, the series stays in the row at the next episode (Play S1 E2), as in Stremio.
- **Seek previews on the first seek.** The video's keyframe index is read while it
  plays, so the first seek shows pictures too.
- **No seam in the background.** Glass and Mist drew a hard line across screens with
  no artwork behind them (Settings); the haze now fades in.

## 0.10.18

- **Sign-in survives Developer Mode's cache clearing.** On webOS 10 the app kept its
  sign-in and settings in the developer partition's temp folder, which the installer
  uses and Developer Mode's "clear cache" empties. They now live on the TV's internal
  storage; the first start moves them over by itself.

## 0.10.17

- **Updates install again.** On webOS 10 the artwork cache sat where the TV unpacks
  app updates and could fill it, so an update failed with "ipk extraction failure".
  The cache now lives on the TV's internal storage (the old one is deleted), and
  wherever it is it always leaves 200 MB free.

## 0.10.16

- **Home appears at once.** The pictures of the first screen are kept, so Home opens
  with its artwork in place and refreshes behind it.
- **Continue Watching from every device.** Resume points from the Stremio account and
  from Trakt are pulled in on start, when the TV wakes and every 15 minutes; the
  newer one wins.
- **Next episode from the same release.** While an episode plays, the next one's
  source is picked from the same release group and addon when there is one, so audio,
  subtitles and quality stay the same.
- **Search in groups.** Results come under Movies and Series (AI picks first), each
  group on its own rows; the closest match still leads.
- **Find more sources while watching.** The player's Sources panel asks every addon
  without stopping the video; new sources join the list.
- **A source that never starts gives way.** A source stuck loading or buffering at the
  start moves on after 45 s; a torrent with no peers after 25 s. Sources above the
  current one are tried too.
- **Steady seeking.** Every press is 10 s and a quick double press 20 s; holding no
  longer speeds up to minutes.
- **Prayer times that match local apps.** Calculation method Auto picks the country's
  own authority (ISNA in North America, Diyanet in Turkey, Umm al-Qura in Saudi
  Arabia...). Thirteen more methods, Asr corrected by up to two minutes in the north,
  and short summer nights handled as prayer apps do.
- **Fixes.** Showcase Home showed no rows on a fresh profile; Compact repeated a title
  line and scrolled past its end; player panels showed the wrong title (Sources said
  Subtitle style); Skip intro covered the buffering card; source badges vanished on
  the focused row; the Done key, the break clock and the clock pill's notices are
  easier to read.

## 0.10.15

- **YouTube in more ways.** Before YouTube's own playlist, the app now tries its own
  byte-range playlist made from YouTube's separate video and audio (up to 4K where the
  TV takes it) and DASH with every piece listed. It remembers which ways don't start on
  this TV and tries them later, every tenth video again.
- **Seek previews keep up.** While seeking, one preview is decoded at a time and the
  nearest picture already decoded shows meanwhile; previews the TV was too busy for are
  asked again instead of being lost.
- **Card details make room.** After a moment on a card it widens with the rating,
  runtime and synopsis, and the cards beside it slide aside instead of being covered.
- **Smoother Home.** Big pictures reach the screen in bands, about 1 MB a frame.

## 0.10.14

- **YouTube starts sharp.** On TVs where YouTube's DASH stream doesn't play, the
  app learns it and starts with the playlist instead (trying DASH again every fifth
  time), and the playlist plays only its best quality within the TV's limit.
- **Smoother Home.** Artwork goes to the screen a little at a time, so a burst of
  posters or a big background no longer drops frames.
- **Removed what webOS doesn't allow.** webOS won't let this app open other apps, so
  the TV interruption test, opening a trailer in the YouTube app, and opening the
  Homebrew Channel from the update screen are gone. A trailer that can't play now
  says so, and a failed update says where to go.

## 0.10.13

- **SimKL sign-in works again.** SimKL apps made today refuse the PIN; sign-in now uses
  SimKL's OAuth 2.0 device code and renews the week-long login by itself. Older SimKL
  apps still use the PIN.
- **YouTube in the best quality.** The TV's player started YouTube at the lowest quality
  and rarely climbed; it now gets only the best one your TV allows.
- **Diagnostics > Speed.** How fast the app runs on this TV: startup times, how long a
  key press takes to show, and how long each screen takes to draw.
- **TV interruption test.** Also tries the newer app launcher name, and reports whether
  the TV's scheduler ran the alarm.

## 0.10.12

- **Clock faces.** The Clock screensaver has seven faces (Settings > Interface > Clock face):
  digital, flip cards that fall at each new minute, an analog dial with a sweeping second
  hand, a word clock ("IT IS HALF PAST SEVEN"), rings, glowing nixie tubes, and stacked
  StandBy-style digits. The big digits stay sharp at any size.
- **Clock island styles.** The clock in the corner can show the day, seconds, or a small
  dial (Settings > Playback > Clock style).
- **Fireplace screensaver.** A real fireplace video (a YouTube or direct link, set in
  Settings > Interface) plays full screen with the clock over it. Until one is set, a
  drawn fire shows instead.
- **Cast.** Pressing an actor opens their own films and series from TMDB, most popular
  first, without talk shows and cameos.
- **Test TV interruption.** Settings > Take breaks: tells whether this TV lets Stremio come
  to the front by itself from another app, with every reply shown on screen.

## 0.10.11

- **YouTube plays again.** YouTube links only work for the YouTube app they were issued
  to; the TV's proxy now fetches each one as that app (YouTube answered "403" before).
  The HLS playlist, its qualities, audio and pieces all go through the proxy too. A
  refusal is reported at once, so the next way to play it starts sooner.

## 0.10.10

- **YouTube in high quality.** Trailers and YouTube streams now play YouTube's separate
  video and audio together through a DASH manifest the TV serves itself: up to 4K in AV1
  when this TV decodes it, else 1080p H.264, with several qualities to step down to on a
  slow connection. The playlist (HLS) comes next and now starts at 720p or above instead
  of climbing from 144p; the 360p file is the last resort.
- The TV's YouTube proxy sends exactly the byte ranges the player asks for.

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
