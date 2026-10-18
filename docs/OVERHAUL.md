# Overhaul plan

Three goals, in this order of priority when they conflict:

1. **Fastest TV client there is.** First key press answered on the next frame, Home
   interactive from cache in well under a second, no frame drops while scrolling
   on a 2017 webOS 3.5 set.
2. **A look of its own.** Not the Stremio web app, not Apple TV glass, not Nuvio.
   Three original themes, switchable at runtime, **Signal** by default.
3. **Features people actually miss** on TV clients (research below), added
   without costing goal 1.

Everything here is measured on the desktop preview under Xvfb (screenshots per
theme, frame-time counters) before it is trusted, and confirmed on the LG QNED
that already runs the app.

## 1. Design direction

### Why change

The current look (violet glow, translucent white surfaces, white focus pill) is the
same family as Apple TV, the official apps and Nuvio. It also leans on large
translucent gradients and ambient glows that cost fill rate on old Mali GPUs.
The new themes are **flat**: no blur, no full-screen translucent layers, focus
expressed with geometry and one colour. That is both the identity and a
performance decision.

### Themes

All three share layout, navigation and components; a theme is a token set plus
a few style switches that the widgets read (section 2).

| | **Signal** (default) | **Marquee** | **Duotone** |
|---|---|---|---|
| Idea | Broadcast control room, Swiss grid | Cinema programme, projector light | Artwork as two-tone print; focus brings colour |
| Background | Graphite `#0B0C0E`, flat | Warm ink `#0E0B09`, flat | Near-black tinted by the accent |
| Text | Cool white, mono metadata | Parchment `#F3EDE4` | White |
| Accent | Signal lime `#C6FF3D` | Projector amber `#FFB547` | Electric coral `#FF5C7A` |
| Corners | 4 px | 6 px | 14 px |
| Focus | Solid accent frame + hard offset block shadow (no blur) | Lift + amber light bar under the card, title in a marquee strip | Card blooms from duotone to full colour, soft scale |
| Titles | Uppercase, tracked, grotesk | Condensed display, uppercase | Bold, sentence case |
| Metadata | `2012 / 95 MIN / 4K HDR` in mono | `Drama · 2012 · 95 min` | `2012 • 95 min` |
| Rows | Numbered rule: `01 ── POPULAR MOVIES ── 24 ›` | "Now showing — Popular movies" | Plain title, count chip |
| Timeline | Tick marks every 10 % | Thin amber line | Rounded capsule |
| Extra GPU cost | none | none | one duotone shader mode (a mix in the existing shader) |

Each theme has an accent picker (6 accents per theme), and the existing
**pure black (OLED)** switch overrides the background in all three.

### Type

Fonts are OFL and bundled; only the active theme's faces are loaded, and the
secondary faces are baked only at the sizes they are used, into smaller atlases.

| Theme | Titles / UI | Secondary |
|---|---|---|
| Signal | Space Grotesk Medium/Bold | JetBrains Mono (metadata, timecodes) |
| Marquee | Bebas Neue (display) + Inter | — |
| Duotone | Sora SemiBold/Bold | — |

Inter stays the fallback face and the one used before the theme loads.

### Brand mark

A new, theme-coloured mark instead of the purple play tile: Signal draws a
broadcast bar, Marquee a four-point light, Duotone a half-filled circle. The
app keeps its current name.

## 2. Theme engine

- `src/ui/theme.h` becomes a runtime `Theme` struct (`g_theme`). The existing
  `THEME_*` and `R_*` names stay as macros over its fields, so the ~500 call
  sites keep compiling; the ~170 raw `RGBA(...)` literals in screens are moved to
  tokens where they are themeable (rail scrim, panels, overlays).
- Style switches the widgets read:
  `focus_style` (FRAME_SHADOW, LIGHT_BAR, BLOOM), `title_case` (UPPER, AS_IS),
  `meta_style` (SLASH_MONO, DOT, BULLET), `row_header` (NUMBERED_RULE, KICKER,
  PLAIN), `card_treatment` (NORMAL, DUOTONE), `timeline` (TICKS, LINE, CAPSULE),
  `radius_card/panel/key`, letter-spacing for titles.
