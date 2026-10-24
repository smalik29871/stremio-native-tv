#!/usr/bin/env bash
# Build a webOS .ipk from a staged app directory with LG's own packager
# (ares-package, from the webOS CLI, or fetched by npx), else by hand in the
# same layout. A TV in Developer Mode verifies the package before installing
# ("-5: ipk verified failed"); the Homebrew Channel does not, so a layout only
# it accepts goes unnoticed on rooted TVs.
#   tools/package-ipk.sh <stage-dir> <out-dir>
set -euo pipefail
STAGE="$1"; OUT="$2"
ID=$(python3 -c "import json,sys;print(json.load(open('$STAGE/appinfo.json'))['id'])")
VER=$(python3 -c "import json,sys;print(json.load(open('$STAGE/appinfo.json'))['version'])")
ARES_CLI="@webos-tools/cli@3.2.6"
if command -v ares-package >/dev/null 2>&1; then
  ares-package --no-minify -o "$OUT" "$STAGE"
  exit 0
fi
if command -v npx >/dev/null 2>&1 && npx --yes -p "$ARES_CLI" ares-package --no-minify -o "$OUT" "$STAGE"; then
  exit 0
fi
echo "ares-package not available: packaging by hand" >&2
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/data/usr/palm/applications/$ID"
PKGDIR="$WORK/data/usr/palm/packages/$ID"
mkdir -p "$APPDIR" "$PKGDIR" "$WORK/ctl"
cp -r "$STAGE"/. "$APPDIR/"
chmod 755 "$APPDIR/stremio-tv"
printf '{"id":"%s","version":"%s","app":"%s"}\n' "$ID" "$VER" "$ID" > "$PKGDIR/packageinfo.json"
SIZE=$(find "$WORK/data" -type f -printf '%s\n' | awk '{ s += $1 } END { print s }')   # bytes, as ares-package writes it
cat > "$WORK/ctl/control" <<CTL
Package: $ID
Version: $VER
Section: misc
Priority: optional
Architecture: arm
Installed-Size: $SIZE
Maintainer: N/A <nobody@example.com>
Description: Stremio native client
webOS-Package-Format-Version: 2
webOS-Packager-Version: x.y.x
CTL
echo 2.0 > "$WORK/debian-binary"
(cd "$WORK/ctl" && tar --owner=0 --group=0 -czf "$WORK/control.tar.gz" control)
(cd "$WORK/data" && tar --format=ustar --owner=0 --group=0 -czf "$WORK/data.tar.gz" usr)   # "usr/...", no "./" entries
IPK="$OUT/${ID}_${VER}_arm.ipk"
rm -f "$IPK"
# The ar container as ares-package writes it: member names without GNU ar's trailing "/"
python3 - "$IPK" "$WORK/debian-binary" "$WORK/control.tar.gz" "$WORK/data.tar.gz" <<'PY'
import os, sys, time
out = open(sys.argv[1], "wb")
out.write(b"!<arch>\n")
for path in sys.argv[2:]:
    data = open(path, "rb").read()
    name = os.path.basename(path)
    out.write(f"{name:<16}{int(time.time()):<12}{0:<6}{0:<6}{'100644':<8}{len(data):<10}`\n".encode())
    out.write(data + (b"\n" if len(data) % 2 else b""))
out.close()
PY
# Refuse to ship anything that looks like a credential.
if tar -tzf "$WORK/data.tar.gz" | grep -qiE 'local\.properties|\.env$|settings\.json|authKey'; then
  echo "refusing to package: credential-looking file inside the app dir" >&2; rm -f "$IPK"; exit 1
fi
echo "packaged $IPK"
