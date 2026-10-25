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
# Writes <out-dir>/webosbrew.manifest.json and <out-dir>/repo.json. Cuelight's YouTube player
# (tools/webos-youtube.sh, build-webos/youtube/) is listed second and copied beside them.
set -euo pipefail
cd "$(dirname "$0")/.."

IPK="${1:?usage: hb-repo.sh <package.ipk> <out-dir>}"
OUT="${2:?usage: hb-repo.sh <package.ipk> <out-dir>}"
# The public repository that carries the releases (the source repository can be private).
REPO="${RELEASES_REPO:-smalik29871/cuelight-releases}"
YT_IPK="${YT_IPK:-$(ls build-webos/youtube/org.stremio.nativetv.yt_*.ipk 2>/dev/null | head -1)}"
mkdir -p "$OUT"

python3 - "$IPK" "$OUT" "$REPO" "$YT_IPK" <<'PY'
import hashlib, json, os, shutil, sys
ipk, out, repo, yt = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
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
if yt:   # Cuelight's YouTube player: Cuelight installs it from here the first time YouTube refuses its own
    ytname = os.path.basename(yt)
    shutil.copy(yt, os.path.join(out, ytname))
    ytsha = hashlib.sha256(open(yt, "rb").read()).hexdigest()
    ytm = {
        "id": "org.stremio.nativetv.yt",
        "version": version,
        "type": "web",
        "title": "Cuelight YouTube",
        "appDescription": "Plays in YouTube's own TV client the trailers, fireplace and break videos YouTube refuses to Cuelight. Opened by Cuelight; Back returns to it.",
        "iconUri": icon,
        "sourceUrl": f"https://github.com/{repo}",
        "rootRequired": False,
        "ipkUrl": f"{base}/{ytname}",
        "ipkHash": {"sha256": ytsha},
        "ipkSize": os.path.getsize(yt),
    }
    json.dump(ytm, open(os.path.join(out, "org.stremio.nativetv.yt.manifest.json"), "w"), indent=2)
    index["packages"].append({"id": ytm["id"], "title": ytm["title"], "iconUri": icon, "manifestUrl": f"{base}/org.stremio.nativetv.yt.manifest.json",
                              "manifest": ytm, "pool": "main", "shortDescription": ytm["appDescription"]})
    index["paging"]["count"] = index["paging"]["itemsTotal"] = len(index["packages"])
    print(f"hb-repo.sh: and {ytname} sha256={ytsha[:12]}...")
json.dump(manifest, open(os.path.join(out, "webosbrew.manifest.json"), "w"), indent=2)
json.dump(index, open(os.path.join(out, "repo.json"), "w"), indent=2)
print(f"hb-repo.sh: {version} sha256={sha[:12]}... -> {out}/repo.json")
PY
