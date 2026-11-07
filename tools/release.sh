#!/bin/sh
# Publishes the current T-Embed build as a GitHub release the board's Update screen can download.
#   tools/release.sh        (version = FW_VERSION from src/config.h; commit and push first)
set -e
cd "$(dirname "$0")/.."
VER=$(sed -n 's/#define FW_VERSION "\(.*\)"/\1/p' src/config.h)
REPO=$(sed -n 's/#define UPDATE_REPO "\(.*\)"/\1/p' src/config.h)
pio run -e tembed_xinput
OUT=dist/release; rm -rf "$OUT"; mkdir -p "$OUT"
cp .pio/build/tembed_xinput/firmware.bin "$OUT/XboxBridge.XInput.bin"
SIZE=$(wc -c < "$OUT/XboxBridge.XInput.bin" | tr -d ' ')
SHA=$(shasum -a 256 "$OUT/XboxBridge.XInput.bin" | cut -d' ' -f1)
printf '{"version":"%s","file":"XboxBridge.XInput.bin","size":%s,"sha256":"%s"}\n' "$VER" "$SIZE" "$SHA" > "$OUT/latest.json"
cat "$OUT/latest.json"
gh release create "v$VER" "$OUT/XboxBridge.XInput.bin" "$OUT/latest.json" --repo "$REPO" --title "v$VER" --notes "Xbox Bridge $VER. On the board: Update from GitHub."
