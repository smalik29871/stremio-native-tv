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


def png(w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
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
MANIFEST["resources"] = ["catalog", "meta", "stream"]
CATALOG = load("catalog.json")
for i, m in enumerate(CATALOG["metas"]):
    m["poster"] = f"{BASE}/poster/{i}.png"
    m["background"] = f"{BASE}/bg/{i}.png"
# pad the catalog so rows scroll
extra = []
for i in range(12):
    extra.append({"id": f"tt{1000+i}", "type": "movie", "name": f"Sample Movie {i+1}", "poster": f"{BASE}/poster/{i+3}.png",
                  "releaseInfo": str(1990 + i), "imdbRating": f"{6 + i % 4}.{i % 10}", "genres": ["Drama"]})
CATALOG["metas"].extend(extra)
META = load("meta_series.json")
META["meta"]["poster"] = f"{BASE}/poster/1.png"
META["meta"]["background"] = f"{BASE}/bg/1.png"
META["meta"]["logo"] = None
STREAMS = load("streams.json")
PALETTE = [(123, 91, 245), (60, 140, 200), (200, 90, 90), (90, 170, 110), (220, 160, 60), (150, 80, 160)]


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
        if p.startswith("/poster/") or p.startswith("/bg/"):
            i = int(p.rsplit("/", 1)[1].split(".")[0])
            w, h = (300, 450) if p.startswith("/poster/") else (640, 360)
            return self.send(200, png(w, h, PALETTE[i % len(PALETTE)]), "image/png")
        self.send(404, b"{}")


if __name__ == "__main__":
    print(f"fake addon on {BASE}/manifest.json", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
