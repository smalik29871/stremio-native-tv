# Playback stability validation

The playback changes are on `fix/playback-stability`. No release version is changed.

## Automated coverage

The CMake test suite includes these regression checks:

- `playback_progress`: paused and buffered time cannot mark a seeked-to ending watched;
  progress still accumulates when the backend reports position less often than the UI draws.
- `diagnostic_redaction`: logged links keep their host while removing user information,
  paths, queries and fragments, including short token values.
- `playback_diagnostics`: buffering excludes startup and user pauses, dropped-frame counts
  survive source changes, and optimistic playhead changes cannot complete seek measurements.
- `desktop_clock_and_timestamps`: audio underruns freeze the media clock, and stream
  timestamps with a nonzero origin are reported relative to the video's beginning.
  This test is enabled when building with `SNTV_PLAYER_FFMPEG`.
- `webos_cancelled_load`: a successful stale load is unloaded without replacing the current
  media session. This test uses a fake media service and runs in the Linux host suite.

`tests/smoke.sh` also checks a source requiring a Referer during preflight and playback.
Its URL assertions use redacted diagnostics. The shard checker accepts Windows paths and
reads the scripts as UTF-8.

Run the configured unit suite with `ctest --test-dir build --output-on-failure` and the
headless UI suite with `bash tests/smoke.sh build` on the normal Linux build machine.

## Checks performed locally

- The progress, redaction and playback diagnostics tests passed as Windows executables.
- The existing source-quality, media-probe and HTTP tests passed.
- Focused tests of the actual desktop timestamp, clock and audio callback functions passed.
- The webOS load-reply handler passed its focused fake-service test. The clock and stale-load
  tests also rejected the original implementations; the redaction test did likewise.
- Changed desktop C files passed syntax checks against SDL 2.30.9 and FFmpeg 7.1.1 headers.
- The Android Java sources compiled against the pinned Media3 1.4.1 dependencies and SDL
  2.30.9 sources using the locally available Android API 36 jar. This is a Java compile
  check, not an APK build or device test.

The complete Linux build and headless suite were not run locally: Ubuntu cannot start in
this environment. The portable checks do not validate TV firmware, decoders or rendering.

## Device acceptance checks

These checks remain pending on real devices. Use an older LG TV, a current LG TV, an
Android TV/Fire TV device, an Android phone, and a Windows PC with a real graphics card.

1. Play, pause, seek forward/back, change audio, change subtitles, stop, and start another
   title repeatedly. Include a resume near the ending and a long pause after seeking.
   Continue Watching must retain the actual resume point; a pause must not mark it watched.
2. On Android, finish the Activity and reopen it while the process remains alive. Repeat
   with Home, picture in picture, returning from another app, and screen rotation. Check
   that video, sound and controls return and that memory does not grow on every reopen.
3. On webOS, leave or change sources repeatedly while the media service is still loading.
   Playback must continue to start after repeated cancellations, without orphaned pipelines.
4. Introduce slow delivery, a complete connection drop, a refused link and an expired link.
   Check recovery, bounded retries, resume position, pause intent and track preferences.
5. Switch to an unsupported alternate audio track on Windows. Working sound must remain
   selected and playback must still reach its end.
6. Play MPEG-TS/HLS with a nonzero timestamp origin. Compare progress, seeking and subtitles
   against the same content in an established player.
7. Let the next episode warm up, then start it. Diagnostics should report reuse of its
   buffered prefix when that same source is selected. Warm-up is bounded to 2 MiB in memory;
   it does not allocate a second decoder. Verify it yields to active playback.
8. Test high-bitrate playback under memory pressure. Background work should yield when the
   buffer drains; stable playback with a small native buffer must still get subtitles and
   chapter information. Test different releases to check subtitle selection after fallback.
9. Share a diagnostic log from synthetic test links containing private paths, user
   information and signed query parameters. Their values must be absent from every output.

## Measurements

Diagnostics > Speed reports playback and buffering time, rebuffer percentage, stall count
and dropped frames where the backend supplies them. Android startup uses the first rendered
frame callback. Desktop seek recovery uses presentation of a picture from the new seek serial;
backends without that evidence omit seek latency, and paused seeks are omitted.

Measure cold and warm starts, p50/p95 seek recovery, failed starts, fallback interruptions and
memory across repeated episode transitions on each device. Suggested initial targets are a
p95 warm start below two seconds for a healthy cached source, buffering below 0.5%, and no
crashes during an overnight run. These are acceptance targets, not measured results or a
claim of superiority to another app.
