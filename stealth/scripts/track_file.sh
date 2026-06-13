#!/usr/bin/env bash
# Register an upstream file as "owned" BEFORE you edit it.
# Usage: stealth/scripts/track_file.sh <repo-relative-path> [more paths...]
#
# Adds each path to stealth/OWNED_FILES (deduped). After editing, run
# stealth/scripts/refresh_patches.py to capture the diff.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEALTH_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$STEALTH_DIR")"
OWNED="$STEALTH_DIR/OWNED_FILES"

if [ "$#" -eq 0 ]; then
  echo "usage: track_file.sh <repo-relative-path> [more...]" >&2
  exit 1
fi

for path in "$@"; do
  if [ ! -e "$REPO_ROOT/$path" ]; then
    echo "warn: $path does not exist under repo root — adding anyway" >&2
  fi
  if grep -qxF "$path" "$OWNED" 2>/dev/null; then
    echo "already tracked: $path"
  else
    echo "$path" >> "$OWNED"
    echo "tracked: $path"
  fi
done

echo "Edit the file(s), keep the change minimal with a // [stealth] marker,"
echo "then run: stealth/scripts/refresh_patches.py"
