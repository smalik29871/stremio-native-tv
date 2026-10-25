#!/usr/bin/env bash
# Packages Cuelight's YouTube player (deploy/webos-youtube: a small web app that opens YouTube's
# own TV client) as <out-dir>/org.stremio.nativetv.yt_<version>_arm.ipk, at Cuelight's version.
# tools/hb-repo.sh lists it in the Homebrew Channel repository beside Cuelight.
#   tools/webos-youtube.sh <out-dir>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:?usage: webos-youtube.sh <out-dir>}"
STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$OUT"
cp -r "$ROOT/deploy/webos-youtube/." "$STAGE/"
python3 - "$STAGE/appinfo.json" "$ROOT/deploy/webos/appinfo.json" <<'PY'
import json, sys
app = json.load(open(sys.argv[1]))
app["version"] = json.load(open(sys.argv[2]))["version"]
json.dump(app, open(sys.argv[1], "w"), indent=2)
PY
bash "$ROOT/tools/package-ipk.sh" "$STAGE" "$OUT"
