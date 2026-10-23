# Cuelight for Android TV and Fire TV: plan

One APK for Android TV, Google TV and Fire TV (all Android 7+, `minSdk 24`), built from the
same C code as the webOS app. Phones and tablets are not in scope: the UI is made for a
remote across the room, and a touch layout is a separate project.

## Why it ports well

The app was split for this from the start:

| Part | webOS today | Android |
|---|---|---|
| Window, GL, input, audio | SDL2 + GLES2 | SDL2's Android port (`SDLActivity`) + GLES2: same code |
| UI, addons, catalog, sync, subtitles, relay, breaks | portable C | unchanged |
| Video (`src/video/video.h`, 18 calls) | `video_webos.c`: the TV's media pipeline over LS2 | **new** `video_android.c` + `CuelightPlayer.java`: Media3/ExoPlayer over JNI |
| Video on screen | hardware plane under a transparent hole the UI draws | ExoPlayer's `SurfaceView` under SDL's surface (SDL surface translucent, `setZOrderMediaOverlay`): the same hole, nothing in the UI changes |
| HTTP | `dlopen` of the TV's libcurl | libcurl + mbedTLS built with the NDK and shipped in the APK (`libcurl.so`, same `dlopen`); CAs from `/system/etc/security/cacerts` (`CURLOPT_CAPATH`) |
| Data folder | `/media/internal/.<id>` etc. | `SDL_GetPrefPath` → the app's internal storage (already the first choice in `data_dir`) |
| P2P engine (Go) | `torrentd` next to the binary | `GOOS=android`, shipped as `libtorrentd.so` in `jniLibs` and run from `nativeLibraryDir` (the one place Android lets an app execute its own files) |
| Seek previews (FFmpeg) | static, built by `tools/ffmpeg.sh webos` | `tools/ffmpeg.sh android` with the NDK |
| Remote | LG keys | D-pad, centre, Back already map (`SDLK_SELECT`, `SDLK_AC_BACK` in `src/ui/input.c`); add media keys, Menu, Fire TV's |
| TV keyboard | webOS IME | Android IME through `SDL_StartTextInput` (the "Type with the TV's keyboard" path) |
| Updates | Homebrew Channel service | the app downloads the APK, checks its SHA-256, hands it to `PackageInstaller` (`REQUEST_INSTALL_PACKAGES`); the system asks once |

## What Android lets us do better than the webOS build

- **Formats**: ExoPlayer plays MKV/MP4/HLS/DASH with every track, HEVC/AV1 where the chip has
  them, **HDR10, HLG and Dolby Vision** passed to the TV, **Dolby/DTS passthrough** over HDMI.
- **Frame-rate matching**: switch the display to 23.976/24/25/50 Hz for the video
  (`Surface.setFrameRate`, `preferredDisplayModeId`): no judder on films.
- **Start speed**: our ranking, start race and buffer ahead in front of ExoPlayer's own
  fast start; the relay stays for sources that need headers.
- **System integration**: a `MediaSession` (the remote's play/pause and Fire TV's voice
  "pause" work while playing), and a Leanback banner on the home screen.

## Distribution

Not Google Play: its rules on apps that play from third-party addons and torrents make a
listing fragile. Sideloaded instead, like the webOS app:

- `cuelight-releases` carries `cuelight-androidtv-<version>.apk` (and `-armv7`) next to the ipk.
- Fire TV: the free **Downloader** app with a short code or URL; Android TV: Downloader, or
  "Send files to TV".
- After the first install, the app updates itself (above).

## App id and signing

- The Android id is permanent from the first release: proposed **`app.cuelight.tv`**
  (webOS keeps `org.stremio.nativetv` until its own tested move).
- One release signing key, generated once and kept as repository secrets
  (`ANDROID_KEYSTORE_B64`, `ANDROID_KEY_PASSWORD`). Losing it means users must uninstall to
  update, so it gets an offline backup too.

## Milestones

Status: **1 and 2 built** (the APK builds with its own libcurl: `tools/android.sh`; not yet run on a device).

1. **Skeleton.** `SNTV_PLATFORM=android` in CMake, `deploy/android` (Gradle, SDL2's
   `SDLActivity`, manifest with `LEANBACK_LAUNCHER`, `touchscreen required=false`,
   cleartext allowed for addons on the LAN), `video_null.c` for now. Done when the APK opens
   on Home in the Android TV emulator.
2. **Network.** NDK builds of libcurl + mbedTLS, CA path. Done when addons, the catalog and
   artwork load.
3. **Video.** `video_android.c` + `CuelightPlayer.java`: load, play/pause, seek, rate,
   volume, position/duration/buffering, audio and text tracks, errors and warnings, the
   surface under the hole. Done when the player screen, seek previews' timing and
   Up Next work as on webOS.
4. **TV system.** Media keys, `MediaSession`, background/foreground, audio focus, keep
   awake while playing, the IME.
5. **Extras.** `libtorrentd.so`, FFmpeg seek previews, the self-updater.
6. **Polish and release.** Frame-rate matching, HDR/DV checks, Fire TV quirks, memory
   limits for 1 GB sticks, a banner and icon, CI job, first release.

## Build and CI

- Gradle project in `deploy/android`; its `externalNativeBuild` runs the root
  `CMakeLists.txt` with `-DSNTV_PLATFORM=android` for `arm64-v8a` and `armeabi-v7a`.
- GitHub's Ubuntu runners have the Android SDK and NDK installed: one more job beside
  `webos`, building and signing the APK. Free while the repository is public; on a
  private repository it uses Actions minutes (about 10 per release).
- Tests: the desktop UI suite already covers the shared code. Android adds a build check
  and, later, an emulator smoke run (Home opens, a stream starts).

## Open questions

- Which device will we test on (an Android TV / Google TV box, a Fire TV stick)? An emulator
  covers the UI; real playback, HDR and passthrough need hardware.
- The app id above: keep, or another?
- Fire TV first, or Android TV first? (Same APK; it decides which quirks get fixed first.)
