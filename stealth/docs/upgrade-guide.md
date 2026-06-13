# Upgrade guide: bumping the Chromium base

Goal: move from base version *A* to *B* while carrying our stealth layer with the
least pain. Budget a few hours; most of it is resolving a handful of patch
conflicts in Blink getters.

## Before you start

- Read `UPSTREAM_BASE` — that's your current *A*.
- Run `stealth/scripts/check_patches.py` to confirm `patches/` matches the working
  tree (no uncommitted upstream drift). Commit anything outstanding.
- Make sure the layer **builds and runs** on *A* first. Never start an upgrade
  from a broken tree.

## The two valid strategies

### Strategy 1 — git rebase (preferred for this fork)

This repo is a git fork of `origin/main` (Chromium). Day-to-day, our changes are
just commits on top of upstream.

```sh
# 1. Fetch the new upstream and find the target tag/commit B.
git fetch origin --tags

# 2. Create a working branch and rebase our commits onto B.
git checkout -b upgrade/<B> <our-stealth-tip>
git rebase <B-commit>

# 3. Resolve conflicts. They cluster in the Blink getters we hook.
#    For each: re-apply the `// [stealth]` guard around the NEW upstream body.
#    git grep "\[stealth\]" shows every hook that must survive.
```

### Strategy 2 — patch re-apply (for a fresh/pristine tree)

Use when the base tree is replaced wholesale (e.g. a clean `fetch chromium` /
`gclient sync` checkout) rather than git-rebased.

```sh
# On a PRISTINE upstream-B checkout that also contains stealth/:
python3 stealth/scripts/apply_patches.py
# It applies patches/series in order and reports any that fail to apply,
# along with the surrounding context so you can place the hook by hand.
```

## Resolving a conflicted hook

Our hooks are deliberately tiny (2-3 lines). When upstream rewrites the function
body around one, the fix is mechanical:

1. Find it: `git grep "\[stealth\]"` or look at the failed patch's target.
2. Re-insert the guard at the top of the **new** function body:
   ```cpp
   if (auto v = stealth::FingerprintConfig::Get().platform()) return *v;
   ```
3. Confirm the signature/return type still matches (e.g. `String` vs `AtomicString`).
4. Move on. The brains in `stealth/src/` rarely need changes — only the hook site.

## After conflicts resolve

```sh
# 1. Regenerate patches from the now-correct working tree.
python3 stealth/scripts/refresh_patches.py

# 2. Update the base marker.
#    Edit stealth/UPSTREAM_BASE: set VERSION + COMMIT to B.

# 3. Re-check every surface still compiles AND still overrides.
stealth/scripts/build.sh

# 4. Sanity-test the fingerprint with a sample profile against a detector
#    (e.g. browserleaks.com, creepjs, pixelscan) — see stealth-build skill.
```

## What commonly breaks on a bump

- **Getter signature changes** (return type, const-ness) — adjust the hook.
- **A surface moves files** (Blink refactors split `navigator.cc` often) — re-grep
  the symbol; update `OWNED_FILES` and `fingerprint-surfaces.md`.
- **A flag is renamed/removed** — check `*_switches.cc` for the new name.
- **`runtime_enabled_features.json5`** reorders — if we toggle a feature there,
  re-find our entry.
- **New fingerprint surfaces appear** (e.g. WebGPU adapter info matured) — add to
  the inventory; not strictly an upgrade blocker.

## Checklist (paste into the upgrade PR)

- [ ] `check_patches.py` clean before starting
- [ ] Rebased / re-applied onto B
- [ ] Every `[stealth]` hook re-verified to override (not just compile)
- [ ] `refresh_patches.py` run, `patches/` committed
- [ ] `UPSTREAM_BASE` updated to B
- [ ] Built successfully
- [ ] Fingerprint smoke-tested against a detector with a sample profile
- [ ] `fingerprint-surfaces.md` updated for any moved/added surfaces
