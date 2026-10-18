#!/usr/bin/env python3
"""Home helper for Stremio (native TV): seek previews, and YouTube or other page links as media.

The TV cannot read frames from its own video plane, so previews while seeking have to
come from somewhere that can decode the stream: this script, running on any computer or
NAS on your network that has ffmpeg.  The app asks

    GET /frame?url=<video url, percent-encoded>&t=<seconds>

and gets a small JPEG (320 px wide by default) of the frame at that time, cut to the
nearest 10 s and cached on disk so a second viewer or a second pass is instant.

    python3 server.py --port 11480 --cache ~/.cache/stremio-thumbs

Then in the app: Settings > Playback > Seek preview server -> http://<this machine>:11480

Only URLs of the video being played are requested; the server fetches at most the bytes
ffmpeg needs to decode one frame (a few MB with Range requests).  No key, no account.

It also turns a page link (YouTube, SoundCloud, archive.org, Facebook... anything yt-dlp
knows, the same job NewPipe's extractor does on Android) into a file the TV can play:

    GET /media?url=<page url>&kind=audio|video

downloads it once with yt-dlp (audio as AAC .m4a, video as H.264 + AAC .mp4 up to 720p,
which every LG and Samsung TV decodes), keeps it in the cache and serves it with Range.
The TV uses this for the adhan, a dua or a bell at the start and end of a break.  Keep
yt-dlp current (pip install -U yt-dlp): YouTube changes and old versions stop working.
"""
import argparse, glob, hashlib, http.server, json, os, shutil, subprocess, sys, threading, time, urllib.parse

ARGS = None
LOCK = threading.Lock()
INFLIGHT = {}


def cache_path(url, t):
    key = hashlib.sha1(f"{url}\n{t}\n{ARGS.width}".encode()).hexdigest()
    return os.path.join(ARGS.cache, key[:2], key + ".jpg")


def grab(url, t, dest):
    tmp = dest + ".part"
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-ss", str(t), "-i", url,
           "-frames:v", "1", "-vf", f"scale={ARGS.width}:-2", "-q:v", "4", "-y", "-f", "image2", tmp]
    if url.startswith("http"):
        cmd[4:4] = ["-user_agent", "StremioNativeTV-thumbs/1", "-reconnect", "1", "-reconnect_streamed", "1"]
    try:
        subprocess.run(cmd, timeout=ARGS.timeout, check=True)
        os.replace(tmp, dest)
        return True
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"ffmpeg failed for t={t}: {e}\n")
        try: os.remove(tmp)
        except OSError: pass
        return False


def ytdlp_cmd():
    """yt-dlp as a program, or as the Python module when only that is installed."""
    exe = shutil.which("yt-dlp")
    if exe: return [exe]
    try:
        import yt_dlp  # noqa: F401
        return [sys.executable, "-m", "yt_dlp"]
    except ImportError:
        return None


def media_path(url, kind):
    key = hashlib.sha1(f"{url}\n{kind}".encode()).hexdigest()
    return os.path.join(ARGS.cache, "media", key[:2], key + (".m4a" if kind == "audio" else ".mp4"))


