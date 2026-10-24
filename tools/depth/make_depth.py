#!/usr/bin/env python3
"""Depth maps for the app's backdrops (Home pushing into the picture, the Depth screensaver).

Each backdrop gets a small grayscale map (white is near) from Depth Anything V2 Small, and a
line in index.txt the app reads first:

    <key> <foreground threshold or -> <the clock's centre across the screen, 0..1, or ->

Keys follow the image, not its size: TMDB backdrops are t<file name>, metahub's are m<IMDb id>,
anything else h<FNV-1a 64 of the URL, hex>, the hash the app computes (src/depth.c).

    pip install onnxruntime numpy pillow scipy
    python3 make_depth.py --model depth.onnx --out <a checkout of the depth-maps branch> --cinemeta 150 [--tmdb 100] [--url URL ...]

Refreshed weekly by tools/depth/depth-maps.yml (copy it into .github/workflows).

The model: https://huggingface.co/onnx-community/depth-anything-v2-small (onnx/model.onnx).
--tmdb reads TMDB_API_KEY from the environment (never printed). Maps already made are kept.
"""
import argparse, io, json, os, re, sys, urllib.request
import numpy as np
from PIL import Image

UA = {"User-Agent": "Cuelight-depth/1"}
W_OUT = 384          # map width; height follows the image
CLOCK_W, CLOCK_Y = 0.38, (0.06, 0.36)   # the clock's block on the 16:9 screen (src/ui/screen_ambient.c), slid across the top


def get(url, timeout=30):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout) as r:
        return r.read()


def fnv64(s):
    h = 1469598103934665603
    for b in s.encode():
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def key_for(url):
    m = re.match(r"https?://image\.tmdb\.org/t/p/[^/]+/([A-Za-z0-9_-]+)\.\w+$", url)
    if m: return "t" + m.group(1)
    m = re.match(r"https?://images\.metahub\.space/background/[^/]+/(tt\d+)/img$", url)
    if m: return "m" + m.group(1)
    return "h%016x" % fnv64(url)


def cinemeta(n):
    urls = []
    for kind in ("movie", "series"):
        skip = 0
        while len([u for u in urls if kind in u[1]]) < n:
            path = "top" if not skip else "top/skip=%d" % skip
            metas = json.loads(get("https://cinemeta-catalogs.strem.io/top/catalog/%s/%s.json" % (kind, path)))["metas"]
            if not metas: break
            for m in metas:
                tt = m.get("imdb_id") or m.get("id")
                if tt and tt.startswith("tt"): urls.append(("https://images.metahub.space/background/medium/%s/img" % tt, kind))
            skip += len(metas)
    return [u for u, _ in urls]


def tmdb(n):
    key = os.environ.get("TMDB_API_KEY", "")
    if not key: print("--tmdb: no TMDB_API_KEY, skipped"); return []
    urls = []
    for path in ("trending/movie/week", "trending/tv/week", "movie/popular", "tv/popular"):
        page = 1
        while page <= 5 and len([u for u in urls if u[1] == path]) < n:
            try: res = json.loads(get("https://api.themoviedb.org/3/%s?api_key=%s&page=%d" % (path, key, page)))["results"]
            except Exception as e: print("tmdb %s: %s" % (path, type(e).__name__)); break
            urls += [("https://image.tmdb.org/t/p/w1280" + r["backdrop_path"], path) for r in res if r.get("backdrop_path")]
            page += 1
    return list(dict.fromkeys(u for u, _ in urls))


class Depth:
    def __init__(self, model):
        import onnxruntime as ort
        self.s = ort.InferenceSession(model, providers=["CPUExecutionProvider"])
        self.inp = self.s.get_inputs()[0].name

    def __call__(self, im):
        W, H = im.size
        ih = 518; iw = max(14, int(round(ih * W / H / 14)) * 14)
        x = np.asarray(im.resize((iw, ih), Image.BICUBIC), np.float32) / 255
        x = ((x - [0.485, 0.456, 0.406]) / [0.229, 0.224, 0.225]).transpose(2, 0, 1)[None].astype(np.float32)
        d = self.s.run(None, {self.inp: x})[0][0]
        lo, hi = np.percentile(d, 1), np.percentile(d, 99)
        return np.clip((d - lo) / (hi - lo + 1e-6), 0, 1)


