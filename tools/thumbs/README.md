# Seek previews

LG and Samsung TVs decode video on a hardware plane the app cannot read, so a picture
of "where you are seeking to" has to come from a machine that can decode the stream.
This tiny server does that with ffmpeg and caches the frames.

## Run it

```sh
python3 tools/thumbs/server.py            # listens on :11480, cache in ~/.cache/stremio-thumbs
# or
docker build -t stremio-thumbs tools/thumbs && docker run -d -p 11480:11480 -v thumbs:/cache stremio-thumbs
```

Then on the TV: **Settings > Playback > Seek preview server** → `http://<machine ip>:11480`.

While you hold Left/Right the player asks for the frame at the target time (rounded to
10 s) and shows it above the time bubble as soon as it arrives; neighbouring frames are
fetched ahead. Nothing is requested while you are not seeking, and only the URL of the
video you are watching is sent to the server.

## Notes

- One frame costs ffmpeg a few MB of the file (it seeks with HTTP ranges) and about a
  second on a small NAS; cached frames are instant.
- Debrid links expire; the cache key includes the URL, so a new link starts fresh.
- The server is unauthenticated: keep it on your LAN.
