#!/usr/bin/env python3
"""A minimal Stremio addon served from the test fixtures, for offline smoke tests.

    python3 tests/fake_addon.py 8765
Serves /manifest.json, /catalog/{type}/{id}.json (with optional /{extra}.json),
/meta/{type}/{id}.json, /stream/{type}/{id}.json and generated poster PNGs."""
import json, os, re, sys, io, struct, zlib, urllib.parse
from time import sleep
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
P2P_ASKED = {}   # /p2p/ stream requests per video
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
        # TorBox adding a torrent (a multipart form): hashes starting "cafe" are cached (id 77), others start downloading (78)
        if p.startswith("/torbox/torrents/createtorrent"):
            if self.headers.get("Authorization") != "Bearer tbkey" or b"multipart/form-data" not in self.headers.get("Content-Type", "").encode():
                return self.send(401, b'{"success":false,"error":"AUTH_ERROR"}')
            m = re.search(rb"btih:([0-9a-fA-F]{40})", body)
            if not m:
                return self.send(400, b'{"success":false,"detail":"no magnet"}')
            cached = m.group(1).lower().startswith(b"cafe")
            pack = m.group(1).lower().startswith(b"cafe5eed")   # a season pack (id 79)
            disc = m.group(1).lower().startswith(b"cafe0d15")   # a disc rip, files named title_t00... (id 80)
            miss = m.group(1).lower().startswith(b"cafebad0")   # a pack without episode 1 (id 81)
            if m.group(1).lower().startswith(b"f00d"):   # not cached: TorBox downloads it (id 82), done on the third look
                return self.send(200, json.dumps({"success": True, "data": {"torrent_id": 82, "hash": m.group(1).decode().lower()}}).encode())
            return self.send(200, json.dumps({"success": True, "data": {"torrent_id": 81 if miss else 80 if disc else 79 if pack else 77 if cached else 78, "hash": m.group(1).decode().lower()}}).encode())
        # IMDb's GraphQL stand-in: /imdbgql has no trailer for anything, /imdbgql/with has one in three qualities
        if p.startswith("/imdbgql"):
            if b"parentsGuide" in body:   # the parents guide: what a title contains, None to Severe
                cats = [("Sex & Nudity", "Mild"), ("Violence & Gore", "Severe"), ("Profanity", "Moderate"),
                        ("Alcohol, Drugs & Smoking", "Severe"), ("Frightening & Intense Scenes", "None")]
                return self.send(200, json.dumps({"data": {"title": {"parentsGuide": {"categories": [
                    {"category": {"text": c}, "severity": {"text": v}} for c, v in cats]}}}}).encode())
            if not p.startswith("/imdbgql/with"):
                return self.send(200, json.dumps({"data": {"title": {"latestTrailer": None}}}).encode())
            q = json.loads(body.decode("utf-8") or "{}")
            if "latestTrailer" not in q.get("query", "") or not str(q.get("variables", {}).get("id", "")).startswith("tt"):
                return self.send(400, b'{"errors":[{"message":"bad query"}]}')
            vids = [{"videoDefinition": "DEF_480p", "videoMimeType": "MP4", "url": f"{BASE}/video/sample_h264_aac.mp4?imdb=480"},
                    {"videoDefinition": "DEF_1080p", "videoMimeType": "MP4", "url": f"{BASE}/video/sample_h264_aac.mp4?imdb=1080"},
                    {"videoDefinition": "DEF_AUTO", "videoMimeType": "M3U8", "url": f"{BASE}/yt/media/master.m3u8?imdb=auto"}]
            return self.send(200, json.dumps({"data": {"title": {"latestTrailer": {"id": "vi123", "playbackURLs": vids}}}}).encode())
        # Trakt stand-in: a device code, accepted on the second poll; scrobbles are logged
        if p.startswith("/trakt/"):
            q = p[6:]
            if self.headers.get("trakt-api-key") != "cid":
                return self.send(403, b"{}")
            if q == "/oauth/device/code":
                return self.send(200, json.dumps({"device_code": "dev1", "user_code": "ABCD1234", "verification_url": "https://trakt.tv/activate", "expires_in": 600, "interval": 1}).encode())
            if q == "/oauth/device/token":
                H.trakt_polls = getattr(H, "trakt_polls", 0) + 1
                if H.trakt_polls < 2:
                    return self.send(400, b"{}")
                return self.send(200, json.dumps({"access_token": "tok1", "refresh_token": "ref1", "expires_in": 7776000}).encode())
            if q == "/oauth/token":   # a renewal: "refdown" while the service is down; else a token for a day
                if json.loads(body.decode() or "{}").get("refresh_token") == "refdown":
                    return self.send(503, b"{}")
                return self.send(200, json.dumps({"access_token": "tok1", "refresh_token": "ref1", "expires_in": 86400}).encode())
            if q.startswith("/sync/ratings"):   # your stars: logged like scrobbles
                if self.headers.get("Authorization") != "Bearer tok1":
                    return self.send(401, b"{}")
                with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                    f.write(b"trakt " + q.encode() + b" " + body + b"\n")
                return self.send(201, b"{}")
            if q.startswith("/scrobble/"):
                if self.headers.get("Authorization") != "Bearer tok1":
                    return self.send(401, b"{}")
                with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                    f.write(b"trakt " + q.encode() + b" " + body + b"\n")
                return self.send(201, b"{}")
        if p == "/os/download":
            fid = json.loads(body.decode())["file_id"]
            with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                f.write(f"os download {fid}\n".encode())
            return self.send(200, json.dumps({"link": f"{BASE}/sub_matched.srt?os={fid}", "remaining": 4}).encode())
        # SimKL AUTH V2: a form-encoded device code, pending on the first poll; "v1app" is an old app
        if p.startswith("/simkl/oauth2/"):
            if self.headers.get("Content-Type") != "application/x-www-form-urlencoded":
                return self.send(415, b'{"error":"invalid_request"}')
            form = dict(kv.split("=", 1) for kv in body.decode().split("&") if "=" in kv)
            if p.startswith("/simkl/oauth2/device"):
                if form.get("client_id") == "v1app":
                    return self.send(401, b'{"error":"invalid_client","error_description":"AUTH V1 app"}')
                if "media%3Awrite" not in form.get("scope", ""):
                    return self.send(400, b'{"error":"invalid_scope"}')
                return self.send(200, json.dumps({"device_code": "sdev", "user_code": "SV2CODE", "verification_uri": "https://simkl.com/activate",
                                                  "verification_uri_complete": "https://simkl.com/activate?user_code=SV2CODE", "expires_in": 900, "interval": 1}).encode())
            if p.startswith("/simkl/oauth2/token"):
                if form.get("grant_type") == "refresh_token":
                    return self.send(200, json.dumps({"access_token": "stok3", "refresh_token": "sref2", "expires_in": 604800}).encode())
                H.simkl_polls = getattr(H, "simkl_polls", 0) + 1
                if H.simkl_polls < 2:
                    return self.send(400, b'{"error":"authorization_pending"}')
                return self.send(200, json.dumps({"access_token": "stok2", "refresh_token": "sref", "expires_in": 604800, "token_type": "Bearer"}).encode())
        if p.startswith("/simkl/sync/ratings"):
            with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                f.write(b"simkl " + p[6:].encode() + b" " + body + b"\n")
            return self.send(201, b"{}")
        if p.startswith("/simkl/sync/history"):
            with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                f.write(b"simkl history " + body + b"\n")
            return self.send(201, b"{}")
        if p.endswith("/chat/completions"):   # a stand-in for any OpenAI-compatible model
            try:
                req = json.loads(body.decode("utf-8"))
            except Exception:
                return self.send(400, b'{"error":{"message":"bad json"}}')
            if "Bearer" not in (self.headers.get("Authorization") or "") and "SNTV_AI_NOAUTH" not in os.environ:
                pass   # keys are the user's business; the fake answers anyway
            if req.get("model") == "missing-model":
                return self.send(404, json.dumps({"error": {"message": "The model `missing-model` does not exist or you do not have access to it."}}).encode())
            system = " ".join(m.get("content", "") for m in req.get("messages", []) if m.get("role") == "system")
            user = " ".join(m.get("content", "") for m in req.get("messages", []) if m.get("role") == "user")
            if "translate subtitle lines" in system:
                lines = json.loads(user).get("lines", [])
                lang = system.split("into ")[1].split(" for")[0]
                answer = json.dumps({"lines": ["[%s] %s" % (lang[:2].lower(), l) for l in lines]})
            elif "which file in a torrent is a given TV episode" in system:   # a disc rip: the second title is the episode
                pick = next((int(l.split(".")[0]) for l in user.split("\n") if "title_t01" in l and "season 1, episode 1" in user), -1)
                answer = json.dumps({"file": pick})
            elif "recommend films" in system:
                # a request with a film industry in it gets filters too (TMDB lists titles for them)
                filters = {"type": "movie", "language": "hi", "country": "IN", "genres": ["Comedy"], "from": 1990, "to": 1999} if "bollywood" in user.lower() else None
                answer = json.dumps({"summary": "Hindi comedies from the 1990s" if filters else "Stories of hope behind bars",
                                     "titles": [{"name": "The Shawshank Redemption", "year": 1994, "type": "movie", "why": "Hope and friendship in prison"},
                                                {"name": "Breaking Bad", "year": 2008, "type": "series", "why": "A slow fall, brilliantly told"},
                                                {"name": "No Such Title", "year": 2001, "type": "movie"}], "filters": filters,
                                     "refine": ["Only series", "Newer ones", "Lighter in tone"]})
            else:
                answer = json.dumps({"nutshell": "A quietly devastating prison drama about hope, patience and friendship that keeps rewarding rewatches.",
                                     "good_for": "A slow evening when you want something moving rather than loud.",
                                     "notes": "Violence and an assault are implied rather than shown; strong language throughout.",
                                     "similar": ["The Green Mile", "Cool Hand Luke", "Escape from Alcatraz"]})
            return self.send(200, json.dumps({"choices": [{"message": {"role": "assistant", "content": answer}}]}).encode())
        # an account whose addons an old version saved with partial manifests (key "brokenkey"),
        # until a whole collection is saved back
        if p == "/api/login":   # any e-mail and password: the test account
            return self.send(200, json.dumps({"result": {"authKey": "testkey", "user": {"email": "sam.rivera@example.com"}}}).encode())
        if p == "/api/addonCollectionSet":
            req = json.loads(body.decode("utf-8") or "{}")
            need = ("id", "name", "version", "description", "resources", "types", "catalogs")
            if not all(all(k in a.get("manifest", {}) for k in need) for a in req.get("addons", [])):
                return self.send(200, b'{"error":{"message":"missing field","code":24}}')
            if b"brokenkey" in body:   # only that account's own save repairs it (others save their lists too)
                H.repaired = True
            with open(os.environ.get("FAKE_COLLECTION_LOG", os.devnull), "ab") as f:
                f.write(body + b"\n")
            return self.send(200, b'{"result":{"success":true}}')
        if p == "/api/addonCollectionGet" and b"brokenkey" in body and not getattr(H, "repaired", False):
            return self.send(200, json.dumps({"result": {"addons": [{"transportUrl": f"{BASE}/manifest.json", "manifest": {"id": MANIFEST["id"], "name": MANIFEST["name"], "version": MANIFEST["version"]}, "flags": {}},
                                                              {"transportUrl": "http://127.0.0.1:1/gone/manifest.json", "manifest": {"id": "org.gone", "name": "Gone", "version": "1.0.0"}, "flags": {}},
                                                              {"transportUrl": "https://opensubtitles.strem.io/stremio/v1", "manifest": {"id": "org.stremio.opensubtitles", "name": "OpenSubtitles", "version": "0.24.0",
                                                               "description": "", "types": [], "resources": [], "catalogs": []}, "flags": {"official": True, "protected": True}}]}}).encode())
        if p == "/api/addonCollectionGet" and b"blankkey" in body:   # repaired earlier: two addons left blank, one of them answers now
            note = "Did not answer when this account was repaired; reinstall it if it stays empty"
            blank = lambda url, i: {"transportUrl": url, "manifest": {"id": i, "name": i, "version": "0.0.0", "description": note, "types": [], "resources": [], "catalogs": []}, "flags": {}}
            return self.send(200, json.dumps({"result": {"addons": [{"transportUrl": f"{BASE}/manifest.json", "manifest": MANIFEST, "flags": {}},
                                                                    blank(f"{BASE}/tbsrc/manifest.json", "org.sntv.tbsrc"), blank("http://127.0.0.1:1/gone/manifest.json", "org.gone")]}}).encode())
        if p == "/api/addonCollectionGet":   # the account's addons: this fake one
            return self.send(200, json.dumps({"result": {"addons": [{"transportUrl": f"{BASE}/manifest.json", "manifest": MANIFEST, "flags": {}}]}}).encode())
        if p == "/api/datastoreGet":   # the account's library (FAKE_LIBRARY), as the Stremio API answers
            path = os.environ.get("FAKE_LIBRARY")
            return self.send(200, open(path, "rb").read() if path else b'{"result":[]}')
        if p == "/api/datastorePut":   # progress written to the account: kept in FAKE_PUT_LOG for the test to read
            if os.environ.get("FAKE_PUT_LOG"):
                with open(os.environ["FAKE_PUT_LOG"], "ab") as f:
                    f.write(body + b"\n")
            return self.send(200, b'{"result":{"success":true}}')
        if p.startswith("/yt/youtubei/v1/player"):   # YouTube's TV app asking for a video
            req = json.loads(body.decode("utf-8"))
            client = req.get("context", {}).get("client", {})
            known = {"VISIONOS": "101", "ANDROID": "3", "IOS": "5", "TVHTML5": "7", "WEB_EMBEDDED_PLAYER": "56"}
            if known.get(client.get("clientName")) != self.headers.get("X-YouTube-Client-Name"):
                return self.send(400, b'{"error":"unknown client"}')
            if self.headers.get("X-Goog-Visitor-Id") != "vis123" or client.get("visitorData") != "vis123":
                return self.send(400, b'{"error":"missing visitor id"}')
            if client["clientName"] == "TVHTML5" and req["playbackContext"]["contentPlaybackContext"].get("signatureTimestamp") != 20250:
                return self.send(400, b'{"error":"the TV client needs the signature timestamp"}')
            if client["clientName"] in ("VISIONOS", "ANDROID", "IOS") and req["videoId"].startswith("tv"):   # the bot check: the app falls back to the TV client
                return self.send(200, json.dumps({"playabilityStatus": {"status": "LOGIN_REQUIRED", "reason": "Sign in to confirm you\u2019re not a bot"}}).encode())
            media = os.path.getsize(os.path.join(MEDIA_DIR, "sample_h264_aac.mp4"))
            vfrag = os.path.getsize(os.path.join(MEDIA_DIR, "yt_video_frag.mp4"))   # fragmented, with a sidx, as YouTube's are
            afrag = os.path.getsize(os.path.join(MEDIA_DIR, "yt_audio_frag.m4a"))
            if "key=" in self.path and "key=AIzaFake" not in self.path:
                return self.send(400, b'{"error":"bad API key"}')
            if req["videoId"].startswith("fileOnly"):   # no playlist: a muxed file, fetched in &range= pieces
                media = os.path.getsize(os.path.join(MEDIA_DIR, "sample_h264_aac.mp4"))
                return self.send(200, json.dumps({"playabilityStatus": {"status": "OK"}, "videoDetails": {"videoId": req["videoId"], "lengthSeconds": "185"},
                    "streamingData": {"formats": [{"itag": 18, "height": 360, "mimeType": "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"", "url": f"{BASE}/yt/media/video.mp4?id=2&clen={media}"}]}}).encode())
            if client["clientName"] == "IOS":   # the iPhone app also offers an HLS playlist
                return self.send(200, json.dumps({"playabilityStatus": {"status": "OK"}, "videoDetails": {"videoId": req["videoId"], "lengthSeconds": "185"},
                                                  "streamingData": {"hlsManifestUrl": f"{BASE}/yt/media/master.m3u8?id={req['videoId']}&c=IOS"}}).encode())
            # "pot...": the visionOS links want a proof-of-origin token past their first bytes; the TV app's do not
            pot = req["videoId"].startswith("pot")
            vc = "TVHTML5" if client["clientName"] == "TVHTML5" else "VISIONOS"
            need = "&pot=need" if pot and vc == "VISIONOS" else ""
            return self.send(200, json.dumps({
                "playabilityStatus": {"status": "OK"},
                "videoDetails": {"videoId": req["videoId"], "title": "Adhan (fixture)", "lengthSeconds": "185"},
                "streamingData": {
                    "formats": [{"itag": 18, "mimeType": "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"", "url": f"{BASE}/yt/media/video.mp4?id=1&n=abc123"}],
                    "adaptiveFormats": [
                        {"itag": 137, "mimeType": "video/mp4; codecs=\"avc1.640028\"", "bitrate": 4400000, "width": 1920, "height": 1080, "fps": 30,
                         "initRange": {"start": "0", "end": "802"}, "indexRange": {"start": "803", "end": "962"}, "contentLength": str(vfrag), "url": f"{BASE}/yt/media/video.mp4?id=v1080&c={vc}&clen={vfrag}{need}"},
                        {"itag": 136, "mimeType": "video/mp4; codecs=\"avc1.4d401f\"", "bitrate": 2300000, "width": 1280, "height": 720, "fps": 30,
                         "initRange": {"start": "0", "end": "802"}, "indexRange": {"start": "803", "end": "962"}, "contentLength": str(vfrag), "url": f"{BASE}/yt/media/video.mp4?id=v720&c={vc}&clen={vfrag}{need}"},
                        {"itag": 251, "mimeType": "audio/webm; codecs=\"opus\"", "bitrate": 150000, "url": f"{BASE}/yt/media/opus.webm?n=abc123"},
                        {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 130000, "audioSampleRate": "44100", "contentLength": str(afrag),
                         "initRange": {"start": "0", "end": "732"}, "indexRange": {"start": "733", "end": "784"}, "url": f"{BASE}/yt/media/audio.m4a?n=abc123&clen={afrag}{need}"},
                        {"itag": 140, "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"", "bitrate": 135000, "isDrc": True, "url": f"{BASE}/yt/media/drc.m4a?n=abc123"}]}}).encode())
        return self.send(404, b"{}")

    def do_GET(self):
        p = self.path
        # /notrailer/...: the same addon with no trailers in its metas (TMDB then has one)
        bestfit = p.startswith("/bestfit/")
        if bestfit:
            p = p[8:]
        # /latesubs/...: the same addon, its first English file 12 s late (a choice made meanwhile wins; a slow test machine needs the time to make it)
        latesubs = p.startswith("/latesubs/")
        if latesubs:
            p = p[9:]
        # /signs/...: the same addon, its English file a credit over the dialogue and a line marked for the top
        signs = p.startswith("/signs/")
        if signs:
            p = p[6:]
        # /pot/...: its trailer's separate video and audio want a token from the visionOS client
        pot = p.startswith("/pot/")
        if pot:
            p = p[4:]
        noisy = p.startswith("/noisy/")
        if noisy:
            p = p[6:]
        slowsearch = p.startswith("/slowsearch/")   # an addon whose search answers after 2 s
        if slowsearch:
            p = p[11:]
        notrailer = p.startswith("/notrailer/")
        if notrailer:
            p = p[10:]
        longseason = p.startswith("/longseason/")   # one season of 26 episodes: its range chips
        if longseason:
            p = p[11:]
        scope = p.startswith("/scope/")   # its sources are scope films framed in 16:9 (black bars in the picture): the living letterbox
        if scope:
            p = p[6:]
        # an iTunes store stand-in: two soundtrack albums of Breaking Bad among names that only look alike
        if p.startswith("/itunes/search"):
            term = urllib.parse.parse_qs(urllib.parse.urlparse(p).query).get("term", [""])[0].lower()
            albums = []
            if term.startswith("breaking bad"):
                def alb(cid, name, artist, year, n):
                    return {"wrapperType": "collection", "collectionId": cid, "collectionName": name, "artistName": artist, "trackCount": n,
                            "releaseDate": f"{year}-06-01T07:00:00Z", "artworkUrl100": f"{BASE}/poster/{cid % 7}.png?size=100x100bb",
                            "collectionViewUrl": f"https://music.apple.com/us/album/{cid}?uo=4"}
                albums = [alb(901, "Breaking Benjamin: Greatest Hits", "Breaking Benjamin", 2011, 14),
                          alb(902, "Breaking Bad (Original Score from the Television Series)", "Dave Porter", 2012, 18),
                          alb(903, "Breaking Bad Habits", "The Habits", 2015, 10),
                          alb(904, "Breaking Bad (Original Motion Picture Soundtrack)", "Various Artists", 1979, 9),
                          alb(905, "Breaking Bad: Music from the Original TV Series", "Various Artists", 2010, 12)]
            return self.send(200, json.dumps({"resultCount": len(albums), "results": albums}).encode())
        if p.startswith("/itunes/lookup"):
            cid = int(urllib.parse.parse_qs(urllib.parse.urlparse(p).query).get("id", ["0"])[0])
            songs = ["Breaking Bad Main Title Theme", "Waiting for Walt", "Crawl Space", "Jesse's Song", "Fly", "Rest in Peace",
                     "The Cousins", "Salud", "Ozymandias", "Felina", "Baby Blue", "Negro y Azul", "Out of Time Man", "DLZ",
                     "Crystal Blue Persuasion", "Quimica Mayor", "Catch Yer Own Train", "Sunlight's Gone"]
            n = 18 if cid == 902 else 12
            res = [{"wrapperType": "collection", "collectionId": cid}] + [
                {"wrapperType": "track", "trackName": songs[i], "trackNumber": i + 1, "discNumber": 1, "trackTimeMillis": 60000 + i * 7300} for i in range(n)]
            return self.send(200, json.dumps({"resultCount": len(res), "results": res}).encode())
        # /dropvid/...: one source, a bigger file whose server cuts the first download partway
        # (/stallvid/...: or stops sending there and holds the connection open)
        if p.startswith("/dropvid/") or p.startswith("/stallvid/"):
            how = "stall" if p.startswith("/stallvid/") else "drop"
            p = p[len(how) + 4:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv." + how + "vid", name=how.title() + " Video")).encode())
            if p.startswith("/stream/"):
                return self.send(200, json.dumps({"streams": [{"name": "Drop\n1080p", "title": "Movie.2024.1080p.WEB.h264\n\U0001F4BE 2.0 GB",
                                                               "url": STREAMS["streams"][0]["url"] + "?" + how + "=100000"}]}).encode())
            # its catalogs and metas are the plain addon's
        # /dievid/...: a source whose server dies partway (the rest refused), then a mirror of
        # the very file under another name; /dieother/...: then the same size, other bytes
        if p.startswith("/dievid/") or p.startswith("/dieother/"):
            other = p.startswith("/dieother/")
            p = p[9 if other else 7:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv." + ("dieother" if other else "dievid"), name="Die Video")).encode())
            if p.startswith("/stream/"):
                url = STREAMS["streams"][0]["url"]
                hints = {"filename": "Movie.2024.1080p.WEB.h264.mkv", "videoSize": 1048576}
                return self.send(200, json.dumps({"streams": [
                    {"name": "Die\n1080p", "title": "Movie.2024.1080p.WEB.h264\n\U0001F4BE 1.0 MB", "url": url + "?die=100000", "behaviorHints": hints},
                    {"name": "Mirror\n720p", "title": "Movie.2024.720p.WEB.h264\n\U0001F4BE 1.0 MB", "url": url + ("?other=1" if other else "?mirror=1"), "behaviorHints": dict(hints, filename="Movie.2024.1080p.WEB.h264-MIRROR.mkv")}]}).encode())
        # /rich/...: a stream that needs a Referer, knows its file's hash and brings its own subtitles
        if p.startswith("/rich/"):
            p = p[5:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.rich", name="Rich Addon", resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                return self.send(200, json.dumps({"streams": [{"name": "Rich\n4K", "title": "Movie.2024.2160p.WEB.h265\n\U0001F4BE 6.0 GB", "url": f"{BASE}/needref/movie.mkv",
                    "behaviorHints": {"proxyHeaders": {"request": {"Referer": "https://rich.example/"}}, "videoHash": "0123456789abcdef", "videoSize": 6442450944},
                    "subtitles": [{"id": "r1", "url": f"{BASE}/richsub/fr.srt", "lang": "fra"}]}]}).encode())
            return self.send(404, b"{}")
        if p.startswith("/needref/"):   # the addon asks for a Referer; this host answers either way
            return self.send(206, b"\x1a\x45\xdf\xa3" + b"\0" * 60, "video/x-matroska")
        if p.startswith("/richsub/"):
            return self.send(200, SRT.encode(), "text/plain")
        # /errpage/...: its best-looking source answers 200 with an HTML "link expired" page
        if p.startswith("/errpage/"):
            p = p[8:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.errpage", name="Error Page", resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                url = STREAMS["streams"][0]["url"]
                return self.send(200, json.dumps({"streams": [
                    {"name": "ErrPage\n2160p", "title": "Movie.2024.2160p.WEB.h265\n\U0001F4BE 9.0 GB", "url": f"{BASE}/expiredpage/movie.mkv"},
                    {"name": "ErrPage\n1080p", "title": "Movie.2024.1080p.WEB.h264\n\U0001F4BE 2.0 GB", "url": url + "?real=1"}]}).encode())
            return self.send(404, b"{}")
        if p.startswith("/expiredpage/"):
            return self.send(200, b"<html><body><h1>This link has expired</h1></body></html>", "text/html")
        # /wrongep/...: its best-looking source for an episode is another episode (a 4K "S01E04" for E1)
        if p.startswith("/wrongep/"):
            p = p[8:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.wrongep", name="Wrong Episode", resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/series/"):
                url = STREAMS["streams"][0]["url"]
                return self.send(200, json.dumps({"streams": [
                    {"name": "Wrong\n2160p", "title": "Breaking.Bad.S01E04.2160p.WEB.h265-GRP\n\U0001F4BE 4.0 GB", "url": url + "?ep=4"},
                    {"name": "Wrong\n1080p", "title": "Breaking.Bad.S01E01.1080p.WEB.h264-GRP\n\U0001F4BE 1.2 GB", "url": url + "?ep=1"}]}).encode())
            return self.send(404, b"{}")
        # /chain/...: episode sources with no bingeGroup, only release names; episode 1 has
        # only NTb, the others list another group first (Up Next should stay on NTb); NTb's
        # files are named as in a season pack, the group only in the torrent's name
        chain = p.startswith("/chain/")
        if chain:
            p = p[6:]
            if p.startswith("/stream/series/"):
                from urllib.parse import unquote as _unq
                vid = _unq(p.split("/stream/series/")[1].split(".json")[0])
                ep = vid.rsplit(":", 1)[-1]
                url = STREAMS["streams"][0]["url"]
                ntb = {"name": "Example\n1080p", "title": f"Breaking.Bad.S01E{int(ep):02d}.1080p.WEB.h264-NTb\n\U0001F4BE 1.1 GB", "url": url + "?rel=ntb", "behaviorHints": {"filename": f"Breaking Bad - S01E{int(ep):02d}.mkv"}}
                other = {"name": "Example\n2160p", "title": f"Breaking.Bad.S01E{int(ep):02d}.2160p.WEB.h265-OTHER\n\U0001F4BE 4.2 GB", "url": url + "?rel=other"}
                return self.send(200, json.dumps({"streams": [ntb] if ep == "1" else [other, ntb]}).encode())
        # /slow/...: a second, stream-only addon that answers after FAKE_SLOW_S seconds for
        # ids containing FAKE_SLOW_ID (the next episode, so the loader has something to show)
        # /p2p/...: an addon like Torrentio next to TorBox. The first answer for a video has
        # only a torrent (FAKE_TORRENT_HASH, seeded on this machine) and an uncached TorBox
        # entry; later answers add a cached TorBox copy, as when TorBox catches up.
        # /p2ponly/...: the same addon while TorBox never catches up.
        if p.startswith("/os/subtitles"):   # OpenSubtitles.com: one popular file, one made for this file (its hash)
            if self.headers.get("Api-Key") != "oskey":
                return self.send(401, b"{}")
            hashed = "moviehash=" in p
            data = [{"id": "1", "attributes": {"language": "en", "release": "Some.Other.Cut.720p.HDTV-XYZ", "download_count": 900, "moviehash_match": False, "files": [{"file_id": 111}]}},
                    {"id": "2", "attributes": {"language": "en", "release": "Movie.2024.1080p.WEB-DL", "download_count": 12, "moviehash_match": hashed, "files": [{"file_id": 222}]}}]
            return self.send(200, json.dumps({"data": data}).encode())
        if p.startswith("/trakt/") and "/comments/likes" in p:   # what people said, most liked first: one spoiler, one too short
            return self.send(200, json.dumps([
                {"comment": "The tension in the last ten minutes is unbearable, in the best way. Cranston is incredible here.", "spoiler": False, "likes": 412, "user": {"username": "heisenfan"}},
                {"comment": "He dies at the end!", "spoiler": True, "likes": 90, "user": {"username": "spoiler"}},
                {"comment": "Wow.", "spoiler": False, "likes": 50, "user": {"username": "short"}},
                {"comment": "Quiet episode, but every scene pays off later. Worth a rewatch.", "spoiler": False, "likes": 88, "user": {"username": "slowburn"}}]).encode())
        if p.startswith("/trakt/") and p.endswith("/stats"):   # a title's numbers on Trakt
            return self.send(200, json.dumps({"watchers": 23456, "plays": 1400000, "collectors": 900, "comments": 12, "lists": 40, "votes": 800}).encode())
        if p.startswith("/trakt/") and p.endswith("/watching"):   # the people watching it right now
            return self.send(200, json.dumps([{"username": f"u{i}"} for i in range(37)]).encode())
        if p.startswith("/trakt/sync/playback"):   # paused on another device: a film past half, an episode at 40%
            if self.headers.get("Authorization") != "Bearer tokresume":   # only the resume test's account has any
                return self.send(200, b"[]")
            import datetime as _dt
            now = _dt.datetime.now(_dt.timezone.utc)
            iso = lambda d: (now - d).strftime("%Y-%m-%dT%H:%M:%S.000Z")
            return self.send(200, json.dumps([
                {"type": "movie", "progress": 55.0, "paused_at": iso(_dt.timedelta(minutes=5)), "movie": {"title": "The Shawshank Redemption", "year": 1994, "runtime": 142, "ids": {"imdb": "tt0111161"}}},
                {"type": "episode", "progress": 40.0, "paused_at": iso(_dt.timedelta(hours=1)), "episode": {"season": 1, "number": 3, "runtime": 47, "ids": {}},
                 "show": {"title": "Breaking Bad", "year": 2008, "runtime": 47, "ids": {"imdb": "tt0903747"}}}]).encode())
        if p.startswith("/trakt/sync/watchlist"):
            return self.send(200, json.dumps([{"type": "movie", "movie": {"title": "The Shawshank Redemption", "year": 1994, "ids": {"imdb": "tt0111161"}}},
                                              {"type": "show", "show": {"title": "Breaking Bad", "year": 2008, "ids": {"imdb": "tt0903747"}}}]).encode())
        if p.startswith("/trakt/recommendations/"):
            shows = "/shows" in p
            return self.send(200, json.dumps([{"title": "Breaking Bad" if shows else "Fight Club", "year": 2008 if shows else 1999,
                                               "ids": {"imdb": "tt0903747" if shows else "tt0137523"}}]).encode())
        if p.startswith("/simkl/") and "app-name=" not in p:
            return self.send(400, b'{"error":"app-name and app-version are required"}')
        if p.startswith("/simkl/oauth/pin") and "client_id=bad" in p:
            return self.send(401, b'{"error":"client_id_failed","message":"Client ID is not valid"}')
        if p.startswith("/simkl/oauth/pin/"):
            return self.send(200, json.dumps({"result": "OK", "access_token": "stok"}).encode())
        if p.startswith("/simkl/oauth/pin"):
            return self.send(200, json.dumps({"result": "OK", "user_code": "SIM12", "verification_url": "https://simkl.com/pin", "expires_in": 900, "interval": 1}).encode())
        if p.startswith("/mdblist/"):
            return self.send(200, json.dumps({"title": "x", "ratings": [{"source": "imdb", "value": 9.3}, {"source": "tomatoes", "value": 91}, {"source": "tomatoesaudience", "value": 96}, {"source": "trakt", "value": 85}, {"source": "rogerebert", "value": 2.5},
                                                                       {"source": "metacritic", "value": 82}, {"source": "letterboxd", "value": 4.5}, {"source": "tmdb", "value": 84}], "score": 80}).encode())
        if p.startswith("/p2p/") or p.startswith("/p2ponly/"):
            only = p.startswith("/p2ponly/")
            p = p[8:] if only else p[4:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.p2p", name="Torrent Addon")).encode())
            if p.startswith("/stream/"):
                key = ("only:" if only else "") + p.split("?")[0]
                P2P_ASKED[key] = P2P_ASKED.get(key, 0) + 1
                streams = [{"name": "Torrent Addon\n1080p", "title": "Show.S02E03.1080p.WEB.x264.mkv\n\U0001F464 25 \U0001F4BE 155 KB",
                            "infoHash": os.environ.get("FAKE_TORRENT_HASH", "0" * 40).upper(), "fileIdx": 0,
                            "sources": ["tracker:udp://127.0.0.1:1/announce", "dht:" + os.environ.get("FAKE_TORRENT_HASH", "")]},
                           {"name": "[TB download] Torrent Addon\n1080p", "title": "Show.S02E03.1080p.WEB.x264.mkv\n\u23F3 needs a download",
                            "url": f"{BASE}/video/never_cached.mkv"}]
                if P2P_ASKED[key] > 1 and not only:
                    streams.insert(0, {"name": "[TB+] Torrent Addon\n1080p", "title": "Show.S02E03.1080p.WEB.x264.mkv\n\u26A1 cached",
                                       "url": f"{BASE}/video/sample_h264_aac.mkv"})
                return self.send(200, json.dumps({"streams": streams}).encode())
        disc = p.startswith("/tbdisc/")   # the same, its series torrent a disc rip
        miss = p.startswith("/tbmiss/")   # the same, its series torrent a pack without the episode
        if disc:
            p = "/tbsrc/" + p[8:]
        if miss:
            p = "/tbsrc/" + p[8:]
        if p.startswith("/tbget/"):   # one torrent, cached nowhere: TorBox can get it
            p = p[6:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.tbget", name="Torrents", resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                return self.send(200, json.dumps({"streams": [{"name": "Torrents\n1080p", "title": "Movie.2024.1080p.WEB.x264.mkv\n\U0001F464 1", "infoHash": "F00D" * 10, "fileIdx": 1}]}).encode())
        if p.startswith("/tbsrc/"):   # torrents only, no links: one TorBox has cached, one it does not
            p = p[6:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.tbsrc", name="Torrents", resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/series/"):   # a whole season, no file named
                return self.send(200, json.dumps({"streams": [
                    {"name": "Torrents\n1080p", "title": "Breaking Bad S01 1080p WEB x264-PACK", "infoHash": ("CAFEBAD0" if miss else "CAFE0D15" if disc else "CAFE5EED") * 5}]
                    + ([{"name": "Torrents\n720p", "title": "Breaking Bad S01 720p WEB x264-PACK", "infoHash": "CAFE5EED" * 5}] if disc else [])}).encode())
            if p.startswith("/stream/"):
                return self.send(200, json.dumps({"streams": [
                    {"name": "Torrents\n1080p", "title": "Movie.2024.1080p.WEB.x264.mkv", "infoHash": "CAFE" * 10, "fileIdx": 1},
                    {"name": "Torrents\n2160p", "title": "Movie.2024.2160p.WEB.x265.mkv", "infoHash": "BEEF" * 10, "fileIdx": 0}]}).encode())
        if p.startswith("/lighter/"):   # 2160p and 480p copies of every video, for the "buffering keeps stopping" offer
            p = p[8:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.lighter", name="Lighter Addon",
                                                      resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                first = STREAMS["streams"][0]
                return self.send(200, json.dumps({"streams": [{"name": "Lighter\n2160p", "title": "Movie.2024.2160p.WEB-DL", "url": first["url"] + "?q=2160"},
                                                              {"name": "Lighter\n480p", "title": "Movie.2024.480p.WEB-DL", "url": first["url"] + "?q=480"}]}).encode())
            return self.send(404, b"{}")
        if p.startswith("/many/"):   # a debrid-sized answer: more playable copies than the player is handed
            p = p[5:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.many", name="Many Addon",
                                                      resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                first = STREAMS["streams"][0]
                return self.send(200, json.dumps({"streams": [{"name": f"Many\n{r}p", "title": f"Movie.2024.{r}p.WEB-DL.Copy{i}", "url": first["url"] + f"?many={i}"}
                                                              for i, r in enumerate([1080, 1080, 720, 720, 1080, 480, 720, 1080, 480, 720])]}).encode())
            return self.send(404, b"{}")
        # /ep2wrong/...: the whole addon, but episode 2's only sources are named for another episode
        if p.startswith("/ep2wrong/"):
            rest = p[10:]
            if rest == "manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.ep2wrong", name="Ep2 Wrong")).encode())
            if rest.startswith("stream/") and (":1:2" in rest or "%3A1%3A2" in rest):
                return self.send(200, json.dumps({"streams": [{"name": "Wrong\n1080p", "title": "Breaking.Bad.S01E04.1080p.WEB.x264-WRONG",
                    "url": f"{BASE}/video/sample_h264_aac.mkv?ep=4", "behaviorHints": {"filename": "Breaking.Bad.S01E04.1080p.WEB.x264-WRONG.mkv"}}]}).encode())
            p = "/" + rest
        # /hdr/...: the addon with only two sources, both asking for a Referer: one refuses anyone without it
        # (/hdrvideo/: 403 without it, the video with it, ranges and all); /hdrbad/ refuses even then
        if p.startswith("/hdr/"):
            rest = p[5:]
            if rest == "manifest.json":   # the whole addon (catalogs, metas), its sources only these
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.hdr", name="Header Addon")).encode())
            if rest.startswith("stream/"):
                return self.send(200, json.dumps({"streams": [{"name": "Hdr\n1080p", "title": "Breaking.Bad.1080p.WEB.x264-HDR",
                    "url": f"{BASE}/hdrvideo/sample_h264_aac.mkv", "behaviorHints": {"proxyHeaders": {"request": {"Referer": "https://hdr.example/"}}}},
                    {"name": "Hdr\n720p", "title": "Breaking.Bad.720p.WEB.x264-HDRBAD",
                    "url": f"{BASE}/hdrbad/sample_h264_aac.mkv", "behaviorHints": {"proxyHeaders": {"request": {"Referer": "https://hdr.example/"}}}}]}).encode())
            p = "/" + rest   # the rest as the plain addon answers it
        if p.startswith("/hdrbad/"):
            return self.send(403, b"Forbidden", "text/plain")
        if p.startswith("/hdrvideo/"):
            if self.headers.get("Referer") != "https://hdr.example/":
                return self.send(403, b"Forbidden: no Referer", "text/plain")
            p = "/video/" + p[10:]
        # /fast/...: a stream-only addon with one cached debrid copy, reached through a redirect
        # (as a debrid link resolves to its file); /redir/<path> sends 302 to /<path>
        if p.startswith("/fast/"):
            rest = p[6:]
            if rest == "manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.fast", name="Fast Addon", resources=["stream"], catalogs=[])).encode())
            if rest.startswith("stream/"):
                return self.send(200, json.dumps({"streams": [{"name": "[RD+] Fast\n1080p", "title": "Breaking.Bad.1080p.WEB.x264-FAST",
                                                               "url": f"{BASE}/redir/video/sample_h264_aac.mkv"}]}).encode())
            return self.send(404, b"{}")
        if p.startswith("/redir/"):
            self.send_response(302)
            self.send_header("Location", f"{BASE}/{p[7:]}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        # /caps/<dts|dvtruehd>/...: a stream-only addon whose one source needs those features of the TV
        if p.startswith("/caps/"):
            kind, rest = p[6:].split("/", 1)
            if rest == "manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.caps." + kind, name="Caps Addon",
                                                      resources=["stream"], catalogs=[])).encode())
            if rest.startswith("stream/"):
                tags = {"dts": "1080p.WEB.DTS.x264", "dvtruehd": "1080p.WEB.DV.TrueHD.x264"}[kind]
                return self.send(200, json.dumps({"streams": [{"name": "Caps\n1080p", "title": f"Breaking.Bad.S01E01.{tags}-CAPS",
                                                               "url": f"{BASE}/video/sample_h264_aac.mkv?caps={kind}"}]}).encode())
            return self.send(404, b"{}")
        if p.startswith("/slow/"):
            p = p[5:]
            if p == "/manifest.json":
                return self.send(200, json.dumps(dict(MANIFEST, id="org.sntv.slow", name="Slow Addon",
                                                      resources=["stream"], catalogs=[])).encode())
            if p.startswith("/stream/"):
                import time
                from urllib.parse import unquote
                if os.environ.get("FAKE_SLOW_ID", ":1:2") in unquote(p):
                    time.sleep(float(os.environ.get("FAKE_SLOW_S", "6")))
                return self.send(200, json.dumps(STREAMS).encode())
            return self.send(404, b"{}")
        if p == "/manifest.json":
            return self.send(200, json.dumps(MANIFEST).encode())
        if p.startswith("/catalog/"):
            if "search=" in p and noisy:   # an addon whose search answers with its whole catalog
                return self.send(200, json.dumps(CATALOG).encode())
            if "search=" in p:
                if slowsearch:
                    sleep(2)
                q = p.split("search=")[1].split("&")[0].split(".json")[0].lower()
                metas = [m for m in CATALOG["metas"] if q.replace("%20", " ") in m.get("name", "").lower()]
                return self.send(200, json.dumps({"metas": metas}).encode())
            return self.send(200, json.dumps(CATALOG).encode())
        if p.startswith("/meta/"):
            mid = p.rsplit("/", 1)[1][:-5]
            if p.startswith("/meta/series/") and longseason:
                v0 = META["meta"]["videos"][0]
                vids = [dict(v0, id=f"{mid}:1:{e}", season=1, episode=e, title=f"Chapter {e}", thumbnail=f"{BASE}/thumb/{e % 8}.png") for e in range(1, 27)]
                return self.send(200, json.dumps({"meta": dict(META["meta"], videos=vids)}).encode())
            if p.startswith("/meta/series/"):
                return self.send(200, json.dumps(META).encode())
            m = next((x for x in CATALOG["metas"] if x["id"] == mid), None)
            if not m:
                return self.send(404, b"{}")
            return self.send(200, json.dumps({"meta": dict(m, description="A fixture movie used by the smoke test.", runtime="101 min",
                director=["Frank Darabont"], writer=["Stephen King", "Frank Darabont"], country="United States",
                awards="Nominated for 7 Oscars. 21 wins & 43 nominations total", released="1994-10-14T00:00:00.000Z",
                trailerStreams=[] if notrailer else [{"title": "Trailer", "ytId": "potPOT00001" if pot else "dQw4w9WgXcQ"}],
                app_extras={"cast": [{"name": n, "character": c, "photo": f"{BASE}/tmdbimg/p{i}.png"} for i, (n, c) in enumerate(
                    [("Tim Robbins", "Andy Dufresne"), ("Morgan Freeman", "Ellis Boyd 'Red' Redding"), ("Bob Gunton", "Warden Norton"), ("William Sadler", "Heywood")])]})}).encode())
        if p.startswith("/stream/"):
            if scope:
                return self.send(200, json.dumps({"streams": [dict(st, url=st["url"] + ("&" if "?" in st["url"] else "?") + "scope=1") if st.get("url") else st for st in STREAMS["streams"]]}).encode())
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
        if p.endswith("/ai/v1/models"):
            return self.send(200, json.dumps({"data": [{"id": "whisper-large-v3"}, {"id": "llama-3.1-8b-instant"}, {"id": "openai/gpt-oss-120b"}, {"id": "llama-guard-4-12b"}]}).encode())
        if p.startswith("/media?"):   # the home helper: a page link as a playable file
            with open(os.path.join(MEDIA_DIR, "sample_h264_aac.mp4"), "rb") as f:
                return self.send(200, f.read(), "video/mp4")
        if p.startswith("/yt/watch?v="):   # the watch page: a visitor id and the player version
            if "SOCS=CAI" not in (self.headers.get("Cookie") or ""):
                return self.send(302, b"")   # the consent page, in real life
            cfg = {"INNERTUBE_CONTEXT": {"client": {"clientName": "WEB", "visitorData": "vis123"}},
                   "VISITOR_DATA": "vis123", "INNERTUBE_API_KEY": "AIzaFake", "PLAYER_JS_URL": "/s/player/abcd1234/player_ias.vflset/en_US/base.js"}
            return self.send(200, ("<html><script>ytcfg.set(%s);</script></html>" % json.dumps(cfg)).encode(), "text/html")
        if p == "/yt/s/player/abcd1234/player_ias.vflset/en_US/base.js":
            return self.send(200, b"var yt={signatureTimestamp:20250};", "text/javascript")
        if p.startswith("/yt/media/"):   # googlevideo: only the AAC (or muxed MP4) the app should pick, in ranges
            name = p.split("?")[0].rsplit("/", 1)[1]
            # links are bound to the app they were issued to, as googlevideo's are
            ua = self.headers.get("User-Agent") or ""
            if ("c=IOS" in p and "ios.youtube" not in ua) or ("c=VISIONOS" in p and "Macintosh" not in ua) or ("c=TVHTML5" in p and "SMART-TV" not in ua):
                return self.send(403, b"")
            if "pot=need" in p:   # the first bytes come, the rest is refused without a token
                rq = p.split("&range=")[1].split("&")[0] if "&range=" in p else (self.headers.get("Range") or "bytes=0-")[6:]
                if int(rq.split("-")[0] or 0) > 4096:
                    return self.send(403, b"")
            if name in ("mid.m3u8", "high.m3u8"):
                return self.send(200, b"#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts?c=IOS\n#EXT-X-ENDLIST\n", "application/vnd.apple.mpegurl")
            if name == "seg1.ts":
                return self.send(200, b"\x47" * 188 * 4, "video/mp2t")
            if name == "master.m3u8":   # three qualities: the app should keep the two sharp ones
                body = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=426x240\nlow.m3u8?c=IOS\n#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1280x720\nmid.m3u8?c=IOS\n#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080\nhigh.m3u8?c=IOS\n"
                return self.send(200, body.encode(), "application/vnd.apple.mpegurl")
            if name not in ("audio.m4a", "video.mp4"):
                return self.send(403, b"")
            src = "yt_audio_frag.m4a" if name == "audio.m4a" else "yt_video_frag.mp4" if "id=v" in p else "sample_h264_aac.mp4"
            with open(os.path.join(MEDIA_DIR, src), "rb") as f:
                data = f.read()
            rng = self.headers.get("Range", "bytes=0-")
            if "&range=" in p:   # YouTube's own range parameter, as Nuvio's player sends it
                rng = "bytes=" + p.split("&range=")[1].split("&")[0]
                with open(os.environ.get("FAKE_TRACK_LOG", os.devnull), "ab") as f:
                    f.write(("yt piece " + rng + "\n").encode())
            a, _, b = rng[6:].partition("-")
            a = int(a or 0); b = min(int(b) if b else len(data) - 1, len(data) - 1)
            self.send_response(206)
            self.send_header("Content-Type", "video/mp4" if name.endswith("mp4") else "audio/mp4")
            self.send_header("Content-Range", f"bytes {a}-{b}/{len(data)}")
            self.send_header("Content-Length", str(b - a + 1))
            self.end_headers()
            self.wfile.write(data[a:b + 1])
            return
        if p == "/android.json":   # the Android app's update notice
            return self.send(200, json.dumps({"version": "9.9.9", "apkUrl": f"{BASE}/cuelight-android-9.9.9.apk", "sha256": "cd" * 32}).encode())
        if p == "/repo.json":   # an update notice
            return self.send(200, json.dumps({"packages": [{"id": "org.stremio.nativetv", "manifest": {
                "version": "9.9.9", "ipkUrl": f"{BASE}/org.stremio.nativetv_9.9.9_arm.ipk", "ipkHash": {"sha256": "ab" * 32}}}]}).encode())
        if p.startswith("/sub_matched.srt"):   # the file-matched subtitle: the same cues, 2 s later
            src = SRT
            import re as _re
            def shift(m):
                h, mi, s, ms = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
                t = h * 3600 + mi * 60 + s + ms / 1000 + 2.0
                return "%02d:%02d:%02d,%03d" % (t // 3600, (t % 3600) // 60, t % 60, round((t - int(t)) * 1000))
            return self.send(200, _re.sub(r"(\d\d):(\d\d):(\d\d),(\d\d\d)", shift, src).encode("latin-1"), "text/plain")
        if signs and p.startswith("/subtitles/"):
            return self.send(200, json.dumps({"subtitles": [{"id": "signs", "url": f"{BASE}/signs.srt", "lang": "eng"}]}).encode())
        if p == "/signs.srt":
            return self.send(200, ("1\n00:00:00,200 --> 00:00:30,000\n{\\an8}for, like, a portion of the day\nand also take time out\n\n"
                                   "2\n00:00:00,300 --> 00:00:30,000\n<font color=\"#00ff00\">Co-Executive Producer</font>\n\n"
                                   "3\n00:00:00,300 --> 00:00:30,000\nSeth MacFarlane\n").encode(), "text/plain")
        if latesubs and p.startswith("/subtitles/"):
            return self.send(200, json.dumps({"subtitles": [{"id": "late", "url": f"{BASE}/gen.srt?n=7&delay=12", "lang": "eng"},
                                                            {"id": "quick", "url": f"{BASE}/gen.srt?n=5", "lang": "eng"}]}).encode())
        if bestfit:   # the same addon, with two English files: one stops an hour early
            if p.startswith("/subtitles/"):
                if "videoHash=" in p:
                    return self.send(200, b'{"subtitles": []}')
                return self.send(200, json.dumps({"subtitles": [{"id": "short", "url": f"{BASE}/gen.srt?n=300", "lang": "eng"},
                                                                {"id": "full", "url": f"{BASE}/gen.srt?n=650", "lang": "eng"}]}).encode())
        if p.startswith("/gen.srt"):   # n lines, one every 10 s
            n = int(p.split("n=")[1].split("&")[0])
            if "delay=" in p:
                import time
                time.sleep(float(p.split("delay=")[1].split("&")[0]))
            fmt = lambda t: "%02d:%02d:%02d,000" % (t // 3600, t % 3600 // 60, t % 60)
            body = "".join(f"{i + 1}\n{fmt(30 + i * 10)} --> {fmt(32 + i * 10)}\nLine {i + 1}\n\n" for i in range(n))
            return self.send(200, body.encode(), "text/plain")
        if p.startswith("/subtitles/"):
            if "videoHash=" in p:
                return self.send(200, json.dumps({"subtitles": [{"id": "eng-matched", "url": f"{BASE}/sub_matched.srt", "lang": "eng"}]}).encode())
            subs = [{"id": "fake-eng", "url": f"{BASE}/sub.srt", "lang": "eng"}, {"id": "pob1", "url": f"{BASE}/sub.srt", "lang": "pob"},
                    {"id": "pob2", "url": f"{BASE}/sub.srt", "lang": "pob"}, {"id": "spa1", "url": f"{BASE}/sub.srt", "lang": "spa"},
                    {"id": "eng2", "url": f"{BASE}/sub.srt", "lang": "eng"}, {"id": "por1", "url": f"{BASE}/sub.srt", "lang": "por"}]
            return self.send(200, json.dumps({"subtitles": subs}).encode())
        if p.startswith("/norange"):   # a server that ignores Range: 20 MB whatever was asked
            self.send_response(200)
            self.send_header("Content-Type", "video/x-matroska")
            self.send_header("Content-Length", str(20 << 20))
            self.end_headers()
            try:
                chunk = b"\0" * 65536
                for _ in range(320):
                    self.wfile.write(chunk)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if p.startswith("/video/"):
            drop = int(p.split("?drop=")[1]) if "?drop=" in p else 0   # the server cuts a download from 0 after this many bytes
            stall = int(p.split("?stall=")[1]) if "?stall=" in p else 0   # or goes quiet there, the connection left open
            die = int(p.split("?die=")[1]) if "?die=" in p else 0   # or closes there and refuses the rest
            if die and self.headers.get("Range", "bytes=0-")[6:].split("-")[0] not in ("", "0"):
                return self.send(503, b"")
            drop = drop or stall or die
            mirror, other = "?mirror=1" in p, "?other=1" in p   # the same file elsewhere; the same size, other bytes
            name = os.path.basename(re.split(r"\?(torbox|drop|stall|die|mirror|other)=", p)[0])   # TorBox links carry their id
            path = os.path.join(MEDIA_DIR, name)
            if not os.path.isfile(path):
                return self.send(404, b"")
            with open(path, "rb") as f:
                data = f.read()
            if len(data) < 262144:   # the real files are tiny: pad so the file hash (first + last 64 KB) applies
                data += b"\0" * (262144 - len(data))
            if drop or mirror:   # big enough that a cut lands in the middle; past the header, byte i is (i * 7 + 3) % 251
                data += bytes((i * 7 + 3) % 251 for i in range(len(data), 1048576))
            elif other:   # another encode: other bytes from early on, though the same size
                data = data[:4096] + bytes(b ^ 0x5A for b in data[4096:]) + bytes((i * 5 + 1) % 251 for i in range(len(data), 1048576))
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
                if drop and a == 0 and b + 1 > drop:   # the connection goes partway through
                    self.wfile.write(data[a:drop]); self.wfile.flush()
                    if stall:
                        sleep(60)
                    self.close_connection = True
                    return
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
            return self.send(200, json.dumps({"movie_results": [] if tv else [{"id": 1, "poster_path": "/p5.png", "backdrop_path": "/b5.png"}], "tv_results": [{"id": 1, "poster_path": "/p3.png"}] if tv else []}).encode(), "application/json")
        if p.startswith("/tmdb/3/collection/"):   # a franchise, out of order on purpose
            parts = [{"id": 3, "title": "Fake Saga: Return", "release_date": "2014-05-01", "poster_path": "/p2.png"},
                     {"id": 1, "title": "Fake Saga", "release_date": "2008-03-01", "poster_path": "/p0.png"},
                     {"id": 2, "title": "Fake Saga: Again", "release_date": "2011-07-01", "poster_path": "/p1.png"}]
            return self.send(200, json.dumps({"id": 10, "name": "Fake Saga Collection", "parts": parts}).encode(), "application/json")
        # the built-in catalog (cuelight://catalog) asks these: lists, searches, a film's and a series' details
        TM_MOVIES = [{"id": 1, "title": "Fake Saga", "release_date": "2008-03-01", "poster_path": "/p0.png", "backdrop_path": "/b0.png", "overview": "The first one.", "genre_ids": [28, 12]},
                     {"id": 2, "title": "Fake Saga: Again", "release_date": "2011-07-01", "poster_path": "/p1.png", "backdrop_path": "/b1.png", "overview": "The second one.", "genre_ids": [28]},
                     {"id": 3, "title": "Quiet Harbour", "release_date": "2019-09-12", "backdrop_path": "/b2.png", "overview": "A drama by the sea.", "genre_ids": [18]}]
        TM_SHOWS = [{"id": 7, "name": "Breaking Bad", "first_air_date": "2008-01-20", "poster_path": "/p3.png", "backdrop_path": "/b3.png", "overview": "A teacher turns.", "genre_ids": [18, 80]}]
        if (p.startswith("/tmdb/3/discover/") and ("vote_count.gte=200" in p or "vote_count.gte=100" in p)) or p.startswith("/tmdb/3/trending/"):
            tv = "/tv" in p.split("?")[0]
            res = TM_SHOWS if tv else TM_MOVIES
            if "with_genres=18" in p: res = [m for m in res if 18 in m["genre_ids"]]
            if "page=1" not in p: res = []
            return self.send(200, json.dumps({"page": 1, "results": res}).encode(), "application/json")
        if p.startswith("/tmdb/3/search/movie") or p.startswith("/tmdb/3/search/tv"):
            from urllib.parse import unquote
            q = unquote(p.split("query=")[1].split("&")[0]).lower()
            res = [m for m in (TM_SHOWS if "/search/tv" in p else TM_MOVIES) if q in (m.get("title") or m.get("name")).lower()]
            return self.send(200, json.dumps({"page": 1, "results": res}).encode(), "application/json")
        if p.startswith("/tmdb/3/tv/1?") and "season/" in p:
            body = {}
            for sn in (1, 2):
                body[f"season/{sn}"] = {"episodes": [{"episode_number": e, "name": f"Episode {e} of season {sn}", "air_date": f"20{8 + sn:02d}-01-{e:02d}", "still_path": f"/s{e}.png", "overview": "Something happens."} for e in (1, 2, 3)]}
            return self.send(200, json.dumps(body).encode(), "application/json")
        if p.startswith("/tmdb/3/tv/1?") and "aggregate_credits,videos,images" in p:
            cast = [{"name": "Bryan Cranston", "roles": [{"character": "Walter White"}], "profile_path": "/p0.png"}, {"name": "Aaron Paul", "roles": [{"character": "Jesse Pinkman"}], "profile_path": "/p1.png"}]
            return self.send(200, json.dumps(dict(TM_SHOWS[0], id=1, status="Ended", last_air_date="2013-09-29", episode_run_time=[47], genres=[{"name": "Drama"}, {"name": "Crime"}],
                created_by=[{"name": "Vince Gilligan"}], production_countries=[{"name": "United States of America"}], seasons=[{"season_number": 1}, {"season_number": 2}],
                aggregate_credits={"cast": cast}, videos={"results": [{"site": "YouTube", "type": "Trailer", "key": "dQw4w9WgXcQ", "official": True}]}, images={"logos": []})).encode(), "application/json")
        if p.startswith("/tmdb/3/movie/") and "append_to_response=credits,videos,images" in p:
            n = int(p.split("/movie/")[1].split("?")[0])
            m = next((x for x in TM_MOVIES if x["id"] == n), TM_MOVIES[0])
            return self.send(200, json.dumps(dict(m, runtime=101, genres=[{"name": "Action"}], production_countries=[{"name": "United States of America"}],
                credits={"cast": [{"name": "Tim Robbins", "character": "Andy Dufresne", "profile_path": "/p0.png"}], "crew": [{"job": "Director", "name": "Frank Darabont"}, {"job": "Screenplay", "name": "Frank Darabont"}]},
                videos={"results": [{"site": "YouTube", "type": "Trailer", "key": "dQw4w9WgXcQ", "official": True}]}, images={"logos": [{"iso_639_1": "en", "file_path": "/logo.png"}]})).encode(), "application/json")
        if p.startswith("/tmdb/3/discover/"):   # titles for filters: only a Hindi comedy of the 90s asked for in full gets any
            ok = all(k in p for k in ("with_original_language=hi", "with_origin_country=IN", "with_genres=35", "primary_release_date.gte=1990-01-01", "primary_release_date.lte=1999-12-31"))
            res = [{"id": 4, "title": "Fake Hindi Comedy", "release_date": "1997-05-01", "poster_path": "/p3.png"},
                   {"id": 1, "title": "Fake Saga", "release_date": "2008-03-01", "poster_path": "/p0.png"}] if ok else []
            return self.send(200, json.dumps({"results": res}).encode(), "application/json")
        if p.startswith("/tmdb/3/search/person"):
            return self.send(200, json.dumps({"results": [{"id": 17419, "name": "Bryan Cranston"}]}).encode(), "application/json")
        if p.startswith("/tmdb/3/person/") and "combined_credits" in p:   # films, a series, and two that should be left out
            return self.send(200, json.dumps({"cast": [
                {"media_type": "movie", "id": 1, "title": "Fake Saga", "release_date": "2008-03-01", "popularity": 50, "poster_path": "/p0.png"},
                {"media_type": "tv", "id": 7, "name": "Breaking Bad", "first_air_date": "2008-01-20", "popularity": 80, "episode_count": 62},
                {"media_type": "tv", "id": 8, "name": "Late Night Talk", "popularity": 99, "episode_count": 3, "genre_ids": [10767]},
                {"media_type": "tv", "id": 9, "name": "One Cameo", "popularity": 90, "episode_count": 1}]}).encode(), "application/json")
        if p.startswith("/tmdb/3/tv/") and "/episode/" in p and "/credits" in p:   # an episode's guest stars
            return self.send(200, json.dumps({"guest_stars": [{"id": 31, "name": "Jonathan Banks", "character": "Mike Ehrmantraut", "profile_path": "/g1.png"},
                                                              {"id": 32, "name": "Raymond Cruz", "character": "Tuco Salamanca", "profile_path": "/g2.png"}]}).encode(), "application/json")
        if p.startswith("/tmdb/3/tv/") and "/external_ids" in p:
            return self.send(200, json.dumps({"imdb_id": "tt0903747"}).encode(), "application/json")
        if p.startswith("/tmdb/3/movie/") and "/external_ids" in p:
            n = int(p.split("/movie/")[1].split("/")[0])
            return self.send(200, json.dumps({"imdb_id": f"tt{1000 + n}"}).encode(), "application/json")
        if p.startswith("/tmdb/3/movie/") or p.startswith("/tmdb/3/tv/"):
            tv = "/tv/" in p
            cast = [{"name": n, "character": c, "profile_path": f"/p{i}.png"} for i, (n, c) in enumerate([("Bryan Cranston", "Walter White"), ("Aaron Paul", "Jesse Pinkman"), ("Anna Gunn", "Skyler White"), ("Dean Norris", "Hank Schrader")])]
            if tv:
                for cm in cast: cm["roles"] = [{"character": cm.pop("character")}]
                body = {"aggregate_credits": {"cast": cast}, "content_ratings": {"results": [{"iso_3166_1": "US", "rating": "TV-MA"}]}, "tagline": "Remember my name."}
            else:
                body = {"videos": {"results": [{"site": "YouTube", "type": "Teaser", "key": "teaserOnly1", "official": True},
                                               {"site": "YouTube", "type": "Trailer", "key": "fileOnly001", "official": True}]},
                        "belongs_to_collection": {"id": 10, "name": "Fake Saga Collection"}, "keywords": {"keywords": [{"id": 179430, "name": "aftercreditsstinger"}]}, "tagline": "Fear can hold you prisoner. Hope can set you free.", "credits": {"cast": cast}, "release_dates": {"results": [{"iso_3166_1": "US", "release_dates": [{"certification": "PG-13"}]}]}}
            return self.send(200, json.dumps(body).encode(), "application/json")
        if p.startswith("/tmdbimg/"):
            i = int(p.rsplit("/p", 1)[1].split(".")[0]) if "/p" in p else 0
            return self.send(200, art("poster", i + 2, ""), "image/png")
        if p.startswith("/torbox/"):   # TorBox: your files (key "tbkey"), and the permanent link that forwards to one
            q = p[8:]
            if q.startswith("torrents/checkcached"):   # hashes starting "cafe" are cached
                if self.headers.get("Authorization") != "Bearer tbkey":
                    return self.send(401, b'{"success":false}')
                hashes = q.split("hash=")[1].split("&")[0].split(",") if "hash=" in q else []
                return self.send(200, json.dumps({"success": True, "data": [{"hash": h, "name": "x", "size": 1} for h in hashes if h.startswith("cafe")]}).encode())
            if "/mylist" in q and "id=79" in q:   # the season pack: its largest file is another episode
                return self.send(200, json.dumps({"success": True, "data": {"id": 79, "download_present": True, "download_finished": True, "files": [
                    {"id": 10, "name": "Breaking Bad S01 1080p/Sample.mkv", "short_name": "Sample.mkv", "mimetype": "video/x-matroska", "size": 40000000},
                    {"id": 11, "name": "Breaking Bad S01 1080p/Breaking.Bad.S01E01.1080p.mkv", "short_name": "Breaking.Bad.S01E01.1080p.mkv", "mimetype": "video/x-matroska", "size": 1500000000},
                    {"id": 12, "name": "Breaking Bad S01 1080p/Breaking.Bad.S01E02.1080p.mkv", "short_name": "Breaking.Bad.S01E02.1080p.mkv", "mimetype": "video/x-matroska", "size": 1900000000},
                    {"id": 13, "name": "Breaking Bad S01 1080p/Breaking.Bad.S01E03.1080p.mkv", "short_name": "Breaking.Bad.S01E03.1080p.mkv", "mimetype": "video/x-matroska", "size": 1700000000}]}}).encode())
            if "/mylist" in q and "id=81" in q:   # episodes 2 and 3 only (and an extra): episode 1 is not in it
                return self.send(200, json.dumps({"success": True, "data": {"id": 81, "download_present": True, "download_finished": True, "files": [
                    {"id": 30, "name": "Breaking Bad S01 Part 2/NCOP.mkv", "short_name": "NCOP.mkv", "mimetype": "video/x-matroska", "size": 90000000},
                    {"id": 31, "name": "Breaking Bad S01 Part 2/Breaking.Bad.S01E02.1080p.mkv", "short_name": "Breaking.Bad.S01E02.1080p.mkv", "mimetype": "video/x-matroska", "size": 1900000000},
                    {"id": 32, "name": "Breaking Bad S01 Part 2/Breaking.Bad.S01E03.1080p.mkv", "short_name": "Breaking.Bad.S01E03.1080p.mkv", "mimetype": "video/x-matroska", "size": 1700000000}]}}).encode())
            if "/mylist" in q and "id=80" in q:   # the disc rip: nothing in the names says which episode
                return self.send(200, json.dumps({"success": True, "data": {"id": 80, "download_present": True, "download_finished": True, "files": [
                    {"id": 20, "name": "BREAKING_BAD_D1/title_t00.mkv", "short_name": "title_t00.mkv", "mimetype": "video/x-matroska", "size": 2600000000},
                    {"id": 21, "name": "BREAKING_BAD_D1/title_t01.mkv", "short_name": "title_t01.mkv", "mimetype": "video/x-matroska", "size": 2400000000},
                    {"id": 22, "name": "BREAKING_BAD_D1/title_t02.mkv", "short_name": "title_t02.mkv", "mimetype": "video/x-matroska", "size": 2500000000}]}}).encode())
            if "/mylist" in q and "id=82" in q:   # downloading: 42%, then 87%, then done
                H.f00d_looks = getattr(H, "f00d_looks", 0) + 1
                done = H.f00d_looks >= 3
                return self.send(200, json.dumps({"success": True, "data": {"id": 82, "download_present": done, "download_finished": done,
                    "progress": 1 if done else 0.42 if H.f00d_looks == 1 else 0.87, "download_speed": 3145728, "seeds": 1,
                    "download_state": "completed" if done else "downloading", "files": [
                    {"id": 1, "short_name": "Movie.2024.1080p.mkv", "mimetype": "video/x-matroska", "size": 2147483648}] if done else []}}).encode())
            if "/mylist" in q and "id=7" in q:   # the torrents added above
                ready = "id=77" in q
                return self.send(200, json.dumps({"success": True, "data": {"id": 77 if ready else 78, "download_present": ready, "download_finished": ready, "files": [
                    {"id": 0, "short_name": "Sample.txt", "mimetype": "text/plain", "size": 10},
                    {"id": 1, "short_name": "Movie.2024.1080p.mkv", "mimetype": "video/x-matroska", "size": 2147483648}] if ready else []}}).encode())
            if "/mylist" in q:
                lib = self.headers.get("Authorization") == "Bearer tbkeylib"   # the account with Breaking Bad copies
                if self.headers.get("Authorization") != "Bearer tbkey" and not lib:
                    return self.send(401, b'{"success":false,"error":"AUTH_ERROR"}')
                kind = q.split("/")[0]
                if kind != "torrents":
                    return self.send(404, b'{"success":false,"error":"NOT_FOUND"}')
                return self.send(200, json.dumps({"success": True, "data": [
                    {"id": 41, "name": "Fireplace 4K", "files": [
                        {"id": 0, "short_name": "fireplace.mp4", "mimetype": "video/mp4", "size": 734003200},
                        {"id": 1, "short_name": "readme.txt", "mimetype": "text/plain", "size": 12}]},
                    {"id": 42, "name": "Adhan", "files": [{"id": 3, "short_name": "adhan.mkv", "mimetype": "video/x-matroska", "size": 52428800}]},
                    {"id": 43, "name": "The.Shawshank.Redemption.1994.1080p.BluRay", "files": [
                        {"id": 5, "short_name": "The.Shawshank.Redemption.1994.1080p.BluRay.x264.mkv", "mimetype": "video/x-matroska", "size": 8589934592}]},
                    {"id": 44, "name": "The Shawshank Redemption 2031 remake", "files": [
                        {"id": 6, "short_name": "Shawshank.Redemption.2031.2160p.mkv", "mimetype": "video/x-matroska", "size": 1073741824}]}] + ([
                    # Breaking Bad S01E01 "Pilot": one right file; another show starting with the same
                    # words, a file of episode 2 in an item named for episode 1, and a file numbered 1
                    # titled as episode 2 (orderings differ between services) are all left out
                    {"id": 45, "name": "Breaking.Bad.S01E01.Pilot.1080p", "files": [
                        {"id": 7, "short_name": "Breaking.Bad.S01E01.Pilot.1080p.mkv", "mimetype": "video/x-matroska", "size": 1073741824}]},
                    {"id": 46, "name": "Breaking.Bad.Insider.S01E01", "files": [
                        {"id": 8, "short_name": "Breaking.Bad.Insider.S01E01.mkv", "mimetype": "video/x-matroska", "size": 1073741824}]},
                    {"id": 47, "name": "Breaking.Bad.S01E01.Pack", "files": [
                        {"id": 9, "short_name": "Breaking.Bad.S01E02.1080p.mkv", "mimetype": "video/x-matroska", "size": 1073741824}]},
                    {"id": 48, "name": "Breaking.Bad.S01E01.NF", "files": [
                        {"id": 10, "short_name": "Breaking.Bad.S01E01.Cats.in.the.Bag.1080p.mkv", "mimetype": "video/x-matroska", "size": 1073741824}]}] if lib else [])}).encode())
            if "/requestdl" in q:
                if "token=tbkey" not in q:
                    return self.send(403, b'{"success":false}')
                if "redirect=true" not in q:   # the file's own link, as JSON
                    tid = q.split("_id=")[1].split("&")[0]
                    return self.send(200, json.dumps({"success": True, "data": f"{BASE}/video/sample_h264_aac.mkv?torbox={tid}"}).encode())
                self.send_response(302)
                self.send_header("Location", f"{BASE}/video/sample_h264_aac.mp4?torbox=1")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            return self.send(404, b"{}")
        if p.startswith("/weather/v1/forecast"):   # Open-Meteo stand-in: a mild, partly cloudy day
            return self.send(200, json.dumps({"current": {"temperature_2m": 18.4, "weather_code": 2, "is_day": 1}}).encode())
        if p.startswith("/ipapi"):
            return self.send(200, json.dumps({"status": "success", "city": "Makkah", "countryCode": "SA", "lat": 21.4225, "lon": 39.8262}).encode(), "application/json")
        if p.startswith("/thumbs/frame"):   # the seek preview server (tools/thumbs) stands in
            t = int(float(p.split("t=")[1].split("&")[0])) if "t=" in p else 0
            if "scope%3D1" in p and HAVE_PIL:   # a 2.39:1 picture with the bars baked into a 16:9 frame
                from PIL import Image as _I
                own = os.environ.get("FAKE_SCOPE_PIC")   # a real still, for screenshots
                pic = (_I.open(own).convert("RGB") if own else _I.open(io.BytesIO(art("bg", (t // 10) % 6)))).resize((1280, 536))
                frame = _I.new("RGB", (1280, 720)); frame.paste(pic, (0, 92))
                b = io.BytesIO(); frame.save(b, "JPEG", quality=85)
                return self.send(200, b.getvalue(), "image/jpeg")
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
