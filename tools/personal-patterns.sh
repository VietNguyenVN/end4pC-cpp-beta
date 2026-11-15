#!/usr/bin/env bash
# Prints, one per line, the strings that identify THIS machine or its owner, for the "nothing personal left" checks of
# tools/make-installer.sh and tools/sync-installer-templates.sh. Everything is read from the machine (the shell's config.json, the
# monitor files of hypr, the git identity): nothing personal is written in this file, so it can travel in the package.
set -u
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
{
    echo "/home/$USER"
    git config user.name 2>/dev/null
    git config user.email 2>/dev/null | cut -d@ -f1
    # the shell's own settings: the weather place, the pictures, the tray items it is told to hide
    python3 -I - "$DATA/end4pC/config.json" <<'PY' 2>/dev/null
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)
for k in ("weatherCity", "customImagePath", "avatarPicture", "avatarPath"):
    v = d.get(k)
    if isinstance(v, str):
        print(v)
for v in d.get("trayExceptions") or []:
    if isinstance(v, str):
        print(v)
PY
    # the screens named in the hypr config
    sed -n -E 's/.*output *= *"([^"]+)".*/\1/p' "$CONFIG/hypr/monitors.lua" 2>/dev/null
    sed -n -E 's/^monitor *= *([^,]+),.*/\1/p' "$CONFIG/hypr/monitors.conf" 2>/dev/null
} | awk 'length($0) >= 4' | sort -u