- Components that change per theme: `ui_card`, `ui_pill`, `ui_icon_button`,
  `ui_chip`, `ui_progress`, the row header (new `ui_row_header`), the meta line
  (new `ui_meta_line`), `ui_background`, `ui_brand`, rail, clock pill, player bar.
- Settings > Interface > **Theme** with a live preview card (same mechanism as
  the subtitle-style preview), and **Accent**.
- Theme switch is instant: tokens swap, font atlases for the new theme are baked
  on a worker and swapped in when ready (Inter shows meanwhile).

## 3. Performance

See the audit findings below; work is split into quick wins (land with the first
theme PR) and structural changes.

### Audit (desktop build, llvmpipe, instrumented copy; relative costs, not Mali timings)

Warm start reaches the first frame in 213 ms on x86: window + GL 48 ms, **font baking
73 ms**, `http_init` 26 ms. Home steadily draws 25–37 calls / 314–544 quads at
0.3–0.6 ms CPU. A TV CPU is roughly 6–10x slower.

| # | Finding | Where | Fix | Phase |
|---|---|---|---|---|
| 1 | **Overdraw**: Home shades 3.8 screens of fragments per frame (two SDF glows alone 1.15); a Home→Detail slide 8–8.8 | `widgets.c` `ui_background`, `gfx.c` shader, `app.c` transitions | Flat themes drop the glows; skip the outgoing page's background in transitions; split the 9-way `if` shader into solid/texture/glyph/SDF programs | 1 / 5 |
| 2 | **One FIFO HTTP queue**: OK on a card waits behind 40–60 poster requests (0.5–1 s) | `http.c`, `image.c` | Two lanes (API first, images LIFO), drop requests for cards scrolled away, `curl_share` for DNS/TLS/connection reuse | 1 |
| 3 | **Detail always waits on the network**, though Home fetched the same meta | `screen_detail.c`, `screen_home.c` | Keep meta JSON in memory + disk cache, fill Detail at once; prefetch meta after ~300 ms on a card | 1 |
| 4 | **Font bake blocks start** (58–73 ms, ~0.5 s on TV) | `font.c` | Cache baked atlases on disk keyed by font file and sizes; 2x1 oversampling | 1 |
| 5 | **Main-thread disk I/O and JSON**: poster files read inside `image_get`, library parsed before first frame, catalog caches parsed in `update`, disk-cache trim `stat`s ~2000 files at start | `image.c`, `app.c`, `screen_home.c` | Disk reads in the decode thread, trim on a worker, parse in workers | 1 / 5 |
| 6 | **Library saved every 30 s during playback** on the main thread (50–100 ms on TV) | `app.c` `save_library_cache` | Dirty flag, debounced save, flush on pause/exit | 1 |
| 7 | **Image pipeline**: one decoder, full-size decode then box shrink (as costly as decode), POT stretch (+40 % VRAM) | `image.c` | Scaled WebP decode, 2 decoder threads, no POT stretch where NPOT is allowed, RGB565 for opaque posters | 5 |
| 8 | **Key latency**: idle loop `SDL_Delay(25)` (avg 12 ms before a key is read) and a delay after a vsynced swap | `main.c` | `SDL_WaitEventTimeout` when idle; no extra delay when the swap blocks | 1 |
| 9 | **O(n²) text fitting/wrapping** (`font_draw_fit` re-measures per removed char; ~25 ms/frame on ARM for long stream rows) | `font.c` | Running pen position + binary search for the cut | 1 |
| 10 | **Batching**: every texture/clip change flushes with a fresh `glBufferData`, 52-byte vertices | `gfx.c` | One VBO per frame with draw ranges, poster atlas, smaller vertex | 5 |
| 11 | Small per-frame scans (library lookups per Continue card, 23 prefetch lookups per idle loop), `http_init` on the main thread | `screen_home.c`, `main.c` | Cache lookups, init curl on a thread | 1 |

### Budgets (checked in CI on the desktop build, confirmed on the TV)

