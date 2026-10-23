#!/usr/bin/env bash
# Homebrew Channel repository for LG webOS, generated on every release.
#
# The Homebrew Channel accepts extra repositories: a URL that returns JSON with
# `packages[]`, each carrying the app's manifest (id, version, ipkUrl, sha256),
# in the same format as the official repo (repo.webosbrew.org/api/apps.json).
#
# No server needed: both files are attached to the GitHub release, and the URL
# people add in the channel is GitHub's "latest release" link, which always
# redirects to the newest one:
#   https://github.com/<owner>/<repo>/releases/latest/download/repo.json
#
# Usage: bash tools/hb-repo.sh <package.ipk> <out-dir>
# Writes <out-dir>/webosbrew.manifest.json and <out-dir>/repo.json.
set -euo pipefail
cd "$(dirname "$0")/.."

IPK="${1:?usage: hb-repo.sh <package.ipk> <out-dir>}"
OUT="${2:?usage: hb-repo.sh <package.ipk> <out-dir>}"
# The public repository that carries the releases (the source repository can be private).
REPO="${RELEASES_REPO:-smalik29871/stremio-custom-lg-tv-releases}"
mkdir -p "$OUT"

python3 - "$IPK" "$OUT" "$REPO" <<'PY'
import hashlib, json, os, sys
ipk, out, repo = sys.argv[1], sys.argv[2], sys.argv[3]
app = json.load(open("deploy/webos/appinfo.json"))
version = app["version"]
name = os.path.basename(ipk)
# The asset name must be the published file's name: ipkUrl is built from it.
expected = f"{app['id']}_{version}_arm.ipk"
if name != expected:
    sys.exit(f"hb-repo.sh: expected {expected}, got {name}")
sha = hashlib.sha256(open(ipk, "rb").read()).hexdigest()
base = f"https://github.com/{repo}/releases/download/v{version}"
icon = f"{base}/largeIcon.png"   # published with the release: the source may be private
manifest = {
    "id": app["id"],
    "version": version,
    "type": "native",
    "title": "Cuelight",
    "appDescription": "A fast native player for your Stremio account and addons on LG webOS. Not affiliated with Stremio.",
    "iconUri": icon,
    "sourceUrl": f"https://github.com/{repo}",
    # Installs and runs without root. Whether the media bus grants video to a
    # Homebrew Channel install on a non-rooted TV is not measured yet; see README.
    "rootRequired": False,
    "ipkUrl": f"{base}/{name}",
    "ipkHash": {"sha256": sha},
    "ipkSize": os.path.getsize(ipk),
}
index = {
    "paging": {"page": 1, "count": 1, "maxPage": 1, "itemsTotal": 1, "prevUrl": None, "nextUrl": None},
    "packages": [{
        "id": app["id"],
        "title": manifest["title"],
        "iconUri": icon,
        "manifestUrl": f"{base}/webosbrew.manifest.json",
        "manifest": manifest,
        "pool": "main",
        "shortDescription": manifest["appDescription"],
    }],
}
json.dump(manifest, open(os.path.join(out, "webosbrew.manifest.json"), "w"), indent=2)
json.dump(index, open(os.path.join(out, "repo.json"), "w"), indent=2)
print(f"hb-repo.sh: {version} sha256={sha[:12]}... -> {out}/repo.json")
PY
