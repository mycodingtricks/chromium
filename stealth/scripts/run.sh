#!/usr/bin/env bash
#
# run.sh — autonomous CreepJS-evasion implementation loop.
#
# Each iteration launches Claude Code in headless mode (`claude -p`) on a FRESH
# context, tells it to complete EXACTLY ONE unchecked item from the progress
# checklist, then exits. The next iteration starts clean. This keeps per-run
# context small while `stealth/docs/creepjs-progress.md` carries state between
# runs — it is the shared memory.
#
# The loop stops when no "- [ ]" items remain in the progress file (all goals
# done), when MAX_ITERS is reached, or when it stalls (N runs with no progress).
#
#   Usage:   stealth/scripts/run.sh
#
#   After each iteration that changes files, the loop commits to a single
#   per-run branch and pushes it. The first iteration opens ONE pull request;
#   every iteration then posts a detailed PR comment (the run's final summary
#   plus the changed-file list). Requires `gh` authenticated (`gh auth login`)
#   and an `origin` remote — if either is missing the loop still commits, just
#   skips the PR step with a warning.
#
#   Env knobs:
#     CLAUDE_MODEL   model alias/id            (default: opus)
#     MAX_ITERS      hard cap on iterations    (default: 200)
#     MAX_STALL      abort after N no-progress runs (default: 3)
#     PERMISSION     permission flag passed to claude
#                    (default: --dangerously-skip-permissions, required for
#                     unattended runs; override to e.g. "--permission-mode acceptEdits")
#     ENABLE_GIT     1 to commit/push/PR per iteration, 0 to disable (default: 1)
#     WORK_BRANCH    branch to commit the run onto
#                    (default: stealth/creepjs-run-<timestamp>)
#     BASE_BRANCH    PR base branch            (default: current branch)
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STEALTH_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd "$STEALTH_DIR/.." && pwd)"

PROGRESS="$STEALTH_DIR/docs/creepjs-progress.md"
SYSTEM_PROMPT="$STEALTH_DIR/docs/creepjs-system-prompt.md"

CLAUDE_MODEL="${CLAUDE_MODEL:-claude-opus-4-8}"
MAX_ITERS="${MAX_ITERS:-200}"
MAX_STALL="${MAX_STALL:-3}"
PERMISSION="${PERMISSION:---dangerously-skip-permissions}"
ENABLE_GIT="${ENABLE_GIT:-1}"

RUN_TS="$(date +%Y%m%d-%H%M%S)"
WORK_BRANCH="${WORK_BRANCH:-stealth/creepjs-run-$RUN_TS}"

LOG_DIR="$STEALTH_DIR/.run-logs"
mkdir -p "$LOG_DIR"

# --- preflight ---------------------------------------------------------------
command -v claude >/dev/null 2>&1 || { echo "error: 'claude' CLI not on PATH" >&2; exit 1; }
[ -f "$PROGRESS" ]      || { echo "error: progress file not found: $PROGRESS" >&2; exit 1; }
[ -f "$SYSTEM_PROMPT" ] || { echo "error: system prompt not found: $SYSTEM_PROMPT" >&2; exit 1; }

# Count remaining unchecked checklist items ("- [ ]").
count_remaining() { grep -cE '^[[:space:]]*- \[ \]' "$PROGRESS" 2>/dev/null || true; }

# The per-iteration instruction. The system prompt (appended separately) carries
# all the rules; this just scopes the run to a single item and enforces "stop".
read -r -d '' ITER_PROMPT <<'EOF'
You are running inside an automated loop. Do EXACTLY ONE unit of work, then stop.

1. Read stealth/docs/creepjs-progress.md and the docs it references.
2. Select the FIRST unchecked item ("- [ ]") in top-to-bottom priority order
   whose status is not already DONE/BLOCKED. (P0 args-ingestion and worker-scope
   parity come before everything else.)
3. Implement that single item END TO END, following the system prompt rules:
   spoof in C++/Blink via // [stealth] hooks (never monkeypatch JS), read values
   from the orchestrator-supplied args via stealth::FingerprintConfig, derive
   nothing independently, forward to every child process. Use the stealth-patch /
   stealth-fingerprint / stealth-build skills. Build and verify against CreepJS
   when the item is testable; confirm window<->worker parity for values touched.
