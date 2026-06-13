# patches/

Auto-generated unified diffs of every upstream file we edit (everything outside
`stealth/`). **Do not hand-edit these** — edit the source file, then run
`stealth/scripts/refresh_patches.py`.

- One `.patch` per owned upstream file, named `path__to__file.ext.patch`.
- `series` lists them in apply order (also auto-generated).
- Each patch is `git diff <UPSTREAM_BASE commit>..worktree -- <file>`, so it
  captures the *cumulative* stealth changes to that file.

These exist so a version bump is auditable and mechanical: `apply_patches.py`
re-grafts them onto a fresh tree, and the ones that conflict are the only work.
See `../docs/upgrade-guide.md`.
