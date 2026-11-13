#!/bin/bash
# Build and flash Sticky OS to a reTerminal Sticky over its USB-C port (CH343 bridge).
# The firmware is in firmware/sticky-os; the build goes to firmware/sticky-os/build.
#   ./upload.sh            build and flash
#   ./upload.sh build      build only
#   PORT=/dev/ttyUSB0 ./upload.sh    flash to that port
#   STICKY_VERSION=v0.2.0 ./upload.sh build    a release build (CI only: refused while the seed headers exist)
#   STICKY_OTA_HTTP=1 ./upload.sh    a test build that takes updates over plain http
#   STICKY_ICS_HTTP=1 ./upload.sh    a test build that takes plain http calendar links (a feed on a computer)
# The flash layout is firmware/sticky-os/partitions.csv (the whole 32 MB):
# the core copies a partitions.csv from the sketch folder over the scheme's,
# and PartitionScheme=custom with upload.maximum_size checks against the 6 MB app slot.
set -euo pipefail
cd "$(dirname "$0")/firmware/sticky-os"
FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=32M,PartitionScheme=custom
APP_MAX=$((0x600000))  # app0 / app1 in partitions.csv
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "$COMMIT" != unknown ] && [ -n "$(git status --porcelain -- . 2>/dev/null)" ]; then COMMIT="$COMMIT-dirty"; fi
VERSION=${STICKY_VERSION:-dev-$COMMIT}
# Passed as a bare token and turned into a string in the source: keep it plain.
[[ "$VERSION" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "STICKY_VERSION: letters, digits, . _ - only" >&2; exit 1; }
# Release builds (v*) are made by CI only: a local one would carry the
# git-ignored seed headers' WiFi passwords and keys inside the image.
if [[ "$VERSION" == v* ]] && { [ -e secrets.h ] || [ -L secrets.h ] || [ -e private_seed.h ]; }; then
  echo "STICKY_VERSION=$VERSION: release builds come from CI; move secrets.h / private_seed.h away to build one here" >&2
  exit 1
fi
FLAGS="-DSTICKY_COMMIT=$COMMIT -DSTICKY_VERSION=$VERSION"
# STICKY_OTA_HTTP=1: updates may come over plain http (a test feed on a computer). Never in a release.
if [ "${STICKY_OTA_HTTP:-}" = 1 ]; then
  [[ "$VERSION" == v* ]] && { echo "STICKY_OTA_HTTP is for test builds, not $VERSION" >&2; exit 1; }
  FLAGS="$FLAGS -DSTICKY_OTA_ALLOW_HTTP"
fi
# STICKY_ICS_HTTP=1: calendar links may be plain http (a test feed on a computer). Never in a release.
if [ "${STICKY_ICS_HTTP:-}" = 1 ]; then
  [[ "$VERSION" == v* ]] && { echo "STICKY_ICS_HTTP is for test builds, not $VERSION" >&2; exit 1; }
  FLAGS="$FLAGS -DSTICKY_ICS_ALLOW_HTTP"
fi
arduino-cli compile --fqbn "$FQBN" --build-path build \
  --build-property "compiler.cpp.extra_flags=$FLAGS" \
  --build-property "upload.maximum_size=$APP_MAX" .
[ "${1:-}" = build ] && exit 0
# The first USB serial port there is (macOS, then Linux), unless PORT says.
PORT=${PORT:-$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)}
[ -n "$PORT" ] || { echo "no board found; set PORT=/dev/..." >&2; exit 1; }
arduino-cli upload --fqbn "$FQBN" --build-path build -p "$PORT" .
