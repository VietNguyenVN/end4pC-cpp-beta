#!/usr/bin/env python3
"""Writes ~/.cache/end4pC/schemes.txt: for each colour scheme, the three main colours (primary, secondary, tertiary) it would
give the current wallpaper (or the accent colour) in the current mode. The settings window shows them as previews.

  schemes.py MODE [#ACCENT]

First line: the key it was made for (wallpaper|mode|accent), so it is only done again when one of them changes.
"""
import json
import os
import subprocess
import sys
import tempfile

SCHEMES = ["scheme-tonal-spot", "scheme-neutral", "scheme-content", "scheme-expressive", "scheme-fidelity",
           "scheme-fruit-salad", "scheme-monochrome", "scheme-rainbow", "scheme-vibrant", "scheme-smart"]
HOME = os.path.expanduser("~")
STATE = os.environ.get("XDG_STATE_HOME", HOME + "/.local/state") + "/end4pC"
CACHE = os.environ.get("XDG_CACHE_HOME", HOME + "/.cache") + "/end4pC"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "dark"
    accent = sys.argv[2] if len(sys.argv) > 2 else ""
    try:
        wall = open(STATE + "/wallpaper.txt").read().strip()
    except OSError:
        return
    key = "%s|%s|%s" % (wall, mode, accent)
    os.makedirs(CACHE, exist_ok=True)
    path = CACHE + "/schemes.txt"
    try:
        if open(path).readline().strip() == key:
            return
    except OSError:
        pass
    lines = [key]
    for t in SCHEMES:
        use = "scheme-tonal-spot" if accent and t == "scheme-neutral" else t  # Neutral becomes tonal spot for accents, as in matugen/apply.sh
        if accent:
            cmd = ["matugen", "color", "hex", accent, "--mode", mode, "--type", use, "--dry-run", "-j", "hex"]
        else:
            cmd = ["matugen", "--source-color-index", "0", "image", wall, "--mode", mode, "--type", use, "--dry-run", "-j", "hex"]
        try:
            d = json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout)["colors"]
            lines.append("|".join([t] + [d[k][mode]["color"] for k in ("primary", "secondary", "tertiary")]))
        except Exception:
            continue
    fd, tmp = tempfile.mkstemp(dir=CACHE)
    with os.fdopen(fd, "w") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, path)


main()
