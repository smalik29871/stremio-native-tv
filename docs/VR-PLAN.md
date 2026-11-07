# Cuelight VR: plan

Status: plan only, nothing built. Written from a research pass on 2026-11-06; the items marked
**unverified** could not be confirmed from where it was done and must be checked before anything
depends on them. Hardware numbers (frame times, passthrough cost, foveation) must come from a
real headset before any of it is called done (CLAUDE.md).

## What is already out there

- **Stremio has its own VR app on Quest** (April 2025, Quest 2, 3, 3S and Pro, closed source):
  180/190/200/220/360, side-by-side and over-under 3D, HDR tone mapping, controllers. Its reviews
  ask for a movable screen, cinema environments and less lag. We are not first, so we win on
  quality and on what only we have.
- Skybox (environments, passthrough, curved screen, 3D, subtitles; paid, local files, no addons),
  DeoVR (rooms with voice), Bigscreen (social theatres, cloud-heavy), Virtual Desktop (DRM games),
  Netflix (Quest app shut down; browser only). Plex has no Quest app. Jellyfin has third-party
  clients (SpatialFin on Android XR).
- **Ambient light from the picture is already claimed by several apps** (Moon Player, AuraPlay,
  Spatial TV and others; marketing claims, untested). It is not a differentiator by itself; how
  good and how cheap it is can be.
- **Commercial streaming is DRM-capped in VR.** Addon and debrid streams are not DRM, which is a
  real advantage for Cuelight.
- No mature open-source OpenXR cinema app exists to build on.

## Platforms

| Platform | Route | Verdict |
|---|---|---|
| Meta Quest 3, 3S (also Pro, 2) | Android APK, OpenXR (Khronos loader), GLES | **First target.** Reuses the Android NDK build, GLES renderer and ffmpeg/MediaCodec work |
| Android XR / Samsung Galaxy XR | OpenXR 1.x; the manifest declares `libopenxr.google.so` | Second. The existing APK may already run as a flat panel (unverified) |
| Pico 4 / 4 Ultra | Native OpenXR | Cheap follow-on once Quest works |
| Valve Steam Frame | Android APKs announced Nov 2025 | Opportunistic; launch status unverified |
| PC VR (SteamVR, Monado, WiVRn, ALVR) | The desktop build plus the OpenXR loader | Low priority; Monado is useful as a headset-free test runtime on Linux |
| Apple Vision Pro | Metal and visionOS only | Not realistic: a separate Swift/Metal port |

## Open-source we can use (our code is all rights reserved: permissive only)

- **Khronos OpenXR-SDK / OpenXR-SDK-Source / OpenXR-Tutorials (Apache-2.0):** the loader, headers
  and the `hello_xr` shape for session, swapchains and layers. Link and copy, keeping notices and
  adding them to the third-party notices file.
- **Meta OpenXR SDK samples (Oculus SDK License):** read, do not copy. Our own code from the
  Khronos spec.
- **GPL projects (WiVRn, SpatialFin, mpv by default):** ideas only. No code, no static linking.
- **MIT:** ALVR (optional PC-VR bridge), StereoKit and Godot XR Tools (reference only; we use raw
  OpenXR because our renderer is our own).
- **Monado:** test runtime only (BSL-1.0 per distro listings; bundled parts differ: unverified).

## Architecture

1. **App shape.** An Android NDK activity with `XR_KHR_opengl_es_enable`, the Khronos loader and
   an OpenXR session, as a second entry point next to the SDL one. The same core (addons,
   sources, library, sync, subtitles) runs underneath.
2. **The UI on a quad.** Our canvas renderer draws into an OpenXR swapchain image (bound to an
   FBO) and is submitted as a quad layer. It redraws only when the UI is dirty, as it does
   today. The compositor samples layers at display resolution, so text is sharper than in an
   eye-buffer render.
3. **Pointer.** The controller or hand aim pose is intersected with the quad and turned into the
   canvas tap events the touch UI already understands (trigger or pinch is a tap, the thumbstick
   scrolls). Our remote-style focus model maps to the thumbstick for controller-less use.
   Comfortable distance is about 0.5 to 3 m; pinch jitter means small targets need slack.
4. **Video: two paths.**
   - *B (default):* MediaCodec to a SurfaceTexture, our shader draws into a swapchain. Costs a
     little compositor sharpness, gives full control of 3D, depth, tone mapping and the pixels
     the lighting needs.
   - *A (optional, 8K 360):* MediaCodec straight into an `XR_KHR_android_surface_swapchain`
     surface on a quad/cylinder/equirect layer. Zero-copy, but the app never sees the pixels, so
     no room lighting in that mode.
5. **3D and projections.** Left and right eye layers with `eyeVisibility`; SBS, over-under,
   180/360 projections in the shader. MV-HEVC in our ffmpeg build is unverified.
