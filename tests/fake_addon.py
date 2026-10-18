#!/usr/bin/env python3
"""A minimal Stremio addon served from the test fixtures, for offline smoke tests.

    python3 tests/fake_addon.py 8765
Serves /manifest.json, /catalog/{type}/{id}.json (with optional /{extra}.json),
/meta/{type}/{id}.json, /stream/{type}/{id}.json and generated poster PNGs."""
import json, os, sys, io, struct, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
BASE = f"http://127.0.0.1:{PORT}"


def png(w, h, rgb, pattern=False):
    rows = []
    for y in range(h):
        if pattern:   # checkerboard so blur is visible in screenshots
            row = bytearray()
            for x in range(w):
                on = ((x // 24) + (y // 24)) % 2
                row += bytes(rgb) if on else bytes((255, 255, 255))
            rows.append(b"\x00" + bytes(row))
        else:
            rows.append(b"\x00" + bytes(rgb) * w)
    raw = b"".join(rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def load(name):
    with open(os.path.join(FIX, name)) as f:
        return json.load(f)


MANIFEST = load("cinemeta_manifest.json")
MANIFEST["id"] = "org.example.fake"
MANIFEST["name"] = "Fake Addon"
MANIFEST["resources"] = ["catalog", "meta", "stream", "subtitles"]
CATALOG = load("catalog.json")
for i, m in enumerate(CATALOG["metas"]):
    m["poster"] = f"{BASE}/poster/{i}.png"
    m["background"] = f"{BASE}/bg/{i}.png"
    m.setdefault("description", "A chemistry teacher diagnosed with a terminal illness turns to making methamphetamine to secure his family's future.")
    m.setdefault("cast", ["Ada Lovelace", "Alan Turing", "Grace Hopper"])
# pad the catalog so rows scroll
extra = []
NAMES = ["Midnight Harbor", "The Glass Orchard", "Northern Lights", "Paper Kingdoms", "Echoes of Mars", "Silent Tides",
         "Neon Requiem", "The Last Lighthouse", "Wild Atlas", "Crimson Valley", "Blue Hour", "Hollow Crown"]
GEN = [["Drama", "Mystery"], ["Sci-Fi", "Adventure"], ["Thriller"], ["Romance", "Drama"], ["Action", "Sci-Fi"], ["Documentary"]]
for i in range(12):
    extra.append({"id": f"tt{1000+i}", "type": "movie", "name": NAMES[i], "poster": f"{BASE}/poster/{i+3}.png",
                  "background": f"{BASE}/bg/{i+3}.png", "releaseInfo": str(2012 + i), "imdbRating": f"{6 + i % 4}.{(i * 3) % 10}",
                  "genres": GEN[i % len(GEN)], "runtime": f"{95 + i * 4} min",
                  "description": "When a quiet engineer finds a signal hidden inside an old broadcast, she follows it across "
                                 "three cities and one long night to the people who never wanted it found."})
CATALOG["metas"].extend(extra)
META = load("meta_series.json")
META["meta"].setdefault("cast", ["Bryan Cranston", "Aaron Paul", "Anna Gunn", "Dean Norris", "Betsy Brandt"])
META["meta"]["app_extras"] = {"cast": [{"name": n, "character": c, "photo": f"{BASE}/tmdbimg/p{i}.png"} for i, (n, c) in enumerate([("Bryan Cranston", "Walter White"), ("Aaron Paul", "Jesse Pinkman"), ("Anna Gunn", "Skyler White")])]}
META["meta"]["poster"] = f"{BASE}/poster/1.png"
META["meta"]["background"] = f"{BASE}/bg/1.png"
META["meta"]["logo"] = None
for i, v in enumerate(META["meta"]["videos"]):
    v["thumbnail"] = f"{BASE}/thumb/{i}.png"
SRT = "1\n00:00:00,500 --> 00:00:09,000\nHello from the <i>fake</i> subtitle addon\n\n2\n00:00:09,500 --> 00:00:20,000\nSecond cue\n"
for _i, _t in enumerate((30.2, 47.9, 61.0, 83.4, 99.1, 120.7, 141.3)):   # later cues so timing alignment has something to vote on
    SRT += "\n%d\n00:%02d:%02d,%03d --> 00:%02d:%02d,000\nCue %d\n" % (_i + 3, _t // 60, _t % 60, round((_t % 1) * 1000), (_t + 2) // 60, (_t + 2) % 60, _i + 3)
STREAMS = load("streams.json")
MEDIA_DIR = os.path.join(FIX, "media")
for _s in STREAMS["streams"]:
    if _s.get("url", "").startswith("https://cdn.example.com"):
        _s["url"] = f"{BASE}/video/sample_h264_aac.mkv"   # a real header (with chapters) the pre-flight can read
PALETTE = [(123, 91, 245), (60, 140, 200), (200, 90, 90), (90, 170, 110), (220, 160, 60), (150, 80, 160)]
PAIRS = [((255, 94, 98), (255, 153, 102)), ((54, 209, 220), (91, 134, 229)), ((142, 45, 226), (74, 0, 224)),
         ((247, 151, 30), (255, 210, 0)), ((17, 153, 142), (56, 239, 125)), ((252, 70, 107), (63, 94, 251)),
         ((0, 180, 219), (0, 131, 176)), ((238, 9, 121), (255, 106, 0)), ((86, 204, 242), (47, 128, 237))]
try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    import io as _io
    def _font(size):
        for f in ("/usr/share/fonts/opentype/inter/InterDisplay-Bold.otf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
            try: return ImageFont.truetype(f, size)
            except Exception: pass
        return ImageFont.load_default()
    def art(kind, i, title=""):
        """Generated artwork so screenshots look like a real catalog."""
        c1, c2 = PAIRS[i % len(PAIRS)]
        w, h = {"poster": (300, 450), "bg": (1280, 720), "thumb": (512, 288)}[kind]
        small = Image.new("RGB", (2, 2))
        small.putdata([c1, tuple((a + b) // 2 for a, b in zip(c1, c2)), tuple((a + b) // 2 for a, b in zip(c1, c2)), c2])
        im = small.resize((w, h), Image.BILINEAR)
        d = ImageDraw.Draw(im, "RGBA")
        import random
        rnd = random.Random(i * 7 + len(kind))
        for _ in range(7):
            r = rnd.randint(w // 10, w // 3)
            cx, cy = rnd.randint(0, w), rnd.randint(0, h)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, rnd.randint(18, 50)))
        d.rectangle([0, int(h * 0.62), w, h], fill=(0, 0, 0, 70))
        # mountains silhouette for backdrops
        if kind != "poster":
            pts = [(0, h)]
            for k in range(0, w + 40, 40):
                pts.append((k, int(h * (0.62 + 0.12 * rnd.random()))))
            pts.append((w, h))
            d.polygon(pts, fill=(10, 8, 25, 170))
        im = im.filter(ImageFilter.GaussianBlur(1.2 if kind == "poster" else 2))
        if kind == "poster" and title:
            d = ImageDraw.Draw(im, "RGBA")
            f = _font(38)
            words, lines, cur = title.upper().split(), [], ""
            for wd in words:
                if d.textlength((cur + " " + wd).strip(), font=f) > w - 40: lines.append(cur); cur = wd
                else: cur = (cur + " " + wd).strip()
            lines.append(cur)
            y0 = h - 40 - 44 * len(lines)
            for ln in lines:
                d.text((22, y0), ln, font=f, fill=(255, 255, 255, 235)); y0 += 44
        buf = _io.BytesIO(); im.save(buf, "JPEG", quality=88); return buf.getvalue()
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False


_ART_CACHE = {}
if "art" in globals():
    _art_impl = art
    def art(kind, i, title=""):
        k = (kind, i, title)
        if k not in _ART_CACHE: _ART_CACHE[k] = _art_impl(kind, i, title)
        return _ART_CACHE[k]


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        p = self.path
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if p.endswith("/chat/completions"):   # a stand-in for any OpenAI-compatible model
            try:
                req = json.loads(body.decode("utf-8"))
            except Exception:
                return self.send(400, b'{"error":{"message":"bad json"}}')
            if "Bearer" not in (self.headers.get("Authorization") or "") and "SNTV_AI_NOAUTH" not in os.environ:
                pass   # keys are the user's business; the fake answers anyway
            system = " ".join(m.get("content", "") for m in req.get("messages", []) if m.get("role") == "system")
            user = " ".join(m.get("content", "") for m in req.get("messages", []) if m.get("role") == "user")
            if "translate subtitle lines" in system:
                lines = json.loads(user).get("lines", [])
                lang = system.split("into ")[1].split(" for")[0]
                answer = json.dumps({"lines": ["[%s] %s" % (lang[:2].lower(), l) for l in lines]})
            elif "recommend films" in system:
                answer = json.dumps({"titles": [{"name": "The Shawshank Redemption", "year": 1994}, {"name": "Breaking Bad", "year": 2008}, {"name": "No Such Title", "year": 2001}]})
            else:
                answer = json.dumps({"nutshell": "A quietly devastating prison drama about hope, patience and friendship that keeps rewarding rewatches.",
                                     "good_for": "A slow evening when you want something moving rather than loud.",
                                     "notes": "Violence and an assault are implied rather than shown; strong language throughout.",
                                     "similar": ["The Green Mile", "Cool Hand Luke", "Escape from Alcatraz"]})
            return self.send(200, json.dumps({"choices": [{"message": {"role": "assistant", "content": answer}}]}).encode())
        return self.send(404, b"{}")

    def do_GET(self):
        p = self.path
        if p == "/manifest.json":
            return self.send(200, json.dumps(MANIFEST).encode())
        if p.startswith("/catalog/"):
            if "search=" in p:
                q = p.split("search=")[1].split("&")[0].split(".json")[0].lower()
                metas = [m for m in CATALOG["metas"] if q.replace("%20", " ") in m.get("name", "").lower()]
                return self.send(200, json.dumps({"metas": metas}).encode())
            return self.send(200, json.dumps(CATALOG).encode())
        if p.startswith("/meta/"):
            mid = p.rsplit("/", 1)[1][:-5]
            if p.startswith("/meta/series/"):
                return self.send(200, json.dumps(META).encode())
            m = next((x for x in CATALOG["metas"] if x["id"] == mid), None)
            if not m:
                return self.send(404, b"{}")
            return self.send(200, json.dumps({"meta": dict(m, description="A fixture movie used by the smoke test.", runtime="101 min")}).encode())
        if p.startswith("/stream/"):
            return self.send(200, json.dumps(STREAMS).encode())
        if p.startswith("/introdb/media"):
            # TheIntroDB v4 shape: intro from the start to 60 s, credits at 50 min
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(p).query)
            body = {"type": "tv" if "season" in q else "movie",
                    "intro": [{"start_ms": None, "end_ms": 60000}],
                    "credits": [{"start_ms": 3000000, "end_ms": None}]}
            if "season" in q:
                body.update(season=int(q["season"][0]), episode=int(q["episode"][0]))
            return self.send(200, json.dumps(body).encode())
        if p.startswith("/link/create"):
            return self.send(200, json.dumps({"result": {"code": "QW7RT2", "link": "https://www.stremio.com/link?code=QW7RT2", "qrcode": ""}}).encode())
        if p.startswith("/link/read"):
            return self.send(200, json.dumps({"result": {}}).encode())
        if p.startswith("/media?"):   # the home helper: a page link as a playable file
            with open(os.path.join(MEDIA_DIR, "sample_h264_aac.mp4"), "rb") as f:
                return self.send(200, f.read(), "video/mp4")
        if p == "/repo.json":   # an update notice
            return self.send(200, json.dumps({"packages": [{"id": "org.stremio.nativetv", "manifest": {"version": "9.9.9"}}]}).encode())
        if p.startswith("/sub_matched.srt"):   # the file-matched subtitle: the same cues, 2 s later
            src = SRT
            import re as _re
            def shift(m):
                h, mi, s, ms = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
                t = h * 3600 + mi * 60 + s + ms / 1000 + 2.0
                return "%02d:%02d:%02d,%03d" % (t // 3600, (t % 3600) // 60, t % 60, round((t - int(t)) * 1000))
            return self.send(200, _re.sub(r"(\d\d):(\d\d):(\d\d),(\d\d\d)", shift, src).encode("latin-1"), "text/plain")
        if p.startswith("/subtitles/"):
            if "videoHash=" in p:
                return self.send(200, json.dumps({"subtitles": [{"id": "eng-matched", "url": f"{BASE}/sub_matched.srt", "lang": "eng"}]}).encode())
            subs = [{"id": "fake-eng", "url": f"{BASE}/sub.srt", "lang": "eng"}, {"id": "pob1", "url": f"{BASE}/sub.srt", "lang": "pob"},
                    {"id": "pob2", "url": f"{BASE}/sub.srt", "lang": "pob"}, {"id": "spa1", "url": f"{BASE}/sub.srt", "lang": "spa"},
                    {"id": "eng2", "url": f"{BASE}/sub.srt", "lang": "eng"}, {"id": "por1", "url": f"{BASE}/sub.srt", "lang": "por"}]
            return self.send(200, json.dumps({"subtitles": subs}).encode())
        if p.startswith("/video/"):
            name = os.path.basename(p)
            path = os.path.join(MEDIA_DIR, name)
            if not os.path.isfile(path):
                return self.send(404, b"")
            with open(path, "rb") as f:
                data = f.read()
            if len(data) < 262144:   # the real files are tiny: pad so the file hash (first + last 64 KB) applies
                data += b"\0" * (262144 - len(data))
            rng = self.headers.get("Range")
            if rng and rng.startswith("bytes="):
                a, _, b = rng[6:].partition("-")
                a = int(a or 0); b = int(b) if b else len(data) - 1
                b = min(b, len(data) - 1)
                self.send_response(206)
                self.send_header("Content-Type", "video/x-matroska")
                self.send_header("Content-Range", f"bytes {a}-{b}/{len(data)}")
                self.send_header("Content-Length", str(b - a + 1))
                self.end_headers()
                self.wfile.write(data[a:b + 1])
                return
            return self.send(200, data, "video/x-matroska")
        if p == "/sub.srt":
            return self.send(200, SRT.encode(), "text/plain")
        if p.startswith("/thumb/"):
            i = int(p.rsplit("/", 1)[1].split(".")[0])
            if HAVE_PIL:
                return self.send(200, art("thumb", i + 4), "image/jpeg")
            return self.send(200, png(320, 180, PALETTE[i % len(PALETTE)], pattern=True), "image/png")
        if p.startswith("/tmdb/3/find/"):     # TMDB stand-in: every IMDb id maps to id 1
            tv = "tt0903747" in p
            return self.send(200, json.dumps({"movie_results": [] if tv else [{"id": 1}], "tv_results": [{"id": 1}] if tv else []}).encode(), "application/json")
        if p.startswith("/tmdb/3/movie/") or p.startswith("/tmdb/3/tv/"):
            tv = "/tv/" in p
            cast = [{"name": n, "character": c, "profile_path": f"/p{i}.png"} for i, (n, c) in enumerate([("Bryan Cranston", "Walter White"), ("Aaron Paul", "Jesse Pinkman"), ("Anna Gunn", "Skyler White"), ("Dean Norris", "Hank Schrader")])]
            if tv:
                for cm in cast: cm["roles"] = [{"character": cm.pop("character")}]
                body = {"aggregate_credits": {"cast": cast}, "content_ratings": {"results": [{"iso_3166_1": "US", "rating": "TV-MA"}]}}
            else:
                body = {"credits": {"cast": cast}, "release_dates": {"results": [{"iso_3166_1": "US", "release_dates": [{"certification": "PG-13"}]}]}}
            return self.send(200, json.dumps(body).encode(), "application/json")
        if p.startswith("/tmdbimg/"):
            i = int(p.rsplit("/p", 1)[1].split(".")[0]) if "/p" in p else 0
            return self.send(200, art("poster", i + 2, ""), "image/png")
        if p.startswith("/ipapi"):
            return self.send(200, json.dumps({"status": "success", "city": "Makkah", "lat": 21.4225, "lon": 39.8262}).encode(), "application/json")
        if p.startswith("/thumbs/frame"):   # the seek preview server (tools/thumbs) stands in
            t = int(float(p.split("t=")[1].split("&")[0])) if "t=" in p else 0
            return self.send(200, art("bg", (t // 10) % 6, f"{t // 60}:{t % 60:02d}"), "image/png")
        if p.startswith("/poster/") or p.startswith("/bg/"):
            i = int(p.rsplit("/", 1)[1].split(".")[0])
            if HAVE_PIL and p.startswith("/poster/") and i % 4 == 2:   # metahub serves WebP for newer titles
                from PIL import Image as _I
                im = _I.open(_io.BytesIO(art("poster", i)))
                out = _io.BytesIO(); im.save(out, format="WEBP", quality=80)
                return self.send(200, out.getvalue(), "image/webp")
            kind = "poster" if p.startswith("/poster/") else "bg"
            if HAVE_PIL:
                title = CATALOG["metas"][i]["name"] if kind == "poster" and i < len(CATALOG["metas"]) else ""
                return self.send(200, art(kind, i, title), "image/jpeg")
            w, h = (300, 450) if kind == "poster" else (640, 360)
            return self.send(200, png(w, h, PALETTE[i % len(PALETTE)]), "image/png")
        self.send(404, b"{}")


if __name__ == "__main__":
    print(f"fake addon on {BASE}/manifest.json", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
