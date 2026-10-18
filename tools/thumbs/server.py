#!/usr/bin/env python3
"""Seek preview server for Stremio (native TV).

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
"""
import argparse, hashlib, http.server, os, shutil, subprocess, sys, threading, time, urllib.parse

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
            return self.reply(200, b"ok")
        if u.path != "/frame":
            return self.reply(404, b"use /frame?url=...&t=seconds")
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
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == "__main__":
    main()
