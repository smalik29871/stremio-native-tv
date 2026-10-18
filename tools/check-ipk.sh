#!/usr/bin/env bash
# Sanity checks on a built webOS package before it is published:
#   - the binary is ARM and only links what every webOS TV has (SDL2, GLESv2,
#     libc family); LS2, glib, ACB and curl must stay dlopen'ed
#   - it needs no glibc symbol newer than webOS 3's (2.12)
#   - the package carries the binary, appinfo.json, icons and fonts, and no
#     settings/credential files
# Usage: bash tools/check-ipk.sh <package.ipk> <toolchain-dir>
set -euo pipefail
IPK="$1"; TC="$2"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
(cd "$W" && ar x "$OLDPWD/$IPK" 2>/dev/null || ar x "$IPK")
mkdir "$W/data" && tar xzf "$W/data.tar.gz" -C "$W/data"
APPDIR=$(ls -d "$W"/data/usr/palm/applications/*/)
BIN="$APPDIR/stremio-tv"
fail() { echo "check-ipk: $*" >&2; exit 1; }
[ -x "$BIN" ] || fail "binary missing or not executable"
"$TC/bin/arm-webos-linux-gnueabi-readelf" -h "$BIN" | grep -q "Machine:.*ARM" || fail "binary is not ARM"
NEEDED=$("$TC/bin/arm-webos-linux-gnueabi-readelf" -d "$BIN" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | sort)
echo "NEEDED: $(echo $NEEDED)"
for lib in $NEEDED; do
  case "$lib" in
    libSDL2-2.0.so.0|libGLESv2.so.2|libEGL.so.1|libm.so.6|libdl.so.2|libpthread.so.0|libc.so.6|librt.so.1) ;;
    *) fail "unexpected hard dependency $lib (load it with dlopen instead)";;
  esac
done
NEWEST=$("$TC/bin/arm-webos-linux-gnueabi-objdump" -T "$BIN" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -V | tail -1)
echo "newest glibc symbol: $NEWEST"
[ "$(printf '%s\n2.12\n' "$NEWEST" | sort -V | tail -1)" = "2.12" ] || fail "needs glibc $NEWEST, newer than webOS 3"
for f in appinfo.json icon.png largeIcon.png assets/fonts/Inter-Medium.otf assets/fonts/Inter-Bold.otf; do
  [ -f "$APPDIR/$f" ] || fail "missing $f"
done
if (cd "$W/data" && find . -iname 'settings.json' -o -iname '*.env' -o -iname 'local.properties' | grep -q .); then
  fail "package contains a settings or credential file"
fi
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); assert d['type']=='native' and d['main']=='stremio-tv', d" "$APPDIR/appinfo.json"
echo "check-ipk: OK ($(du -h "$IPK" | cut -f1))"