def otsu(d):
    hist, edges = np.histogram(d, bins=64, range=(0, 1))
    p = hist / hist.sum(); best, th = -1, 0.5
    for i in range(1, 64):
        w0, w1 = p[:i].sum(), p[i:].sum()
        if w0 < 1e-3 or w1 < 1e-3: continue
        m0 = (p[:i] * edges[:i]).sum() / w0; m1 = (p[i:] * edges[i:64]).sum() / w1
        v = w0 * w1 * (m0 - m1) ** 2
        if v > best: best, th = v, edges[i]
    return th


def placement(d):
    """The foreground threshold, and the clock's block that people overlap a little (or not at all)."""
    from scipy import ndimage
    th = otsu(d)
    fg = ndimage.binary_opening(d > th, iterations=2)
    frac = fg.mean()
    if not (0.05 <= frac <= 0.5) or d[fg].mean() - d[~fg].mean() < 0.25: return None, "-"
    H, W = fg.shape
    sa, ia = 16 / 9, W / H   # the screen shows a cover-fit crop
    x0, x1, y0, y1 = 0, W, 0, H
    if ia > sa: c = int(H * sa); x0 = (W - c) // 2; x1 = x0 + c
    else: c = int(W / sa); y0 = (H - c) // 2; y1 = y0 + c
    crop = fg[y0:y1, x0:x1]; ch, cw = crop.shape
    over = {}
    for c in np.arange(0.04 + CLOCK_W / 2, 0.96 - CLOCK_W / 2 + 1e-6, 0.02):
        a, b = c - CLOCK_W / 2, c + CLOCK_W / 2
        over[round(float(c), 2)] = crop[int(CLOCK_Y[0] * ch):int(CLOCK_Y[1] * ch), int(a * cw):int(b * cw)].mean()
    # people cover a little of it (about 7%): behind them, still easy to read
    some = [c for c in over if 0.025 <= over[c] <= 0.14]
    if some: return th, "%.2f" % min(some, key=lambda c: abs(over[c] - 0.07))
    c = min(over, key=over.get)
    return th, ("%.2f" % c) if over[c] < 0.025 else "-"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cinemeta", type=int, default=0)
    ap.add_argument("--tmdb", type=int, default=0)
    ap.add_argument("--url", action="append", default=[])
    ap.add_argument("--replace-placement", action="store_true", help="weigh the clock's place again for the maps already made")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    idx_path = os.path.join(a.out, "index.txt")
    index = {}
    if os.path.exists(idx_path):
        for line in open(idx_path):
            if line.strip() and not line.startswith("#"): k, rest = line.split(" ", 1); index[k] = rest.strip()
    if a.replace_placement:
        for k in list(index):
            png = os.path.join(a.out, k + ".png")
            if os.path.exists(png):
                th, pos = placement(np.asarray(Image.open(png), np.float32) / 255)
                index[k] = "%s %s" % ("-" if th is None else "%.2f" % th, pos)
        write(idx_path, index)
    urls = list(a.url) + (cinemeta(a.cinemeta) if a.cinemeta else []) + (tmdb(a.tmdb) if a.tmdb else [])
    urls = list(dict.fromkeys(urls))
    print("%d backdrops, %d maps already" % (len(urls), len(index)))
    depth = None; made = 0
    for url in urls:
        key = key_for(url)
        if key in index and os.path.exists(os.path.join(a.out, key + ".png")): continue
        try: im = Image.open(io.BytesIO(get(url))).convert("RGB")
        except Exception as e: print("skip %s: %s" % (key, type(e).__name__)); continue
        if depth is None: depth = Depth(a.model)
        d = depth(im)
        from scipy import ndimage
        d = ndimage.median_filter(d, 3)
        h = max(8, round(W_OUT * im.height / im.width))
        small = Image.fromarray((d * 255).astype(np.uint8)).resize((W_OUT, h), Image.BILINEAR)
        small.save(os.path.join(a.out, key + ".png"), optimize=True)
        th, pos = placement(np.asarray(small, np.float32) / 255)
        index[key] = "%s %s" % ("-" if th is None else "%.2f" % th, pos)
        made += 1
        if made % 20 == 0: print("%d made" % made); write(idx_path, index)
    write(idx_path, index)
    print("%d made, %d in the index" % (made, len(index)))


def write(path, index):
    with open(path + ".tmp", "w") as f:
        f.write("# cuelight depth maps 1\n")
        for k in sorted(index): f.write("%s %s\n" % (k, index[k]))
    os.replace(path + ".tmp", path)


if __name__ == "__main__":
    sys.exit(main())
