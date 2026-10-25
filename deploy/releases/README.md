# Cuelight (beta)

> **Beta:** Cuelight is in active development. Expect rough edges; the Windows app is a
> preview. Settings > Diagnostics in the app shows a log to report a problem with.

*Your cue to press play.* Cuelight is a fast native player that works with your Stremio
account and addons: the same sign-in, addons, Library and Continue Watching. It runs on
LG webOS TVs (webOS 3.0 and newer), on Android phones, tablets, Android TV, Google TV
and Fire TV (Android 7 and newer), and as a preview on Windows 10 and 11.

> Cuelight is an independent project. It is not affiliated with, endorsed by or made by
> Stremio, LG, Google or YouTube. It plays only what your own addons and accounts provide.

This repository holds the builds. The newest is **${TAG}**: see
[Releases](https://github.com/${RELEASES_REPO}/releases) for what changed. The source is at
[smalik29871/stremio-native-tv](https://github.com/smalik29871/stremio-native-tv).

## Install on an LG TV

1. Install the [Homebrew Channel](https://github.com/webosbrew/webos-homebrew-channel) on your TV.
2. In the Homebrew Channel, open **Settings > Add repository** and enter:

   `https://github.com/${RELEASES_REPO}/releases/latest/download/repo.json`
3. Back in the app list, install **Cuelight** (listed as "Stremio (native)" before 0.10.57).

Or download the `.ipk` from the [latest release](https://github.com/${RELEASES_REPO}/releases/latest)
and install it with Developer Mode (`ares-install`) or Dev Manager.


## Install on Android

Download the `.apk` from the [latest release](https://github.com/${RELEASES_REPO}/releases/latest)
on the device and open it (allow installs from your browser or file app when asked).

## Install on Windows (preview)

Coming in a later release. Then: download `Cuelight-<version>-windows-x64.zip` from the
[latest release](https://github.com/${RELEASES_REPO}/releases/latest), unpack it anywhere and
run `Cuelight.exe` (Windows 10 or 11, 64-bit). The app is not signed, so Windows may say it
"protected your PC": *More info > Run anyway*. Your sign-in and settings are kept in your user
folder, so a newer version unpacked anywhere keeps them.

## Updates

Cuelight checks for new versions itself and asks before it installs one (Settings > About >
Update). On an LG TV the Homebrew Channel installs it; on Android, Android asks you to
confirm.

## Licence

Copyright (c) 2026 Shahrukh. All rights reserved. You may install and use these builds on
your own devices for personal, non-commercial use; redistributing them, or modified
versions, needs written permission. The full terms are in [LICENSE](LICENSE).

The builds include open-source components under their own licences, among them FFmpeg's
H.264 and HEVC decoders (LGPL 2.1, linked statically: the source repository has everything
needed to rebuild Cuelight against a changed FFmpeg), QuickJS-ng, cJSON, libwebp, the
P2P engine's Go modules (MPL 2.0 and others), the fonts (SIL OFL 1.1), and on Android
SDL2, curl, Mbed TLS, AndroidX Media3 and android-youtube-player, and on Windows SDL2,
ANGLE, curl and zlib. This product uses the
TMDB API but is not endorsed or certified by TMDB. Weather data by
Open-Meteo.com (CC BY 4.0). The free fireplace video is "Holiday Fireplace" by Berkeley
Community Media (CC BY-SA 3.0, Internet Archive). Every component, its version and licence text, and every
service the apps talk to are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
