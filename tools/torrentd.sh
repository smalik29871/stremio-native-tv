#!/usr/bin/env bash
# Builds the P2P engine (tools/torrentd, Go, no cgo: one static file).
#   tools/torrentd.sh host  <output>   desktop (tests, smoke runs)
#   tools/torrentd.sh webos <output>   ARMv7 for the TV
set -euo pipefail
OUT="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
cd "$(dirname "$0")/torrentd"
export CGO_ENABLED=0
case "$1" in
  host) ;;
  webos) export GOOS=linux GOARCH=arm GOARM=7 ;;
  *) echo "usage: $0 host|webos <output>" >&2; exit 1 ;;
esac
go build -trimpath -ldflags "-s -w" -o "$OUT" .
echo "torrentd: built for $1 ($(du -h "$OUT" | cut -f1))"
