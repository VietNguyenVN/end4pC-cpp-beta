#!/usr/bin/env python3
"""What the "AI usage" bar module shows: one JSON line, read from the files the tools leave on this machine.

claude: tokens used in the current 5 hour window (from ~/.claude), and how many sessions are open now.
codex:  the account limits Codex itself reported last (from ~/.codex), already as a fraction.
Nothing is sent anywhere and no key is needed. Arguments: "claude", "codex" (default: both).
"""
import glob
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path

WINDOW = 5 * 3600


def claude():
    home = os.path.expanduser("~/.claude")
    now = time.time()
    if not os.path.isdir(home):
        return {"available": False}
    sessions = 0
    for path in glob.glob(os.path.join(home, "sessions", "*.json")):
        try:
            with open(path) as f:
                pid = json.load(f).get("pid")
            if pid and os.path.exists(f"/proc/{pid}"):
                sessions += 1
        except Exception:
            continue
    entries = {}
    for path in glob.glob(os.path.join(home, "projects", "*", "*.jsonl")):
        try:
            if now - os.path.getmtime(path) > WINDOW * 2:
                continue
            with open(path, errors="ignore") as f:
                for line in f:
                    if '"usage"' not in line:
                        continue
                    try:
                        data = json.loads(line)
                        ts = datetime.fromisoformat(data["timestamp"].replace("Z", "+00:00")).timestamp()
                        if now - ts > WINDOW * 2:
                            continue
                        message = data["message"]
                        usage = message["usage"]
                        key = (message.get("id"), data.get("requestId"))
                        entries[key] = (ts, usage.get("input_tokens", 0) + usage.get("output_tokens", 0)
                                        + usage.get("cache_creation_input_tokens", 0))
                    except Exception:
                        continue
        except OSError:
            continue
    tokens, start = 0, None
    for ts, count in sorted(entries.values()):
        if start is None or ts >= start + WINDOW:
            start, tokens = ts - ts % 3600, 0
        tokens += count
    if start is None or now >= start + WINDOW:
        tokens, start = 0, None
    return {"available": True, "sessions": sessions, "tokens": tokens, "resetsAt": int(start + WINDOW) if start else 0}


def codex():
    home = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
    latest = None
    def mtime(p):
        try:
            return p.stat().st_mtime
        except OSError:
            return 0
    paths = sorted((home / "sessions").glob("**/*.jsonl"), key=mtime, reverse=True)
    for path in paths:
        if time.time() - mtime(path) > 7 * 86400:
            break  # Sorted newest first; sessions older than a week hold no current limits
        try:
            with path.open(errors="replace") as stream:
                for line in stream:
                    try:
                        event = json.loads(line)
                        payload = event.get("payload", {})
                        limits = payload.get("rate_limits")
                        if (event.get("type") != "event_msg" or payload.get("type") != "token_count"
                                or not isinstance(limits, dict)
                                or limits.get("limit_id") not in (None, "codex")
                                or not isinstance(limits.get("primary"), dict)):
                            continue
                        timestamp = event.get("timestamp", "")
                        if latest is None or timestamp > latest[0]:
                            latest = (timestamp, limits)
                    except (ValueError, TypeError, AttributeError):
                        continue
        except OSError:
            continue
    if latest is None:
        return {"available": False}
    primary = latest[1]["primary"]
    secondary = latest[1].get("secondary") or {}
    now = time.time()

    def pct(window):  # A window that has already reset stays empty until Codex reports again
        reset = window.get("resets_at") or 0
        return 0 if reset and reset < now else (window.get("used_percent") or 0) / 100
    return {"available": True, "percentage": pct(primary), "resetsAt": primary.get("resets_at") or 0,
            "weeklyAvailable": isinstance(secondary.get("used_percent"), (int, float)),
            "weeklyPercentage": pct(secondary), "weeklyResetsAt": secondary.get("resets_at") or 0}


if __name__ == "__main__":
    want = sys.argv[1:] or ["claude", "codex"]
    out = {}
    for name, fn in (("claude", claude), ("codex", codex)):
        if name in want:
            try:
                out[name] = fn()  # One failing provider must not stop the others
            except Exception:
                out[name] = {"available": False}
    print(json.dumps(out, separators=(",", ":")))
