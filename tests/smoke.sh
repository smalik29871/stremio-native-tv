#!/usr/bin/env bash
# Headless UI smoke test: runs the desktop build under Xvfb against the fake
# addon, drives a few keys, and writes screenshots to tests/out/.
# Usage: bash tests/smoke.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="$ROOT/tests/out"
mkdir -p "$OUT"
PORT=8765
python3 -I "$ROOT/tests/fake_addon.py" $PORT & SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
sleep 0.5
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1
export SNTV_DEFAULT_ADDONS="http://127.0.0.1:$PORT/manifest.json"
export HOME="$OUT/home"; mkdir -p "$HOME"
run() { # name frames keys
  SNTV_SCREENSHOT="$OUT/$1.ppm" SNTV_SCREENSHOT_FRAMES="$2" SNTV_KEYS="$3" \
    xvfb-run -a -s "-screen 0 1280x720x24" "$BUILD/stremio-tv" >"$OUT/$1.log" 2>&1 || { echo "run $1 failed"; tail -20 "$OUT/$1.log"; exit 1; }
  echo "ok: $1"
}
run home 90 "down"
run detail_movie 140 "down,ok"
run detail_series 160 "down,right,ok"
run episodes 180 "down,right,ok,ok"
run streams 200 "down,right,ok,ok,ok"
run player 220 "down,right,ok,ok,ok,ok"
run search 200 "search,t:brea,right,right"
run settings 120 "right,ok"
run login 140 "right,right,ok"
python3 -I - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
print("screenshots:", sorted(os.path.basename(x) for x in glob.glob(os.path.join(sys.argv[1], "*.png"))))
PY
