# Stealth Browser Layer

This directory contains **all** of our custom code and tooling for building a
stealth / anti-detect browser (à la GoLogin Orbita, Multilogin Mimic) on top of
upstream Chromium. **Version 151.0.7892.0** is the current base.

## Prime directive: stay rebaseable

Chromium ships a new stable every ~4 weeks. The single most important goal of
this layer is that **upgrading the Chromium base must be cheap**. We achieve that
with two rules:

1. **New code lives only under `stealth/`.** Net-new `.cc`/`.h`/`.json`/`.py`
   files never conflict on rebase. Put as much logic here as physically possible.

2. **Upstream edits are minimal hooks, and every one is tracked as a patch.**
   When you *must* touch a file outside `stealth/` (e.g. to add a one-line call
   into our code), keep the edit as small as possible and immediately record it
   with `scripts/refresh_patches.py`. On upgrade we re-apply the patch set and
   only fix the handful that conflict.

Think of it as: **upstream files get hooks, `stealth/` gets the brains.**

## Layout

```
stealth/
├── README.md                 ← you are here
├── UPSTREAM_BASE             ← chromium commit/version this layer is based on
├── OWNED_FILES               ← list of upstream files we patch (one per line)
├── config/                   ← sample fingerprint profiles (JSON)
├── docs/
│   ├── architecture.md       ← how a profile flows browser → renderer → Blink
│   ├── fingerprint-surfaces.md ← inventory of every detectable surface + patch point
│   └── upgrade-guide.md      ← step-by-step Chromium version bump
├── patches/                  ← generated unified diffs of every upstream edit
│   └── series                ← ordered apply list (auto-managed)
├── scripts/
│   ├── track_file.sh         ← register an upstream file BEFORE editing it
│   ├── refresh_patches.py    ← regenerate patches/ from the working tree
│   ├── apply_patches.py      ← apply patches/ onto a pristine upstream tree
│   ├── check_patches.py      ← CI check: patches match working tree
│   └── build.sh              ← configure + build the browser
└── src/                      ← our compiled C++ (the "brains")
    ├── BUILD.gn
    └── fingerprint/          ← fingerprint config parsing + override providers
```

## Workflows (the short version)

- **Editing an upstream file:** `stealth/scripts/track_file.sh <path>` first, make
  the minimal edit, then `stealth/scripts/refresh_patches.py`. See the
  `stealth-patch` skill.
- **Adding a fingerprint override:** find the surface in
  `docs/fingerprint-surfaces.md`, put the logic in `src/fingerprint/`, add a hook
  upstream. See the `stealth-fingerprint` skill.
- **Bumping Chromium:** follow `docs/upgrade-guide.md`. See the `stealth-upgrade`
  skill.
- **Building:** `stealth/scripts/build.sh`. See the `stealth-build` skill.

## Why patches *and* git?

This checkout is a git fork, so day-to-day you just commit. The `patches/`
directory is the durable, human-reviewable record of exactly which upstream lines
we changed and why — it is what makes a version bump a mechanical, auditable
process instead of an archaeology dig through a giant merge diff. Treat
`patches/` as the source of truth for "what we changed in upstream."
