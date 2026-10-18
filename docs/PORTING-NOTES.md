# Porting notes: what to check on real hardware

These are the points where this code makes assumptions that could only be validated on a
TV. Each one names the file to adjust.

## LG webOS

The build is now real: CI cross-compiles with the openlgtv buildroot SDK
(`webos-a38c582`) and `tools/check-ipk.sh` verifies the package. What still needs
a TV is playback. The backend (`src/video/video_webos.c`) follows the behaviour
the Nuvio native client documents from real TVs:

1. **Bus name.** `LSRegister("com.webos.media.client.org.stremio.nativetv")`; other
   names are refused. Fallback `....<pid>` if the hub still holds the name.
2. **Load payload.** `{"payload":{"option":{"useSeekableRanges":true,"appId":...,
   "bufferControl":{"userBufferCtrl":false},"windowId":...}},"uri":...,"type":"media"}`.
3. **webOS 5+.** `SDL_webOSCreateExportedWindow(0)`, then
   `SDL_webOSSetExportedWindow(id, &{0,0,videoW,videoH}, &screen)`; never a NULL source.
4. **webOS 3-4.** `AcbAPI_create`, `initialize(id, 0, appId, cb)`, `setSinkType(0)`;
   after each `loadCompleted`: `setMediaId`, state 1, `connectDass(0)`,
   `setMediaVideoData` (JSON with `"context":mediaId`, `mediaSei/mediaVui` keys on
   webOS < 5), `setDisplayWindow`, state 2.
5. **Events.** `currentTime` nested object; `sourceInfo` tracks; `videoInfo`
   size; `bufferRange`; error 200 = unsupported audio, not fatal.
6. **Permission.** `appinfo.json` has `requiredPermissions: ["all"]`. Whether a
   non-rooted Homebrew Channel install may call `com.webos.media` is unmeasured.
   Symptom if not: the UI works, video is black. `app.log` shows the LS2 replies.
7. **Remote keys.** Hints `SDL_WEBOS_ACCESS_POLICY_KEYS_BACK/EXIT` are set before
   the window exists; scancodes match LG's `SDL_webOS.h`.
8. **Memory.** webOS 3 sets have ~300 MB free; the artwork budget follows RAM.
9. **DTS.** Not handled; error 200 is shown as a notice so you can switch audio.

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

## Playback behaviour to confirm on a TV

- Track lists: `sourceInfo.programInfo[0].audioTrackInfo` / `subtitleTrackInfo` is the
  shape the OSE docs show; if the menu stays empty, log the raw `sourceInfo` payload.
- `bufferingStart` / `bufferingEnd` / `bufferRange.percent` drive the spinner.
- Fallback: a `error` event or 45 s without `loadCompleted` moves to the next source.
  If a firmware emits `error` for recoverable hiccups, raise the threshold in
  `screen_player.c` (`fail_over`).
- Addon subtitles are drawn by the app over the video hole; embedded tracks are drawn
  by the pipeline. Selecting one disables the other to avoid double captions.

## Stremio API specifics

- Requests are POST JSON to `https://api.strem.io/api/<path>` and carry a `type` tag
  (`Login`, `AddonCollectionGet`, `DatastoreGet`, ...) like stremio-core does.
- An `{"error":{"code":1}}` reply on `addonCollectionGet` is treated as an expired session.
- `datastorePut` sends full `LibraryItem` objects; timestamps are epoch milliseconds.
  stremio-core accepts both ms and ISO strings, and `api_parse_library()` reads both.
- Streams with `infoHash` are resolved to `<streaming server>/<infoHash>/<fileIdx>`.
