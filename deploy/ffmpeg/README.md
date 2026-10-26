# FFmpeg in Cuelight

Cuelight uses [FFmpeg](https://ffmpeg.org) 7.1.1, unmodified, under the GNU Lesser General
Public License 2.1 or later (`COPYING.LGPLv2.1` here). No GPL or non-free parts are enabled.

- **Windows:** the player. FFmpeg's DLLs sit beside `Cuelight.exe`: `avcodec-61.dll`,
  `avformat-61.dll`, `avutil-59.dll`, `swresample-5.dll` and `swscale-8.dll`, with dav1d (AV1)
  and zlib built into them.
- **LG webOS:** seek previews. `lib/libavcodec.so.61` and `lib/libavutil.so.59`, loaded by
  the app when it needs them.

You may replace these libraries with your own build of FFmpeg 7.1 (the same major versions:
avcodec 61, avformat 61, avutil 59, swresample 5, swscale 8), and Cuelight will use it.

## Source

- FFmpeg 7.1.1: <https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz>, or the `n7.1.1` tag of
  <https://git.ffmpeg.org/ffmpeg.git> (mirrored at <https://github.com/FFmpeg/FFmpeg>).
- How it was configured and built: `ffmpeg-player.sh` (Windows) and `ffmpeg.sh` (webOS), here.
- dav1d 1.5.4 (BSD 2-clause): <https://code.videolan.org/videolan/dav1d>; zlib 1.3.1:
  <https://zlib.net>.

If you cannot get the source from these addresses, ask for it through the releases
repository's Issues and it will be sent to you, for at least three years after this release.
