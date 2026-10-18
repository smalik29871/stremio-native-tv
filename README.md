# Stremio Native TV (unofficial)

A native Stremio client for TVs that the official web app cannot run on well:
LG webOS 3+, Samsung Tizen 4+ (native `.tpk`) and Tizen 5.5+ (WebAssembly `.wgt`).
Written in C99 on SDL2 + OpenGL ES 2, with the TV's own video pipeline doing playback.

It is modelled on [nuvio-native-legacy](https://github.com/iqui27/nuvio-native-legacy),
which proved the approach for Nuvio. This project does the same for the
**Stremio addon protocol** and the **Stremio account API**, so it uses the same
account, addons, Library and Continue Watching as the official apps.

> Status: **v0.1.0 scaffold**. The desktop preview builds, passes its tests and runs the
> full UI flow against a fake addon. The webOS and Tizen video backends are written
> against the platform APIs but have **not yet been compiled against the TV toolchains
> or run on a TV** in this environment. See *What is verified* below before trusting anything.

## What it does

- **Addon protocol**: manifests (string and object resources, `idPrefixes`, modern `extra`
  and legacy `extraSupported`), catalogs with extras (`search`, `skip`, `genre`), meta,
  streams, subtitles. URL building and response parsing are unit-tested.
- **Account**: `login`, `logout`, `addonCollectionGet/Set`, `datastoreGet/Put` for
  `libraryItem`. Progress is written with the same `state` fields as stremio-core, so
  Continue Watching stays in sync with phones and desktops.
- **Logged out**: Cinemeta + OpenSubtitles defaults, plus addons installed from a URL.
- **Screens**: Home (hero, Continue Watching, one row per catalog), Detail (seasons and
  episodes, resume, add to Library), Streams (all stream addons, badges for torrent /
  YouTube / external), Player overlay, Search (debounced, every catalog with a `search`
  extra), Settings (account, streaming server, install/remove addons), Sign in with an
  on-screen keyboard.
- **Low-RAM friendly**: images are decoded on a worker thread, downscaled to 512 px on
  TVs, and evicted by byte budget (48 MB on TV). Home starts from the cached addon
  collection and library.
- **libcurl via `dlopen`**: never linked, because LG ships `libcurl.so.5` and the SDK
  ships `.so.4`.

Not done yet: subtitles rendering (parsed, not drawn), audio/subtitle track selection,
torrent playback without an external Stremio streaming server, YouTube trailers,
Trakt, debrid shortcuts, profiles, notifications. See `docs/ROADMAP.md`.

## Layout

```
src/
  main.c               SDL/GL setup, main loop, dev hooks (SNTV_SCREENSHOT, SNTV_KEYS)
  app.c/.h             app state: addons, library, auth, screen stack, progress sync
  http.c/.h            libcurl over dlopen + worker pool; callbacks on the main thread
  store.c/.h           settings.json + cached JSON in the app's writable dir
  image.c/.h           async artwork loader with LRU byte budget
  log.c/.h             log file (stdout is /dev/null under SAM on webOS)
  addon/addon.c        Stremio addon protocol: manifest, catalog/meta/stream/subtitles
  addon/stremio_api.c  api.strem.io requests/responses, LibraryItem (de)serialisation
  ui/gfx.c             GLES2 2D renderer, scissor clip, video hole (alpha 0)
  ui/font.c            stb_truetype atlas, UTF-8, ellipsis, word wrap
  ui/input.c           keyboard + LG/Samsung remote mapping
  ui/keyboard.c        d-pad on-screen keyboard
  ui/screen_*.c        home, detail, streams, player, login, search, settings
  video/video.h        backend interface
  video/video_null.c   desktop preview (simulated clock)
  video/video_webos.c  com.webos.media over LS2; exported window (webOS 5+) or ACB (2-4)
  video/video_tizen.c  capi-media-player, overlay display
  video/video_wasm.c   HTML5 <video> behind a transparent canvas (Tizen .wgt)
deploy/webos           appinfo.json, icons (type: native)
deploy/tizen-native    tizen-manifest.xml
deploy/tizen-wasm      config.xml + index.html
tools/                 linux.sh, webos.sh (+ --ipk/--install), package-ipk.sh,
                       tizen-native.sh, tizen-wasm.sh, env.sh
tests/                 test_addon.c (protocol/API unit tests), fake_addon.py, smoke.sh
vendor/                cJSON (MIT), stb_truetype, stb_image (public domain)
```

## Building

### Desktop preview (Linux, no video)
```
sudo apt install cmake build-essential libsdl2-dev libgles-dev
tools/linux.sh            # builds, runs unit tests
./build/stremio-tv        # arrows / Enter / Esc, F3 = search, F1/F2/F4 = colour keys
```
Headless UI check (needs Xvfb + Pillow): `bash tests/smoke.sh` writes screenshots to `tests/out/`.

### LG webOS (.ipk)
1. Get the toolchain: `arm-webos-linux-gnueabi_sdk-buildroot-x86_64.tar.gz` from
   [openlgtv/buildroot-nc4 releases](https://github.com/openlgtv/buildroot-nc4/releases)
   (macOS builds: [webosbrew/native-toolchain](https://github.com/webosbrew/native-toolchain)).
   Extract it and run `relocate-sdk.sh`. It ships SDL2 (the webOS fork), GLES2, luna-service2.
2. `WEBOS_TOOLCHAIN=/path/to/arm-webos-linux-gnueabi_sdk-buildroot tools/webos.sh --ipk`
3. Install with the Homebrew Channel / Dev Manager, or `TV_IP=... tools/webos.sh --install` on a rooted TV.

`tools/package-ipk.sh` uses `ares-package` when present and otherwise writes the
`ar` + tar layout itself, and refuses to package anything that looks like a credential file.

### Samsung Tizen native (.tpk, Tizen 4.0-9)
Tizen Studio with the TV native rootstrap: `tools/tizen-native.sh`. The package is left
unsigned on purpose; sign with your certificate profile (`TIZEN_SIGN_PROFILE=...`) or let
Apps2Samsung sign it for the TV's DUID. 2018-2020 sets block native code in the app
folder; the Nuvio project works around that with a .NET host that loads the library from
memory. That shim is **not** part of this repository yet.

### Samsung Tizen WebAssembly (.wgt, Tizen 5.5+)
Emscripten: `EMSDK=~/emsdk tools/tizen-wasm.sh`. Networking must go through the browser
in this build; `src/http.c` is still the curl implementation, so a `fetch()`-based
`http_wasm.c` is the first thing to add before the `.wgt` is useful (see roadmap).

## What is verified, and what is not

| Area | Status |
|---|---|
| Addon protocol + API parsing | Unit tests pass (`tests/test_addon.c`, 60+ assertions) |
| Full UI flow (home → detail → episodes → streams → player, search, settings, login) | Smoke-tested headless under Xvfb against `tests/fake_addon.py` |
| Live `api.strem.io` / Cinemeta | Not reachable from the build sandbox; untested live |
| webOS build, LS2 payloads, exported window / ACB binding | Written from the webOS OSE reference and stremio-video/moonlight-tv practice; **needs a TV** |
| Tizen native player | Written against capi-media-player; **needs Tizen Studio + TV** |
| Remote scancodes (`0x1BE` back etc.) | Taken from community SDL-webOS usage; verify in `src/ui/input.c` |

Things known from the Nuvio port that are already handled: `SDL_WEBOS_ACCESS_POLICY_KEYS_BACK`
is set before the window is created; logs go to a file; libcurl is `dlopen`ed trying
`.so.5` then `.so.4`; the GL surface keeps an 8-bit alpha channel and the player punches a
transparent hole so the video plane shows through; the UI is laid out on a 1920x1080
canvas and scaled, so 720p webOS 3 panels work.

## Torrent streams

Stremio's torrent engine (`server.js`) is not available on TVs. Streams that only carry an
`infoHash` are shown with a *needs server* badge until you set a **Streaming server** URL
in Settings (any machine on your LAN running the Stremio service, usually port 11470).
Debrid addons return plain HTTPS URLs and work without it.

## Configuration and data

Everything lives in the app's writable directory (`SDL_GetPrefPath`, falling back to
`$HOME/.org.stremio.nativetv`): `settings.json` (auth key, streaming server, local addon
list, resume points), `addons.json` and `library.json` caches, `app.log` and `app.prev.log`.
No credentials are compiled into the binary.

## License

No licence file is committed yet; pick one before publishing. The reference project is
GPL-3.0 and stremio-web is GPL-2.0, so a GPL licence is the natural fit. Vendored
code: cJSON (MIT), stb (public domain / MIT), DejaVu Sans (Bitstream Vera licence, see
`assets/fonts/DejaVuSans-LICENSE.txt`).
