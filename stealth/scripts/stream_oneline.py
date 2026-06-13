#!/usr/bin/env python3
"""stream_oneline.py — collapse `claude -p --output-format stream-json` into
one concise line per step.

Reads newline-delimited JSON events from stdin (the stream-json transcript)
and prints a single human-readable line per meaningful event, so you can watch
exactly what the loop's Claude run is doing without the full firehose. The raw
JSON is preserved separately (run.sh tees it to the per-iteration log file).

stdlib only; run with system python3.
"""
import json
import os
import sys

MAX = 160  # truncate each line to keep it to one terminal row-ish

# If set, the final assistant result text is written here (run.sh uses it as the
# per-iteration PR comment body).
SUMMARY_FILE = os.environ.get("STREAM_SUMMARY_FILE")


def _short(text, n=MAX):
    text = " ".join(str(text).split())  # collapse whitespace/newlines
    return text if len(text) <= n else text[: n - 1] + "…"


def _tool_summary(name, inp):
    """One-line description of a tool call, keyed on the important arg."""
    inp = inp or {}
    if name == "Bash":
        return f"$ {inp.get('command', '')}"
    if name in ("Read", "Edit", "Write", "NotebookEdit"):
        return inp.get("file_path", "")
    if name in ("Grep", "Glob"):
        q = inp.get("pattern", "")
        path = inp.get("path", "")
        return f"{q}" + (f"  in {path}" if path else "")
    if name == "Task" or name == "Agent":
        return inp.get("description", inp.get("prompt", ""))
    if name == "TodoWrite":
        todos = inp.get("todos", [])
        active = next((t.get("content") for t in todos
                       if t.get("status") == "in_progress"), None)
        return active or f"{len(todos)} todo(s)"
    if name == "Skill":
        return inp.get("skill", "")
    # generic: show first short value
    for v in inp.values():
        if isinstance(v, str) and v:
            return v
    return ""


def emit(line):
    print(_short(line), flush=True)


def main():
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            ev = json.loads(raw)
        except json.JSONDecodeError:
            # not JSON (e.g. a stray stderr line) — pass it through verbatim
            emit(raw)
            continue

        etype = ev.get("type")

        if etype == "system" and ev.get("subtype") == "init":
            model = ev.get("model", "?")
            emit(f"⏻  session start · model={model}")

        elif etype == "assistant":
            for block in ev.get("message", {}).get("content", []):
                btype = block.get("type")
                if btype == "text":
                    txt = block.get("text", "").strip()
                    if txt:
                        emit(f"💬 {txt}")
                elif btype == "tool_use":
                    name = block.get("name", "?")
                    emit(f"→  {name}: {_tool_summary(name, block.get('input'))}")
                elif btype == "thinking":
                    emit("…  (thinking)")

        elif etype == "result":
            if SUMMARY_FILE:
                try:
                    with open(SUMMARY_FILE, "w") as f:
                        f.write(ev.get("result") or "")
                except OSError:
                    pass
            sub = ev.get("subtype", "")
            cost = ev.get("total_cost_usd")
            dur = ev.get("duration_ms")
            tail = []
            if dur is not None:
                tail.append(f"{dur / 1000:.0f}s")
            if cost is not None:
                tail.append(f"${cost:.2f}")
            extra = (" · " + " · ".join(tail)) if tail else ""
            emit(f"■  done ({sub or 'ok'}){extra}")


if __name__ == "__main__":
    try:
        main()
    except (BrokenPipeError, KeyboardInterrupt):
        pass