6. **Budget.** 72 Hz default (13.9 ms); higher rates through `XR_FB_display_refresh_rate`; 60 Hz
   exists for media apps. No official 24 fps cadence guidance: test 72 against 120 Hz judder on a
   device. Foveation (`XR_FB_foveation`) has reports of silent failure with GL: verify. Passthrough
   cost: not documented, measure.
7. **Distribution.** Sideload with `adb` through Developer Mode first (a verified developer
   account, age 18+). The store needs a signed release build, a privacy policy, an age rating and
   review (about two weeks, one source); release channels allow invite testing.

## What makes it better (ranked; effort and risk are relative to our code)

1. **A lit cinema room** (medium effort, medium risk). The Living Letterbox colours become light
   in the room: spill on walls, floor and seats, brightness following the scene. Needs path B and
   a GPU budget; quality and low cost are the edge, since others claim the idea.
2. **Subtitles in 3D** (low to medium, low). Our own subtitle renderer on a depth-placed plane that
   stays readable on bright scenes and avoids stereo conflicts. Best effort-to-win ratio.
3. **Comfort and smart presets** (low to medium, low). Screen size and distance chosen from the
   content and field of view, curved or flat, a dim surround that follows the scene, 24 fps
   handling. Needs real frame timing.
4. **Sources and library as a spatial wall** (medium, low). Posters on a wall and our source picker
   (quality, debrid status, release group stars) in space. The data and focus model already exist.
5. **Depth-map parallax backdrops** (medium, medium to high). Our depth maps turn the title art
   into a window you can lean into. No shipping rival found (search was not exhaustive); comfort
   at six degrees of freedom is the risk.
6. **Cue Party as a shared cinema** (high, high). Seats, simple presence and reactions on top of
   the room sync we have. Ship presence dots and shared reactions first; avatars, voice and
   moderation later.
7. **Mixed-reality mode** (medium, medium). Tint and dim the real room from the film's colours and
   anchor a movable screen to a wall. Needs passthrough, and the headset-camera permission if the
   camera picture is used (Quest 3, 3S; review required).
8. **Universal 3D and 180/360** (medium to high, medium). Table stakes: Stremio VR and Skybox have
   it. Needed, but not the story.

## Phases

0. **Spike (a week):** an OpenXR session on Quest from our NDK build that shows one static quad of
   our Home screen, with controller pointing and taps. Measures the real frame cost of the
   canvas on a quad. Needs a headset; nothing below should start before this answer.
1. **Flat cinema:** path B video on a curved or flat screen, resizable and movable, our player
   controls on a quad, subtitles, the comfort presets. Sideload build.
2. **The story:** lit room, 3D subtitles, the source wall.
3. **3D and 360:** projections and stereo.
4. **Together:** Cue Party presence, then more.
5. **Reach:** Android XR and Pico builds, store submission.

## Open questions (to answer on a device, not at a desk)

- Does today's Cuelight APK run as a flat window on Quest or Galaxy XR?
- Frame cost of our canvas on a quad; text sharpness against the compositor's filtering.
- Passthrough and foveation costs; 72 against 120 Hz at 24 fps films.
- ffmpeg MV-HEVC support in our build.
- Loader packaging in our Gradle build.
- Whether the store accepts a debrid and addon app (policy review, not a technical question).

## The Quest build as it stands (controls and film)

Controllers: trigger taps where the ray meets the screen; A is OK, B is Back, X plays or pauses, Y
changes the view (flat, 3D side by side, 3D top and bottom, 180 and 360 degrees); the thumbstick is the
arrow keys (seek in the player); the left menu button brings the screen back in front of you; the grip
takes hold of the screen and carries it, and while it is held the stick pushes and pulls the screen
(up and down) and makes it bigger or smaller (right and left).

Film: ExoPlayer decodes into the Android surface of an OpenXR swapchain
(`XR_KHR_android_surface_swapchain`), shown as a quad behind the panel (the player screen leaves a
see-through hole there), or as the inside of a sphere (`XR_KHR_composition_layer_equirect2`) for 180 and
360 degree films. 3D films are the two halves of that one surface, one for each eye. A release name with
SBS, HSBS, 3DH, OU, TB, 180 or 360 in it picks the view; Y changes it.

Not checked on a headset: which way up the surface's rectangle runs for top and bottom films (a swapped
pair of eyes means it is the other way), the runtime's treatment of the create-info fields for a surface
swapchain (two forms are tried), HDR films (the Quest 2 shows SDR, so they will look flat until tone
mapping is added), and everything about frame timing. Still to build: a curved screen, subtitles in 3D,
a lit room, the player's own controls drawn for pointing at in space.

