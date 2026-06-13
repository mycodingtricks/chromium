#!/usr/bin/env python3
"""CI guard: verify patches/ exactly matches the current working tree.

Regenerates each owned file's diff in-memory and compares to the committed
patch. Exit non-zero on any drift, so an upstream edit can't land without its
patch being refreshed. Run in pre-commit / CI.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _lib import (  # noqa: E402
    PATCHES_DIR, base_commit, git, owned_files, patch_name,
)


def main():
    base = base_commit()
    drift = []
    for path in owned_files():
        current = git("diff", base, "--", path, check=False).stdout
        name = patch_name(path)
        committed_path = os.path.join(PATCHES_DIR, name)
        committed = ""
        if os.path.exists(committed_path):
            with open(committed_path) as f:
                committed = f.read()
        if current.strip() != committed.strip():
            drift.append(path)

    if drift:
        print("patch drift detected — run stealth/scripts/refresh_patches.py:")
        for p in drift:
            print(f"  - {p}")
        sys.exit(1)
    print("ok: patches/ matches working tree")


if __name__ == "__main__":
    main()
