# Roadmap

Ordered by what unblocks real use on a TV.

1. **First device bring-up (webOS).** Cross-compile with buildroot-nc4, install the `.ipk`,
   confirm UI, keys, networking (libcurl `.so.5`), then `com.webos.media` playback.
   Everything in `docs/PORTING-NOTES.md` section 1.
2. **Subtitles.** `addon_parse_subtitles()` exists; add fetching from subtitle addons
   (`/subtitles/{type}/{videoId}/videoHash=..&videoSize=..&filename=..json`), SRT/VTT
   parsing, and an overlay in `screen_player.c`. Embedded ASS later via libass.
3. **Audio / subtitle track switching** through `selectTrack` on webOS and
   `player_select_track` on Tizen.
4. **`http_wasm.c`** so the Tizen `.wgt` build is functional.
5. **Binge / next episode**: on `VIDEO_ENDED` for a series, pick the next video and reuse
   the stream's `bingeGroup` when the same addon offers one.
6. **Catalog paging** with the `skip` extra and genre filters from `extra.options`.
7. **Disk cache for posters** (Nuvio opens Home in ~1 s from cache).
8. **Login with QR / link code** via `link.stremio.com` (`LinkCodeResponse` in stremio-core)
   so nobody types a password with a remote.
9. **Trakt / debrid / profiles** are account-side features in Stremio; expose what the
   addon collection already carries, nothing more.
10. **Samsung .NET host** for 2018-2020 Tizen sets, mirroring `tizen-tpk/` in the reference project.
11. **Seek preview thumbnails** — done as an opt-in in 0.5.6 via a self-hosted ffmpeg
    server (`tools/thumbs`), one small JPEG per request, nothing held in texture memory
    beyond the frames near the seek point. The original note, for the record: On LG and Samsung the video is a
    hardware plane the app cannot read, so frames cannot be grabbed locally the way
    desktop players (mpv's thumbfast in Stremio-Kai) do it. Nuvio's experimental version
    uses seekr.tv: each viewer needs their own Seekr key, and one sprite sheet decodes to
    about 22 MB of texture, which is a real risk on 2017-2019 TVs. Revisit as an opt-in
    with a hard memory cap and the sheet dropped after seeking.