4. Update stealth/docs/creepjs-progress.md for THIS ITEM ONLY:
   - change its "- [ ]" to "- [x]";
   - replace its TODO/WIP status with DONE (or FLAG/BLOCKED) and append the
     required metadata: CreepJS surface | upstream patch point (symbol, not line)
     | stealth/src file | mechanism (flag/hook/noise) | last observed CreepJS
     result;
   - if CreepJS surfaced a new lie/inconsistency, add it as a new "- [ ]" TODO;
   - add a dated entry to the change log at the bottom (newest first).
   If you genuinely cannot finish (missing dependency, needs a human decision),
   set the item to BLOCKED with a one-line reason and STILL flip "- [ ]" to
   "- [x]" so the loop does not retry it forever.
5. Do NOT start a second item. Stop after one is recorded.
EOF

echo "Loop start: $(count_remaining) item(s) remaining · model=$CLAUDE_MODEL · repo=$REPO_DIR"
echo "Logs: $LOG_DIR"

cd "$REPO_DIR"

# --- git / PR setup ----------------------------------------------------------
# All commits for this run land on a single branch; one PR is opened on the
# first committed iteration and each iteration adds a detailed comment.
PR_NUMBER=""
BASE_BRANCH="${BASE_BRANCH:-$(git rev-parse --abbrev-ref HEAD)}"
# Never use an ephemeral per-run branch (a leftover checkout from a previous
# loop) as the PR base — those aren't guaranteed to exist on origin, so
# `gh pr create` fails with "Base sha can't be blank". Fall back to the trunk.
case "$BASE_BRANCH" in
  "$WORK_BRANCH" | stealth/creepjs-run-* | HEAD)
    BASE_BRANCH="$(git symbolic-ref --short refs/remotes/origin/HEAD 2>/dev/null | sed 's#^origin/##')"
    BASE_BRANCH="${BASE_BRANCH:-main}"
    echo "ℹ️  HEAD is a run branch; using '$BASE_BRANCH' as PR base instead." >&2
    ;;
esac

if [ "$ENABLE_GIT" = "1" ]; then
  if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "⚠️  not a git work tree — disabling commit/PR for this run." >&2
    ENABLE_GIT=0
  else
    # Create (or switch to) the per-run branch. Existing working-tree changes
    # carry over to the new branch.
    if [ "$(git rev-parse --abbrev-ref HEAD)" != "$WORK_BRANCH" ]; then
      git switch -c "$WORK_BRANCH" 2>/dev/null || git switch "$WORK_BRANCH" || {
        echo "⚠️  could not create/switch to branch $WORK_BRANCH — disabling commit/PR." >&2
        ENABLE_GIT=0
      }
    fi
    [ "$ENABLE_GIT" = "1" ] && echo "Git: committing to branch '$WORK_BRANCH' (PR base '$BASE_BRANCH')"
  fi
fi

