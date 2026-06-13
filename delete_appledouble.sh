#!/usr/bin/env bash
set -euo pipefail

PROGRESS_EVERY="${PROGRESS_EVERY:-500}"

FIFO="$(mktemp -u)"
FOUND_COUNT_FILE="$(mktemp)"
DELETED_COUNT_FILE="$(mktemp)"

mkfifo "$FIFO"

echo 0 > "$FOUND_COUNT_FILE"
echo 0 > "$DELETED_COUNT_FILE"

cleanup() {
  rm -f "$FIFO" "$FOUND_COUNT_FILE" "$DELETED_COUNT_FILE"
}
trap cleanup EXIT INT TERM

echo "Scanning only current directory and child directories: $(pwd)"
echo "Deleting files matching: ._*"
echo "Progress update every $PROGRESS_EVERY deletions."
echo

# Child 1: find files only under current directory
(
  count=0

  find . -type f -name '._*' -print0 |
  while IFS= read -r -d '' file; do
    count=$((count + 1))
    echo "$count" > "$FOUND_COUNT_FILE"
    printf '%s\0' "$file" > "$FIFO"
  done

  echo "$count" > "$FOUND_COUNT_FILE"
) &

FINDER_PID=$!

# Child 2: delete files received from finder
(
  deleted=0

  while IFS= read -r -d '' file; do
    rm -f -- "$file"
    deleted=$((deleted + 1))
    echo "$deleted" > "$DELETED_COUNT_FILE"

    if (( deleted % PROGRESS_EVERY == 0 )); then
      found="$(cat "$FOUND_COUNT_FILE")"
      printf '\rFound: %s | Deleted: %s' "$found" "$deleted"
    fi
  done < "$FIFO"

  found="$(cat "$FOUND_COUNT_FILE")"
  printf '\rFound: %s | Deleted: %s\n' "$found" "$deleted"
) &

DELETER_PID=$!

wait "$FINDER_PID"
wait "$DELETER_PID"

echo
echo "Done."
echo "Total found:   $(cat "$FOUND_COUNT_FILE")"
echo "Total deleted: $(cat "$DELETED_COUNT_FILE")"
