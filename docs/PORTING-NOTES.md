# Porting notes: what to check on real hardware

These are the points where this code makes assumptions that could only be validated on a
TV. Each one names the file to adjust.

## LG webOS

1. **Toolchain + SDL fork.** Build with the buildroot-nc4 SDK (`tools/webos.sh`). Its SDL2
   is the webosbrew fork; `src/video/video_webos.c` declares
   `SDL_webOSCreateExportedWindow/SetExportedWindow/DestroyExportedWindow` as weak symbols
   so the binary still loads when the fork lacks them (then ACB is used).
2. **`com.webos.media/load` payload.** The option block currently sends `appId`,
   `windowId`, `needAudio`, and `transmission.contentsType`. If `load` returns
   `returnValue:false`, read `errorText` in `app.log` and compare with what
   `luna-send -i` reports on that firmware. stremio-video's `WebOsVideo` is the best
   reference for the fields newer firmwares expect (`mediaTransportType`, `option.appId`).
3. **ACB on webOS 2-4.** Constants `PLAYER_TYPE_MSE=2`, `SINK_TYPE_MAIN=1`,
   `APPSTATE_FOREGROUND=1`, `PLAYSTATE_*` are the values moonlight-tv uses. If the video
   plane stays black while audio plays, the ACB state sequence is the first suspect.
4. **Video hole.** `gfx_video_hole()` writes alpha 0 with `GL_ONE, GL_ZERO`. It needs an
   RGBA8 EGL config (requested in `main.c`) and `"transparent": true` in `appinfo.json`.
   `glReadPixels` screenshots will show black where the video is; that is expected.
5. **Remote keys.** `SDL_WEBOS_ACCESS_POLICY_KEYS_BACK/EXIT` hints are set before window
   creation. Scancodes `0x1BE` (back) and `0x1BF` (exit), colour keys `0x190-0x193`, are in
   `src/ui/input.c`; dump `e.key.keysym.scancode` to `app.log` if a key is dead.
6. **Writable dir.** `SDL_GetPrefPath` is probed for writability and falls back to
   `$HOME/.org.stremio.nativetv`, then `/tmp`. If Continue Watching does not persist across
   launches, check which path `app.log` printed on start.
7. **Memory.** webOS 3 sets have ~300 MB free. `SNTV_IMAGE_CACHE_BYTES` is 48 MB and images
   are capped at 512 px. Lower both in `src/platform/platform.h` if the app is killed.
8. **DTS audio.** Not handled. Nuvio transcodes DTS to AAC with FFmpeg on webOS; that is a
   separate component (LGPL) and out of scope for v0.1.

## Samsung Tizen native

1. `video_tizen.c` passes the Elementary window to `player_set_display(... OVERLAY ...)`.
   SDL on Tizen exposes the window through `SDL_SysWMinfo`; if the struct differs for the
   rootstrap in use, swap the accessor under `SDL_VIDEO_DRIVER_TIZEN`.
2. The back key arrives as `SDLK_AC_BACK`. Media keys may need
   `tizen.tvinputdevice.registerKey` equivalents for native apps (`ecore_wl2` key grabs).
3. 2018-2020 firmwares refuse to `dlopen`/exec native code from the app directory. The
   Nuvio project solves this with a .NET host that loads the native library from memory;
   port that `tizen-tpk/` approach if you target those models.

## Tizen WebAssembly

1. `src/http.c` relies on libcurl. For the `.wgt`, add `src/http_wasm.c` that implements
   the same `http_*` API with `emscripten_fetch` and swap it in CMake for `SNTV_PLATFORM=wasm`.
2. Samsung's web engine decides what the `<video>` element can play; no Dolby Vision in
   MP4 and no plain-HTTP video from an HTTPS page, mirroring the VIDAA notes in Nuvio.

## Stremio API specifics

- Requests are POST JSON to `https://api.strem.io/api/<path>` and carry a `type` tag
  (`Login`, `AddonCollectionGet`, `DatastoreGet`, ...) like stremio-core does.
- An `{"error":{"code":1}}` reply on `addonCollectionGet` is treated as an expired session.
- `datastorePut` sends full `LibraryItem` objects; timestamps are epoch milliseconds.
  stremio-core accepts both ms and ISO strings, and `api_parse_library()` reads both.
- Streams with `infoHash` are resolved to `<streaming server>/<infoHash>/<fileIdx>`.