# publish_iteration <iter> <log-file> <summary-file>
# Commits any changes, pushes, opens the PR (once) and posts a detailed comment.
publish_iteration() {
  local iter="$1" log="$2" summary_file="$3"
  [ "$ENABLE_GIT" = "1" ] || return 0

  if [ -z "$(git status --porcelain)" ]; then
    echo "ℹ️  iteration $iter changed no files — nothing to commit." >&2
    return 0
  fi

  local summary=""
  [ -s "$summary_file" ] && summary="$(cat "$summary_file")"

  git add -A
  git commit -q \
    -m "stealth(creepjs): automated iteration $iter" \
    -m "${summary:-Automated CreepJS-evasion iteration $iter.}" \
    -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>" || {
      echo "⚠️  git commit failed (iteration $iter)." >&2; return 0; }

  if ! git push -u origin "$WORK_BRANCH" >/dev/null 2>&1; then
    echo "⚠️  git push failed (iteration $iter) — committed locally, skipping PR." >&2
    return 0
  fi

  command -v gh >/dev/null 2>&1 || {
    echo "ℹ️  gh not installed — committed & pushed, no PR for iteration $iter." >&2; return 0; }

  # Detailed comment body: the run's own summary + the files it touched.
  local changed body
  changed="$(git show --stat --format= HEAD)"
  body="$(printf '## Iteration %s\n\n%s\n\n<details><summary>Changed files</summary>\n\n```\n%s\n```\n</details>\n\nCommit: %s · log: `%s`\n' \
    "$iter" "${summary:-_(no summary captured)_}" "$changed" "$(git rev-parse --short HEAD)" "$log")"

  if [ -z "$PR_NUMBER" ]; then
    # The base ref must exist on origin or GitHub rejects the PR ("Base sha
    # can't be blank"). Push it if the remote doesn't have it yet.
    if ! git ls-remote --exit-code --heads origin "$BASE_BRANCH" >/dev/null 2>&1; then
      echo "ℹ️  base '$BASE_BRANCH' missing on origin — pushing it." >&2
      git push origin "$BASE_BRANCH" >/dev/null 2>&1 \
        || echo "⚠️  could not push base branch '$BASE_BRANCH' to origin." >&2
    fi
    local url
    if url="$(gh pr create --base "$BASE_BRANCH" --head "$WORK_BRANCH" \
        --title "stealth: CreepJS-evasion automated run ($RUN_TS)" \
        --body "Automated CreepJS-evasion loop (run $RUN_TS, model $CLAUDE_MODEL). Each iteration completes one item from stealth/docs/creepjs-progress.md; per-iteration detail is posted as comments below." \
        2>&1)"; then
      PR_NUMBER="$(printf '%s' "$url" | grep -oE '[0-9]+' | tail -1)"
      echo "🔗 opened PR #$PR_NUMBER: $url"
    else
      echo "⚠️  gh pr create failed: $url" >&2
      return 0
    fi
  fi

  if gh pr comment "$PR_NUMBER" --body "$body" >/dev/null 2>&1; then
    echo "💬 commented on PR #$PR_NUMBER (iteration $iter)"
  else
    echo "⚠️  gh pr comment failed (iteration $iter)." >&2
  fi
}

iter=0
prev_remaining=-1
stall=0

while :; do
  remaining="$(count_remaining)"

  if [ "${remaining:-0}" -eq 0 ]; then
    echo "✅ All progress items complete. Nothing left to do."
    break
  fi

  if [ "$remaining" -eq "$prev_remaining" ]; then
    stall=$((stall + 1))
    if [ "$stall" -ge "$MAX_STALL" ]; then
      echo "⚠️  No progress in $stall consecutive runs ($remaining left). Aborting — inspect the latest log." >&2
      break
    fi
  else
    stall=0
  fi
  prev_remaining="$remaining"

  iter=$((iter + 1))
  if [ "$iter" -gt "$MAX_ITERS" ]; then
    echo "Reached MAX_ITERS=$MAX_ITERS ($remaining still left). Stopping." >&2
    break
  fi

  ts="$(date +%Y%m%d-%H%M%S)"
  log="$LOG_DIR/iter-$(printf '%03d' "$iter")-$ts.log"
  summary="$LOG_DIR/iter-$(printf '%03d' "$iter")-$ts.summary.txt"
  echo "── iteration $iter · $remaining item(s) remaining · log: $log ──"

  # Fresh process => fresh context every iteration. System prompt appended from
  # file; the progress markdown is the only state carried across runs.
  #
  # We ask Claude for the stream-json transcript so we can show ONE concise line
  # per step (tool call / message) on the console via stream_oneline.py, while
  # the full raw JSON transcript is still saved to "$log" for later inspection.
  # STREAM_SUMMARY_FILE captures Claude's final message for the PR comment.
  claude -p "$ITER_PROMPT" \
    --append-system-prompt "$(cat "$SYSTEM_PROMPT")" \
    --model "$CLAUDE_MODEL" \
    --output-format stream-json --verbose \
    $PERMISSION \
    2>&1 | tee "$log" | STREAM_SUMMARY_FILE="$summary" python3 "$SCRIPT_DIR/stream_oneline.py"

  status=${PIPESTATUS[0]}
  if [ "$status" -ne 0 ]; then
    echo "⚠️  claude exited with status $status on iteration $iter (see $log). Continuing." >&2
  fi

  # Commit this iteration's work, open the PR (once) and post a detailed comment.
  publish_iteration "$iter" "$log" "$summary"
done

echo "Loop finished after $iter iteration(s). $(count_remaining) item(s) remaining."
