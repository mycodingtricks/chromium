#!/usr/bin/env bash
# Configure + build the stealth browser.
# Requires depot_tools (gn, autoninja) on PATH. depot_tools is NOT vendored in
# this repo — install it and `export PATH="/path/to/depot_tools:$PATH"` first.
#
# Usage:
#   stealth/scripts/build.sh            # incremental build of chrome
#   stealth/scripts/build.sh gen        # (re)generate the out dir from args
#   stealth/scripts/build.sh <target>   # build a specific ninja target
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEALTH_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$STEALTH_DIR")"
OUT_DIR="${STEALTH_OUT:-out/Stealth}"
TARGET="${1:-chrome}"

cd "$REPO_ROOT"

if ! command -v gn >/dev/null 2>&1; then
  echo "error: 'gn' not found. Put depot_tools on PATH:" >&2
  echo "  export PATH=\"/path/to/depot_tools:\$PATH\"" >&2
  exit 1
fi

# (Re)generate the build dir if missing or explicitly requested.
if [ "$TARGET" = "gen" ] || [ ! -d "$OUT_DIR" ]; then
  echo ">> gn gen $OUT_DIR"
  mkdir -p "$OUT_DIR"
  cp "$STEALTH_DIR/config/args.gn" "$OUT_DIR/args.gn"
  gn gen "$OUT_DIR"
  [ "$TARGET" = "gen" ] && exit 0
fi

echo ">> autoninja -C $OUT_DIR $TARGET"
autoninja -C "$OUT_DIR" "$TARGET"

echo ""
echo "Built. Run with a profile:"
echo "  $OUT_DIR/Chromium.app/Contents/MacOS/Chromium \\"
echo "    --stealth-profile=$STEALTH_DIR/config/sample_profile.json \\"
echo "    --user-data-dir=/tmp/stealth-profile-1"