def fetch_media(url, kind, dest):
    """Download `url` with yt-dlp into dest. Returns (ok, message)."""
    cmd = ytdlp_cmd()
    if not cmd:
        return False, "yt-dlp is not installed on the helper (pip install yt-dlp)"
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    base = dest[:-4] + ".dl"
    for old in glob.glob(base + "*"):
        try: os.remove(old)
        except OSError: pass
    common = ["--no-playlist", "--no-progress", "--quiet", "--no-warnings", "--max-filesize", f"{ARGS.media_mb}M",
              "--match-filter", f"duration <? {ARGS.media_minutes * 60}", "-o", base + ".%(ext)s"]
    if kind == "audio":
        fmt = ["-f", "ba[ext=m4a]/ba/b", "-x", "--audio-format", "m4a"]
    else:
        fmt = ["-f", "bv*[height<=720][vcodec^=avc1]+ba[ext=m4a]/b[ext=mp4][height<=720]/bv*[height<=720]+ba/b",
               "--merge-output-format", "mp4", "--remux-video", "mp4"]
    title_opt = ["--no-simulate", "--print", "after_move:title"]
    try:
        r = subprocess.run(cmd + common + fmt + title_opt + ["--", url], timeout=ARGS.media_timeout, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return False, "the download took too long"
    got = [p for p in glob.glob(base + ".*") if not p.endswith((".part", ".ytdl"))]
    if r.returncode != 0 or not got:
        msg = (r.stderr or r.stdout or "").strip().splitlines()
        return False, (msg[-1] if msg else "yt-dlp could not get this link")[:300]
    os.replace(got[0], dest)
    title = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
    with open(dest + ".json", "w") as f: json.dump({"url": url, "title": title}, f)
    return True, title


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quieter
        if ARGS.verbose: sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def reply(self, code, body=b"", ctype="text/plain"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "public, max-age=86400")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        if self.command != "HEAD": self.wfile.write(body)

    def do_HEAD(self): self.do_GET()

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        if u.path == "/health":
            return self.reply(200, json.dumps({"ok": True, "frames": True, "media": ytdlp_cmd() is not None}).encode(), "application/json")
        if u.path == "/media":
            return self.media(urllib.parse.parse_qs(u.query))
        if u.path != "/frame":
            return self.reply(404, b"use /frame?url=...&t=seconds or /media?url=...&kind=audio|video")
        q = urllib.parse.parse_qs(u.query)
        url = q.get("url", [""])[0]
        try:
            t = max(0, int(float(q.get("t", ["0"])[0]) // ARGS.step * ARGS.step))
        except ValueError:
            return self.reply(400, b"bad t")
        if not (url.startswith("http://") or url.startswith("https://")):
            return self.reply(400, b"only http(s) urls")
        dest = cache_path(url, t)
        if not os.path.exists(dest):
            with LOCK:
                ev = INFLIGHT.get(dest)
                if ev is None:
                    ev = INFLIGHT[dest] = threading.Event()
                    owner = True
                else:
                    owner = False
            if owner:
                try: grab(url, t, dest)
                finally:
                    ev.set()
                    with LOCK: INFLIGHT.pop(dest, None)
            else:
                ev.wait(ARGS.timeout + 5)
        if not os.path.exists(dest):
            return self.reply(502, b"could not decode a frame")
        with open(dest, "rb") as f:
            self.reply(200, f.read(), "image/jpeg")


    def media(self, q):
        url = q.get("url", [""])[0]
        kind = "video" if q.get("kind", ["audio"])[0] == "video" else "audio"
        if not (url.startswith("http://") or url.startswith("https://")):
            return self.reply(400, b"only http(s) urls")
        dest = media_path(url, kind)
        if not os.path.exists(dest):
            with LOCK:
                ev = INFLIGHT.get(dest)
                owner = ev is None
                if owner: ev = INFLIGHT[dest] = threading.Event()
            msg = ""
            if owner:
                try:
                    ok, msg = fetch_media(url, kind, dest)
                    if not ok: sys.stderr.write(f"media failed for {url}: {msg}\n")
                finally:
                    ev.set()
                    with LOCK: INFLIGHT.pop(dest, None)
            else:
                ev.wait(ARGS.media_timeout + 5)
            if not os.path.exists(dest):
                return self.reply(502, (msg or "could not get this link").encode(), "text/plain; charset=utf-8")
        os.utime(dest)   # recently used: the pruner keeps it
        title = ""
        try:
            with open(dest + ".json") as f: title = json.load(f).get("title", "")
        except Exception:  # noqa: BLE001
            pass
        self.send_file(dest, "audio/mp4" if kind == "audio" else "video/mp4", title)

    def send_file(self, path, ctype, title=""):
        size = os.path.getsize(path)
        a, b, partial = 0, size - 1, False
        rng = self.headers.get("Range", "")
        if rng.startswith("bytes="):
            s, _, e = rng[6:].partition("-")
            try:
                if s: a = int(s); b = int(e) if e else size - 1
                elif e: a = max(0, size - int(e))
                if a < size: partial = True
                else: a, b = 0, size - 1
            except ValueError:
                a, b = 0, size - 1
            b = min(b, size - 1)
        self.send_response(206 if partial else 200)
        self.send_header("Content-Type", ctype)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(b - a + 1))
        if partial: self.send_header("Content-Range", f"bytes {a}-{b}/{size}")
        if title: self.send_header("X-Title", urllib.parse.quote(title))
        self.send_header("Cache-Control", "public, max-age=86400")
        self.end_headers()
        if self.command == "HEAD": return
        with open(path, "rb") as f:
            f.seek(a)
            left = b - a + 1
            while left > 0:
                chunk = f.read(min(65536, left))
                if not chunk: break
                try: self.wfile.write(chunk)
                except (BrokenPipeError, ConnectionResetError): return
                left -= len(chunk)


def prune(cache, limit_mb):
    """Keep the cache under limit_mb by dropping the oldest files."""
    files = []
    for root, _, names in os.walk(cache):
        for n in names:
            p = os.path.join(root, n)
            try: st = os.stat(p)
            except OSError: continue
            files.append((st.st_mtime, st.st_size, p))
    total = sum(s for _, s, _ in files)
    for _, s, p in sorted(files):
        if total <= limit_mb * 1024 * 1024: break
        try: os.remove(p); total -= s
        except OSError: pass


def main():
    global ARGS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=11480)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--cache", default=os.path.expanduser("~/.cache/stremio-thumbs"))
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--step", type=int, default=10, help="seconds between distinct frames")
    ap.add_argument("--timeout", type=int, default=25, help="seconds ffmpeg may take per frame")
    ap.add_argument("--cache-mb", type=int, default=500)
    ap.add_argument("--media-mb", type=int, default=200, help="largest page-link download")
    ap.add_argument("--media-minutes", type=int, default=30, help="longest page-link video")
    ap.add_argument("--media-timeout", type=int, default=300, help="seconds a page-link download may take")
    ap.add_argument("--verbose", action="store_true")
    ARGS = ap.parse_args()
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg is not installed (apt install ffmpeg / brew install ffmpeg)")
    os.makedirs(ARGS.cache, exist_ok=True)

    def pruner():
        while True:
            time.sleep(600)
            prune(ARGS.cache, ARGS.cache_mb)
    threading.Thread(target=pruner, daemon=True).start()
    srv = http.server.ThreadingHTTPServer((ARGS.bind, ARGS.port), Handler)
    print(f"seek previews on http://{ARGS.bind}:{ARGS.port}/frame  (cache {ARGS.cache})")
    print("page links on /media: " + ("yt-dlp found" if ytdlp_cmd() else "yt-dlp NOT installed (pip install yt-dlp)"))
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == "__main__":
    main()