| Metric | Budget |
|---|---|
| Cold start to interactive Home (cached) | < 600 ms desktop, < 1.5 s webOS 3.5 |
| Key press to first changed frame | 1 frame |
| Home scroll, Light effects, webOS 3.5 | 60 fps, no frame > 33 ms |
| Draw calls on Home | < 25 |
| Texture memory, Low memory mode | < 64 MB |

A `SNTV_PERF=1` mode prints startup phases and a frame-time histogram, and
`tests/smoke.sh` records them per run so regressions show up in CI.

## 4. Features (researched)

From Stremio's own September 2026 update, Plex/Jellyfin/Kodi skins, Nuvio and
common complaints in Stremio feature requests. Ranked by value / cost.

| Feature | Why | Cost |
|---|---|---|
| **Next episode preload indicator** | The Next button fills like a loader while the next episode's sources are fetched; full and lit when it can start instantly | S |
| **Watched / progress marks on every poster** | Most common complaint about Stremio TV apps (you can't see what you've watched) | S |
| **Sleep timer** + **"Still watching?"** after N autoplayed episodes | Plex has it, Jellyfin users keep asking; pairs with the existing Bedtime break | S |
| **Interface size** (90–130 %) and **text size** | Stremio added interface zoom in 2026; 720p panels and older viewers | M |
| **Poster quick menu** (long OK / Menu key): Play, Trailer-free details, Add to Library, Mark watched, Hide | Fewer screens per action on a remote | M |
| **Shuffle / "Pick for me"** from Library, a catalog or a genre, with **"fits before bed"** (runtime ends before the Bedtime time) | Decision fatigue; nobody else ties it to a bedtime | S |
| **"New episodes" badge** on the rail and a Home row from the Calendar data already fetched | Calendar exists; surface it | S |
| **See all** grid from any row | Rows cap at a few dozen | S |
| **Jump by letter / number keys** in long grids (Library, Explore) | Remote number keys are unused | S |
| **Profiles (local)** with per-profile Library filters, track memory, theme, and a **Kids** profile filtered by TMDB age rating | Shared family TV | L |
| **Watch stats**: hours this month/year, top genres, streaks, from Library data | Fun, local, no server | M |
| **Live TV guide** for addon `tv` catalogs that carry schedules | Stremio added it in 2026 | L |
| **Share to phone**: QR with the title's web link | QR infra exists | S |
| **Remote help overlay** (hold Info): what each key does on this screen | Discoverability of colour keys | S |

Not feasible on TV hardware, so left out: dialogue boost / night-mode audio
compression (the TV's pipeline owns audio), voice search (webOS voice framework
is not open to native apps), local frame grabs.

## 5. Phases

Each phase is one PR, screenshots for all three themes attached, smoke tests and
unit tests green, version bumped and CHANGELOG updated.

1. **Engine + Signal + quick perf wins + preload indicator.** Runtime tokens, the
   component kit reading them, Signal as default, Theme setting, the next-episode
   loader, the perf quick wins and `SNTV_PERF`.
2. **Marquee and Duotone**, accent picker, theme preview in Settings, fonts per
   theme with lazy atlases.
3. **Home and Detail re-layout** for the new language: numbered rows, poster
   watched/progress marks, See all, poster quick menu.
4. **Player chrome**: new control bar per theme, timeline styles, sleep timer,
   "Still watching?".
5. **Structural performance work** from the audit.
6. **Features**: shuffle / fits before bed, new-episode badges, letter jump,
   interface and text size, share to phone, key help.
7. **Profiles + Kids**, watch stats, Live TV guide.

## 6. Verification

- `tests/smoke.sh` gains a `SNTV_THEME` loop: every scripted screen captured in
  all three themes.
- Unit tests for token resolution (accent contrast against background ≥ 4.5:1
  for text on accent), the prefetch progress counter and sleep-timer logic.
- `SNTV_PERF` numbers compared before/after each phase and recorded in the PR.
- First TV check after phase 1 on the LG QNED, then on a webOS 3.x set if one is
  available.
