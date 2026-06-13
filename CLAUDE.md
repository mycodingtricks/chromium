# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A **fork of Chromium (base v151.0.7892.0)** that we are turning into a stealth /
anti-detect browser (think GoLogin Orbita, Multilogin Mimic): one binary that
presents a different, internally-consistent fingerprint per launch, driven by a
profile config.

Everything we add lives under **`stealth/`**. The other ~70 top-level dirs
(`chrome/`, `content/`, `third_party/blink/`, `v8/`, …) are **upstream Chromium**
— treat them as vendored third-party code.

## The one rule that governs everything: stay rebaseable

Chromium ships a new version every ~4 weeks. Our work is worthless if we can't
carry it forward cheaply. So:

1. **Net-new code goes only in `stealth/`.** New `.cc/.h/.json/.py` files never
   conflict on rebase. Put all real logic there.
2. **Editing an upstream file is a last resort, and is never silent.** Before
   touching anything outside `stealth/`:
   - run `stealth/scripts/track_file.sh <path>` to register it,
   - make the **smallest possible** edit — ideally a 2-3 line `// [stealth]`
     hook that calls into `stealth/src/`,
   - run `stealth/scripts/refresh_patches.py` to record the diff.
   Every upstream edit must end up as a patch in `stealth/patches/`. If you
   edited an upstream file and didn't refresh patches, the change is not done.

`git grep "\[stealth\]"` lists every upstream hook we carry. Keep that list small.

**Start every stealth task by reading `stealth/README.md`** and the relevant doc
in `stealth/docs/`.

## Where things are

| You want to… | Go to |
|---|---|
| Understand config flow (browser → renderer → Blink) | `stealth/docs/architecture.md` |
| Find the patch point for a JS surface (canvas, WebGL, navigator…) | `stealth/docs/fingerprint-surfaces.md` |
| Bump the Chromium version | `stealth/docs/upgrade-guide.md` |
| Add override logic | `stealth/src/fingerprint/` (the "brains") |
| See/define the profile schema | `stealth/config/sample_profile.json` |
| Patch tooling | `stealth/scripts/` |

## Skills (prefer these over ad-hoc work)

- **`stealth-patch`** — safely edit an upstream file (track → edit → refresh).
- **`stealth-fingerprint`** — add/modify a fingerprint override end-to-end.
- **`stealth-upgrade`** — rebase the layer onto a new Chromium version.
- **`stealth-build`** — configure, build, and fingerprint-test the browser.

## Build & run

depot_tools (`gn`, `autoninja`) is **not** vendored — install it and put it on
PATH first. This is a macOS (arm64/x64) checkout; there is no `out/` yet.

```sh
stealth/scripts/build.sh gen      # generate out/Stealth from stealth/config/args.gn
stealth/scripts/build.sh          # incremental build of `chrome`
# run with a fingerprint profile:
out/Stealth/Chromium.app/Contents/MacOS/Chromium \
  --stealth-profile=stealth/config/sample_profile.json \
  --user-data-dir=/tmp/stealth-1
```

A clean first build of Chromium takes a long time (tens of minutes to hours) and
needs lots of disk; incremental builds after a small edit are minutes. Build a
narrow target (e.g. `blink_tests`, or a specific unit-test binary) when iterating
rather than all of `chrome`.

## Architecture essentials (read before patching Blink)

- **Renderers are sandboxed** — they cannot read the profile file. The browser
  process reads `--stealth-profile` once, then forwards values to every child
  process via command-line switches (scalars) or Mojo (structured data). The
  forwarding hook lives in `chrome/browser/chrome_content_browser_client.cc`
  (`AppendExtraCommandLineSwitches`). Site isolation means *many* renderers — all
  must receive it.
- **The hook pattern** (every upstream touch looks like this):
  ```cpp
  String NavigatorID::platform() const {
    if (auto v = stealth::FingerprintConfig::Get().Platform())
      return String::FromUTF8(*v);   // [stealth]
    // ... original upstream body unchanged ...
  }
  ```
- **Prefer cheaper mechanisms in order:** existing Chromium flag → new flag read
  by `stealth/src/` via a tiny hook → larger upstream patch (avoid). Timezone via
  `TZ` env var, WebRTC IP leak via `--force-webrtc-ip-handling-policy`, window
  size via `--window-size` — flags, not patches.
- **Consistency over spoofing:** detectors flag *inconsistency*. Derive every
  surface from the single profile JSON; never let two surfaces compute a value
  independently. See the consistency map in `fingerprint-surfaces.md`.
- Canvas/audio/WebGL are handled by **seeded noise** (stable per-profile, differs
  across profiles), not static spoofing.

## Conventions

- C++ follows Chromium style (`.clang-format` at root; `git cl format` if
  depot_tools is present). Our files use the same style.
- Mark every upstream edit with a `// [stealth]` comment so it's greppable.
- Don't reformat or "clean up" upstream files — it inflates the patch and the
  rebase cost. Touch only the lines you must.
- Patch scripts are stdlib-only Python 3; run them with system `python3`.
