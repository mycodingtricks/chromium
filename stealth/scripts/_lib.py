"""Shared helpers for the stealth patch tooling.

All paths are resolved relative to the repo root (the parent of stealth/).
No third-party deps — stdlib only, runs under the vpython3 / system python3.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEALTH_DIR = os.path.dirname(HERE)
REPO_ROOT = os.path.dirname(STEALTH_DIR)
PATCHES_DIR = os.path.join(STEALTH_DIR, "patches")
SERIES_FILE = os.path.join(PATCHES_DIR, "series")
OWNED_FILES = os.path.join(STEALTH_DIR, "OWNED_FILES")
UPSTREAM_BASE = os.path.join(STEALTH_DIR, "UPSTREAM_BASE")

PATCH_SUFFIX = ".patch"


def base_commit():
    """Read COMMIT=... from stealth/UPSTREAM_BASE."""
    with open(UPSTREAM_BASE) as f:
        for line in f:
            line = line.strip()
            if line.startswith("COMMIT="):
                return line.split("=", 1)[1].strip()
    sys.exit("error: COMMIT= not found in stealth/UPSTREAM_BASE")


def owned_files():
    """Return the list of upstream files we patch (skips comments/blanks)."""
    out = []
    if not os.path.exists(OWNED_FILES):
        return out
    with open(OWNED_FILES) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            out.append(line)
    return out


def patch_name(repo_rel_path):
    """Map an upstream path to its patch filename: a/b/c.cc -> a__b__c.cc.patch"""
    return repo_rel_path.replace("/", "__") + PATCH_SUFFIX


def git(*args, check=True):
    return subprocess.run(
        ["git", *args], cwd=REPO_ROOT, check=check,
        capture_output=True, text=True,
    )
