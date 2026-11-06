# Android phones and tablets: what to improve

Status: a plan from a research pass on 2026-11-06. What was checked in our own code is marked
**(checked)**; dates and rules taken from Google's pages or third parties that could not be
confirmed twice are **unverified**. Nothing here is built yet.

## Verdict

Cuelight is not on Google Play, so Play's deadlines do not bind it. Three platform changes matter
anyway, and two are breakages in code we have rather than policy:

1. **The landscape lock stops working on tablets.** Raising `targetSdk` to 36 makes Android ignore
   `screenOrientation` and `setRequestedOrientation()` on screens 600 dp and wider (the opt-out is
   gone at 37). Ours: `sensorLandscape` in the manifest **(checked)**, and the player turning a phone
   sideways through code. Tablets will rotate freely, so the player must survive rotation mid-film.
2. **The Back key goes away at targetSdk 36 on Android 16** (predictive back: `KEYCODE_BACK` is no
   longer sent). We trap Back with `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` and read `SDLK_AC_BACK`
   **(checked)**. SDL 2.30.9 is untested with this.
3. **16 KB memory pages.** We have no `max-page-size` linker flag anywhere **(checked)** and build
   with NDK r27.2 **(checked)**, which does not align for 16 KB by itself (r28 and newer do).
   Play requires it for updates from 1 Feb 2027; off Play, Android 15 and later warn about 4 KB
   libraries and a later release can refuse them. It covers libSDL2, our library, libcurl,
   mbedTLS, `libtorrentd.so` (Go), FFmpeg and `libc++_shared.so`.

The biggest user-visible gap is the missing background layer: no `MediaSession`, no notification
or lock-screen controls, no screen-off audio **(checked: only docs/ANDROID_TV.md mentions one)**.

Tablets are the weakest area: a phone and a tablet kind exist, but no navigation rail, no two-pane
layout and little hardware-keyboard work.

## Compliance items

| Item | When | Do | Size |
|---|---|---|---|
| Target 36 | Play: new apps and updates from 31 Aug 2026, extension to 1 Nov 2026 (unverified for us: off Play) | The way to the next three items | S |
| Ignore orientation and resizability on 600 dp and wider | At target 36; no opt-out at 37 | Window-size and rotation decisions in the player; survive rotation and resize mid-film | M |
| Predictive back | Target 36 on Android 16 | An `OnBackInvokedCallback` forwarding to the app's Back, or `android:enableOnBackInvokedCallback="false"` and a test | M, device |
| Edge-to-edge | Already enforced at 35 | Replace the deprecated system-bar calls with `WindowInsetsController`; test gesture and 3-button navigation and cutouts | S |
| 16 KB alignment | Play: updates from 1 Feb 2027 | `-Wl,-z,max-page-size=16384` on every native library, or NDK r28+; check with `llvm-readelf` and `zipalign -c -P 16`; the Go binary separately | M |
| Background audio (Android 17) | Android 17 | If we add it: a Media3 session service and the `mediaPlayback` foreground service type | M |
| Local network permission | Mandatory at target 37 (unverified date) | LAN addons, our LAN `torrentd` and Cue Party discovery need `ACCESS_LOCAL_NETWORK` and a prompt | M |
| Developer verification | 30 Sep 2026 in four countries, global 2027 (unverified for plain sideloading) | Register the package name and signing key, or accept the advanced install flow; whether our self-update is affected is unknown | S |

## Improvements, ranked

1. **Move off the Android 15 assumptions:** Back handling, target 36, a rotation-safe player.
   Medium, high risk, needs an Android 16 device. Touches the manifest, `CuelightActivity`,
   `CuelightPlayer.java`, `src/ui/input.c`, `src/main.c`.
2. **16 KB alignment for all native code** plus a CI check. Medium, low risk, mostly verifiable
   without a device (ELF inspection, a 16 KB emulator).
3. **Media3 session service:** lock-screen and notification controls, headset and Bluetooth
   buttons. Medium, medium risk, device.
4. **Background and screen-off audio** as a setting, on the same service. Medium, device.
5. **Modern picture-in-picture:** auto-enter, a source rectangle, remote actions (play, pause,
   seek). Small to medium, device.
6. **Tablet layouts:** a navigation rail from the medium width class up, list and detail panes for
   Search and Library, the title page beside its sources, grid columns by width. Large, medium
   risk, mostly checkable in the layout tests.
7. **Keyboard, mouse and stylus:** Space, arrows, Esc, F, hover, right click, wheel; the on-screen
   keyboard must give way to a physical one. Medium; mapping is checkable here, feel needs a tablet.
8. **Foldables and resizable windows:** split-screen, DeX, a tabletop posture for the player.
   Medium, needs a foldable or the resizable emulator.
9. **Player gestures:** brightness (left), volume (right), horizontal seek, long press for 2x,
   double tap to skip. Small to medium.
10. **Chromecast** (not on Fire or devices without Google services; needs the local-network
    permission at target 37). Large, high risk.
11. **Offline downloads.** Large; sources are debrid and addon links, so defer.
12. **Accessibility pass:** TalkBack through the existing bridge, font scale, 48 dp targets, focus
    visibility. Medium, devices.
13. **Per-app language, data saver, battery.** Small to medium.
14. **SDL upgrade** (SDL3 has safe-area and newer inset handling, but needs a newer NDK and Android
    SDK): plan it together with item 2. Large, high risk.

## Apps to learn from (our code is all rights reserved: ideas, not code)

| App | Licence | Take |
|---|---|---|
| Nuvio Mobile | GPL-3.0 | Ideas only |
| Findroid | GPL-3.0 | Downloads and the dual-player fallback idea |
| Streamyfin | MPL-2.0 | Download UX; avoid copying files |
| mpv-android | MIT (confirm the text and its dependencies) | Gesture feel and subtitle styling; code could in principle be reused with notice |
| Jellyfin Android | GPL-2.0 | The Cast pattern |
| Stremio Android, Plex, Infuse | Closed source | Ideas only |
| VLC, Kodi | Believed GPL (unverified) | Ideas only |

## Not verified

The Android 17 date and Play's date for target 37; whether developer verification touches plain
sideloading and our self-updater; which SDL release fixed which Android issue (no SDL2 backport of
the inset handling was found); the alignment of our built libraries (nobody ran `readelf`); Media3
1.4.1 against the current version; foldable posture and stylus specifics; licences of VLC, Kodi,
Infuse and Plex; and Samsung tablet typing and rotation, which stay untested on real devices.
