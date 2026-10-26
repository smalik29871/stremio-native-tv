# Third-party notices

Cuelight's own code is under the terms in [LICENSE](LICENSE) (all rights reserved). The
components below are not: each stays under its own licence, and nothing in Cuelight's
licence limits what that licence lets you do with it. The paths below are in Cuelight's source,
where each licence text sits next to its component; the same text is in each component's own
repository, linked here.

## Bundled in the apps

| Component | Version | Licence | Where it is used | Licence text |
|---|---|---|---|---|
| [FFmpeg](https://ffmpeg.org) (libavcodec, libavutil; on Windows also libavformat, libswscale, libswresample) | 7.1.1 | LGPL 2.1 or later | Seek previews: only the H.264 and HEVC decoders (webOS: `lib/libavcodec.so.61`, `lib/libavutil.so.59`). On Windows the whole player: demuxers, HTTP(S) and HLS, video, audio and text-subtitle decoders (the `av*-*.dll` and `sw*-*.dll` beside `Cuelight.exe`). No GPL or nonfree parts; unmodified, shared libraries you may replace with your own build of the same version. Where to get its source, and the scripts it was built with, are in the `ffmpeg` folder beside them | [COPYING.LGPLv2.1](https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/refs/heads/release/7.1:/COPYING.LGPLv2.1) |
| [QuickJS-ng](https://github.com/quickjs-ng/quickjs) | 0.17 | MIT | Runs the YouTube solver in a sandbox | `vendor/quickjs/LICENSE` |
| [cJSON](https://github.com/DaveGamble/cJSON) | | MIT | JSON | header of `vendor/cJSON.h` |
| [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) (Project Nayuki) | | MIT | QR codes on screen | header of `vendor/qrcodegen.c` |
| [stb_truetype, stb_image](https://github.com/nothings/stb) (Sean Barrett) | | Public domain or MIT | Fonts, images | end of each header in `vendor/` |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) (decoder) | 1.4.0 | BSD-3-Clause | WebP artwork | `vendor/libwebp/COPYING` |
| [yt-dlp/ejs](https://github.com/yt-dlp/ejs), with [meriyah](https://github.com/meriyah/meriyah) and [astring](https://github.com/davidbonnet/astring) | 0.8.0 | Unlicense; meriyah ISC; astring MIT | YouTube signature solver (`assets/yt`) | `assets/yt/LICENSE` and the file headers |
| [anacrolix/torrent](https://github.com/anacrolix/torrent) and its Go modules | pinned in `tools/torrentd/go.sum` | MPL 2.0, MIT, BSD, Apache 2.0 (per module) | The P2P engine `torrentd` (webOS, Android) | each module's repository, at the pinned version |
| [Inter](https://rsms.me/inter/), [Space Grotesk](https://github.com/floriankarsten/space-grotesk), [JetBrains Mono](https://www.jetbrains.com/lp/mono/), [Open Sans](https://github.com/googlefonts/opensans), [Bebas Neue](https://github.com/dharmatype/Bebas-Neue), [DM Serif Display](https://github.com/googlefonts/dm-fonts) | | SIL Open Font License 1.1 | Type in the themes | `assets/fonts/*-LICENSE.txt` |

### Android app only

| Component | Version | Licence | Where it is used | Licence text |
|---|---|---|---|---|
| [SDL2](https://libsdl.org) | 2.30.9 | zlib | Window, input, audio (`SDLActivity`) | [LICENSE.txt](https://github.com/libsdl-org/SDL/blob/release-2.30.9/LICENSE.txt) |
| [curl](https://curl.se) | 8.11.0 | curl (MIT-style) | HTTP | [COPYING](https://curl.se/docs/copyright.html) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | 3.6.2 | Apache 2.0 (chosen of Apache 2.0 or GPL 2.0+) | TLS for curl | [LICENSE](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-3.6.2/LICENSE) |
| [AndroidX Media3](https://github.com/androidx/media) (ExoPlayer, HLS, DASH, UI) | 1.4.1 | Apache 2.0 | Video playback | [LICENSE](https://github.com/androidx/media/blob/release/LICENSE) |
| [AndroidX Core](https://developer.android.com/jetpack/androidx/releases/core) | 1.13.1 | Apache 2.0 | Handing a downloaded update to the installer | [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0) |
| [android-youtube-player](https://github.com/PierfrancescoSoffritti/android-youtube-player) (Pierfrancesco Soffritti) | 13.0.0 | MIT | YouTube's own embedded player where YouTube refuses Cuelight's | [LICENSE](https://github.com/PierfrancescoSoffritti/android-youtube-player/blob/master/LICENSE) |

These libraries bring their own dependencies (Kotlin's standard library, Guava and other
AndroidX parts), all under Apache 2.0.

### Windows app only

| Component | Version | Licence | Where it is used | Licence text |
|---|---|---|---|---|
| [SDL2](https://libsdl.org) (MSYS2 build) | 2.32.10 | zlib | Window, input, audio (`SDL2.dll`) | [LICENSE.txt](https://github.com/libsdl-org/SDL/blob/release-2.32.10/LICENSE.txt) |
| [ANGLE](https://chromium.googlesource.com/angle/angle) (MSYS2 build) | 2.1.25748 | BSD-3-Clause | OpenGL ES over Direct3D (`libEGL.dll`, `libGLESv2.dll`) | [LICENSE](https://chromium.googlesource.com/angle/angle/+/refs/heads/main/LICENSE) |
| [curl](https://curl.se) | 8.11.0 | curl (MIT-style) | HTTP, with Windows' own TLS (`libcurl.dll`) | [COPYING](https://curl.se/docs/copyright.html) |
| [zlib](https://zlib.net) | 1.3 | zlib | Compressed data (`zlib1.dll`, and in FFmpeg) | [LICENSE](https://zlib.net/zlib_license.html) |
| [dav1d](https://code.videolan.org/videolan/dav1d) (VideoLAN; MSYS2 build) | 1.5.4 | BSD-2-Clause | AV1 decoded on the processor, where the graphics card cannot; linked statically into FFmpeg | [COPYING](https://code.videolan.org/videolan/dav1d/-/blob/1.5.4/COPYING) |
| GCC runtime (libgcc, libstdc++), [MinGW-w64 winpthreads](https://www.mingw-w64.org) | 16.2, 14.0 | GPL 3 with the GCC Runtime Library Exception; MIT and BSD | Loaded by ANGLE and the app (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) | [Runtime exception](https://www.gnu.org/licenses/gcc-exception-3.1.html), [winpthreads COPYING](https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/mingw-w64-libraries/winpthreads/COPYING) |
| [Khronos OpenGL ES headers](https://github.com/KhronosGroup/OpenGL-Registry) | | MIT | Building only (headers) | in the headers |

### On the TV, not bundled

On LG webOS, Cuelight uses the TV's own SDL2 (webOS fork), OpenGL ES, libcurl,
luna-service2 and media pipeline. They are part of the TV's system software and are not
shipped in the package.

## Services Cuelight talks to

Cuelight ships none of their data. It asks them at run time, only for the features you
use, and what they return stays under their terms:

| Service | Used for | Terms |
|---|---|---|
| [The Movie Database (TMDB)](https://www.themoviedb.org) | The built-in catalog, cast photos, age ratings | This product uses the TMDB API but is not endorsed or certified by TMDB. [Terms](https://www.themoviedb.org/api-terms-of-use) |
| [Open-Meteo](https://open-meteo.com) | The Weather clock face | Weather data by Open-Meteo.com, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [Internet Archive](https://archive.org) | The free fireplace video, streamed when none is set: [Holiday Fireplace](https://archive.org/details/betv-16515bcm-fireplace-plain) by Berkeley Community Media, then [Fireplace](https://archive.org/details/FireplaceStockFootage) by Vincent Wolf | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) and [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/), unchanged; credited on screen while they play |
| [Art Institute of Chicago API](https://api.artic.edu) | The gallery screensaver (public-domain works) | Data [CC0](https://creativecommons.org/publicdomain/zero/1.0/); images of public-domain works |
| [ip-api.com](https://ip-api.com) | Your approximate location, when you ask for it (breaks, weather) | Free for non-commercial use |
| Stremio (api.strem.io, link.stremio.com, Cinemeta, OpenSubtitles addon) | Optional account sync, sign-in by link, metadata | Stremio's terms; Cuelight is not affiliated with Stremio |
| Trakt, SimKL, MDBList, OpenSubtitles, TorBox | Optional accounts you sign in to | Each service's terms |
| TheIntroDB, AniSkip, Kitsu | Skip intro and credits markers | Each service's terms |
| IMDb (public pages and graph API) | Ratings, parents' guide, trailers | IMDb's terms |
| iTunes Search API | A title's soundtrack | Apple's terms |
| YouTube | Trailers, break recordings | YouTube's terms |
| Groq, OpenRouter, Google Gemini, or your own endpoint | The optional AI assistant, with your own key | Each provider's terms |

Your addons are third-party services of your choosing; Cuelight plays only what they
return.

## Used to build Cuelight, not shipped

- [Depth Anything V2 Small](https://huggingface.co/depth-anything/Depth-Anything-V2-Small)
  (Apache 2.0), through its [ONNX export](https://huggingface.co/onnx-community/depth-anything-v2-small),
  makes the depth maps for the Depth effect in CI (`tools/depth`). The model is not in
  the apps; the maps are fetched from this repository's `depth-maps` branch.
- The openlgtv/buildroot-nc4 toolchain, the Android SDK and NDK, Gradle and Emscripten
  build the apps; nothing of them ships except the runtime libraries listed above.

## Ideas, not code

Some features follow the design of other open-source projects: Stremio-Kai, the Nuvio
port [nuvio-native-legacy](https://github.com/iqui27/nuvio-native-legacy) and NuvioMobile
(GPL 3.0), [youtube-webos](https://github.com/webosbrew/youtube-webos) (GPL 3.0), and
stremio-core for the shapes of Stremio's API. Cuelight's code is written independently;
no code from those projects is copied into it.
