#!/usr/bin/env python3
"""Apply stealth/patches/ onto the current tree (in patches/series order).

Use on a PRISTINE upstream checkout (that already contains stealth/) to graft
our upstream edits back on — e.g. after a clean `fetch chromium` / `gclient
sync`. On a normal git fork you rebase instead; this is the portability path.

Reports each patch that fails to apply so you can place the hook by hand.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _lib import PATCHES_DIR, SERIES_FILE, git  # noqa: E402


def series():
    if not os.path.exists(SERIES_FILE):
        sys.exit("error: patches/series not found — run refresh_patches.py first")
    out = []
    with open(SERIES_FILE) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    return out


def main():
    check_only = "--check" in sys.argv
    failed = []
    for name in series():
        path = os.path.join(PATCHES_DIR, name)
        args = ["apply", "--check" if check_only else "--index", path]
        res = git(*args, check=False)
        if res.returncode == 0:
            print(f"  {'ok (would apply)' if check_only else 'applied'}: {name}")
        else:
            failed.append(name)
            print(f"  FAILED: {name}\n    {res.stderr.strip()}")

    if failed:
        print(f"\n{len(failed)} patch(es) failed — resolve by hand, then run "
              f"refresh_patches.py:")
        for n in failed:
            print(f"  - {n}")
        sys.exit(1)
    print("done: all patches applied cleanly")


if __name__ == "__main__":
    main()