### The Cinema sheet

The left menu button (or the small "Cinema" chip at the bottom left, for hands) opens a sheet over the
screens, kept in vr.cfg: Screen flat or curved; Size small, cinema, big, huge or fit the film (a wide film
gets a wider screen); Follow my head (stay, lazy: it comes round once you have turned well away, always:
it eases after you); Lying down (the screen tilts to wherever you look, up at a ceiling); Room (dark, or
the headset's cameras behind the screen); Distance; Film view; Bring it in front of me. Hands work as a
pointer where the runtime offers hand interaction (a pinch is the trigger).

Geometry (the curved screen's ray, the angles the screen is placed by, the tilt) is unit-tested. Not
checked on a headset: where the runtime puts a curved layer relative to its pose (the code takes the
pose as the bend's axis and the screen a radius ahead of it; if the curved screen shows at the wrong
distance, that is the place), the hand profile's path names, and passthrough (grey on the Quest 2).

### The lit room and the floating subtitles

**Room light** (Cinema sheet: off, soft, strong; on by default). The film no longer goes straight to the
compositor: ExoPlayer decodes into a SurfaceTexture, `vr_film.c` draws each picture into the screen's swapchain,
boils it down on the GPU to a 32 x 18 grid of colours (eased over time, nothing read back to the CPU), and
draws five soft glows from the grid's edges onto five quad layers: the wall behind the screen, the floor, the
ceiling and the two side walls, each fading with distance from the screen and with how dark the scene is. Over
the passthrough room they tint your real walls. With the light off the film takes the zero-copy path as
before; if the texture path delivers no picture in 4.5 s it turns itself off (and says so).

**Subtitles** (on the screen, in front, close up; in front by default). The lines the player screen draws
are caught in `ui_subtitle_ex` and drawn into a swapchain of their own, a layer that floats 22 % (in front) or
42 % (close up) of the way toward you from the screen, scaled so it looks the same size and sits in the same
direction. In 3D films and in 180/360 views they then sit nearer than the picture, in true depth, instead of
fighting it. The layer's rectangle is centred on the image so it does not matter which way up the runtime
counts rows.

3D eyes (normal or swapped) turns the two halves of a side-by-side or top-and-bottom film around.

Not checked on a headset: all of it. The picture orientation through the texture path, the room's look and
strength (the falloff constants are in the shader), the cost of the passes on the Quest 2's GPU, and
whether a runtime reads the layers as premultiplied the way they are drawn.

### Moving and sizing the screen

The dock: point near the bottom of the screen and a bar fades in under it (and out again after a moment). Hold
the trigger on **Move** and drag to carry the screen about; hold it on **Resize** and drag sideways to make it
bigger or smaller; tap **Smaller**, **Bigger**, **Nearer**, **Farther** (hold to repeat), **Curve**, **View**,
**Cinema** (the sheet) or **Center**. With both grips closed, pull the hands apart or together to stretch the
screen. The old ways still work: one grip carries it and the stick pushes, pulls and sizes it, and the menu
button opens the sheet. The distance you leave it at is the one the sheet shows and Center returns to.

### The control box (replaces the sheet and the bottom chip)

The left menu button opens a box that floats in front of you (and goes when pressed again; B closes it too). Its
first page is what to do now: Back, Home, Search; -10 s, play or pause, +10 s, -1 and +1 minute, next episode,
another source, show the bar; the screen smaller, bigger, nearer, farther, curved or flat, centred; the film view.
Its second page is how things are (the old sheet's choices). It is drawn by the app's renderer into a layer of its
own and pointed at like any other; taps on the screen behind it still reach the app.

The right thumbstick moves the screen with nothing held (up and down away and near, left and right bigger and
smaller); the left thumbstick is the arrow keys (move about, seek). B goes back one screen and, at the first
screen, does nothing (it used to close the app). The Meta button stays the system's: its long press recentres the
whole view, and the screen follows (a recentre brings it back in front of you).

### Updating from inside the headset

Each Quest build the CI makes is a newer version of the one before (the fourth part of its version is the run
number: 0.10.117.14, and the Android version code follows). When the release key is present the CI puts the APK and
a small manifest (quest.json: version, apkUrl, sha256) into the releases repository's fixed "quest" pre-release,
never "latest" (that is every other device's). The app asks for that manifest at start and every six hours (the
Quest build only), says so when something newer is there (the menu box shows "Update ready" on its first page and
Check now / Install on its second), and installs the way the Android app does: it downloads, checks the checksum,
and hands the file to Android's installer (the first time Android asks to allow Cuelight to install apps).

The first install has to be sideloaded as before; later ones come from inside. Not checked on a headset: whether
Horizon OS shows the package installer for an app that asks (the same flow as the phone app's).
