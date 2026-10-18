# Home helper: seek previews and page links

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

## YouTube and other page links (adhan, dua, a bell)

A native TV app cannot play a YouTube page: the page is not a media file, and turning it
into one means running YouTube's own player code, which changes every few weeks. That is
what NewPipe's extractor does on Android and what yt-dlp does on a computer; both need
frequent updates and neither can run inside the TV app. So the helper does it:

```sh
pip install yt-dlp                         # once; update it now and then (pip install -U yt-dlp)
python3 tools/thumbs/server.py             # same server as the previews
```

On the TV, in Settings > Take breaks > Recording at the start (or at the end), paste the
YouTube link from your phone page. The app asks the helper for

    GET /media?url=<page>&kind=audio|video

which downloads it once with yt-dlp (audio as AAC `.m4a`, video as H.264 + AAC `.mp4`
up to 720p, which every LG and Samsung TV decodes), keeps it in the cache and serves it
with Range. The first request takes as long as the download; after that it is instant.
*Show the video* in the same settings plays the video full screen during the break.

Limits (flags): `--media-mb 200`, `--media-minutes 30`, `--media-timeout 300`. The Docker
image updates yt-dlp at every start. Only download what you have the right to use.
