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
