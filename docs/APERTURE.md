# Aperture native prototype

Select **Settings > Interface > Theme > Aperture**. Existing profiles keep their
theme. The charcoal background, warm ivory type, and coral focus marker are the
default palette; the usual accent and background choices remain available.

Home uses an editorial title column and an off-center artwork window. The artwork
opens sideways by 20 logical pixels when focus settles; the poster rows retain
their existing navigation and position. Compact Home remains compact. Showcase
retains its carousel. Title pages share the composition, with Watch/Resume,
episodes, and the existing About/cast/related content below.

Sources keeps the current recommendation and fallback logic. The recommendation
leads, comparisons have thin rules, and the selected row reveals its filename.
`Checked file` describes a successful container probe; `From source name` describes
inferred tags. Unsupported sources, failed links, source rules, and bandwidth or
codec warnings remain visible.

| Remote action | Result |
| --- | --- |
| Home: arrows, OK | Browse and open Details |
| Details: OK on Play/Resume or an episode | Sources or the existing resume path |
| Sources: Up/Down, OK | Choose and start a source |
| Player: OK or Play/Pause | Pause/resume; the title card appears on pause |
| Player: Left/Right | Existing debounced seek, with a filmstrip |
| Player: Down, Left/Right, OK | Quiet control row, including subtitle/audio drawers |
| Drawer: Up/Down, OK | Choose a track; existing appearance/timing actions stay available |
| Drawer: Back | Close drawer and restore player controls |
| Player: Back | Return to Sources (existing PiP preference still applies) |
| Sources: Back; Details: Back | Return to Details; return to the focused Home card |

Titles are set in DM Serif Display (SIL OFL 1.1, `assets/fonts`), loaded only in this
theme as its alternate face: baked at 24, 40, 72 and 132 px into one 2048x1024 alpha
atlas, the 132 px hero size with ASCII only (titles with other letters step down to
96 px, which carries Latin-1 and Extended-A). Home, title pages, the player, the
drawers, Search and Settings follow the reference mockups: full-bleed artwork with the
serif title column on the left, slash-separated facts, an ivory primary button with
quiet text actions, landscape rows titled in spaced serif capitals, a coral-marked
focus with an ivory outline, the time / coral line / time left on one row in the
player, a lower-left pause card, and borderless keyboard keys.

The artwork windows pan slowly across a slightly closer crop. Home and the title page
ask for about eight frames a second while they show, not a continuous 60; Light
effects and low memory keep the artwork still. Taglines come from TMDB's details
(when a TMDB key is set) and replace the synopsis on Home.

There are no additional render targets, blur shaders, or decoder instances.
Artwork uses the existing bounded image cache and the renderer's cropped quad.
The filmstrip requests only the selected frame through the existing decoder path;
its neighbors are cache-only, so cold neighbors are placeholders until those times
have been visited. Without FFmpeg or the configured thumbnail helper, preview
slots remain placeholders. Low-memory mode shows one preview. Decoded preview
frames are letterboxed, and drawers overlay the video without resizing it.

Light effects disables artwork/transition interpolation. The existing transitions
setting is honored; full Aperture transitions finish in 220 ms. Aperture's artwork
windows use still images, so background trailers are not started in this theme.
The main film still uses the TV's video pipeline. All layouts use the existing
1920x1080 logical canvas and scale with the GLES viewport for 720p through 4K.

## Validation

On a Linux host with the README's build dependencies plus `xvfb`, `xauth`, and
`python3-pil`:

```sh
bash tools/linux.sh
ctest --test-dir build --output-on-failure
bash tests/aperture-smoke.sh build
```

The smoke script uses an isolated fixture profile and checks the current native
screen and expected playback/pause state before capture. It covers Home, Details, Sources, Player, pause/rest,
subtitle/audio drawers, seek cancellation, Back to Home, transitions, and
Light effects with low memory. Captures go to `tests/out/aperture/` (run it locally;
CI runs the unit tests, including the theme contracts, and builds the webOS package). The desktop backend simulates video; physical webOS playback, remote
repeat timing, and GPU performance still need a device run.

The full Linux build, the unit tests (including the theme contracts), this journey and
the main smoke suite pass on the desktop backend.

Background trailers and card details are off in this theme; Settings says so beside
those rows instead of showing switches that do nothing.
