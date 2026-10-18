#!/usr/bin/env bash
# Build a webOS .ipk from a staged app directory. Uses ares-package when the
# webOS CLI is installed, otherwise assembles the ar/tar layout by hand.
#   tools/package-ipk.sh <stage-dir> <out-dir>
set -euo pipefail
STAGE="$1"; OUT="$2"
ID=$(python3 -c "import json,sys;print(json.load(open('$STAGE/appinfo.json'))['id'])")
VER=$(python3 -c "import json,sys;print(json.load(open('$STAGE/appinfo.json'))['version'])")
if command -v ares-package >/dev/null 2>&1; then
  ares-package --no-minify -o "$OUT" "$STAGE"
  exit 0
fi
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/data/usr/palm/applications/$ID"
PKGDIR="$WORK/data/usr/palm/packages/$ID"
mkdir -p "$APPDIR" "$PKGDIR" "$WORK/ctl"
cp -r "$STAGE"/. "$APPDIR/"
chmod 755 "$APPDIR/stremio-tv"
printf '{"id":"%s","version":"%s","app":"%s"}\n' "$ID" "$VER" "$ID" > "$PKGDIR/packageinfo.json"
SIZE=$(du -sk "$WORK/data" | cut -f1)
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
(cd "$WORK/data" && tar --owner=0 --group=0 -czf "$WORK/data.tar.gz" .)
IPK="$OUT/${ID}_${VER}_arm.ipk"
rm -f "$IPK"
(cd "$WORK" && ar -rc "$IPK" debian-binary control.tar.gz data.tar.gz)
# Refuse to ship anything that looks like a credential.
if tar -tzf "$WORK/data.tar.gz" | grep -qiE 'local\.properties|\.env$|settings\.json|authKey'; then
  echo "refusing to package: credential-looking file inside the app dir" >&2; rm -f "$IPK"; exit 1
fi
echo "packaged $IPK"
