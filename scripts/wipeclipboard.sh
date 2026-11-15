#!/usr/bin/env bash
# Empties the clipboard and the clipboard history: the current selection, the cliphist entries, and the cliphist database
# file itself (cliphist creates it again by itself the next time something is copied).
cache="${XDG_CACHE_HOME:-$HOME/.cache}/cliphist"
db="$cache/db"
before=$(stat -c %s "$db" 2>/dev/null || echo 0)
command -v wl-copy >/dev/null 2>&1 && wl-copy --clear
command -v cliphist >/dev/null 2>&1 && cliphist wipe
rm -f "$db"
freed=$(numfmt --to=iec --suffix=B "$before" 2>/dev/null || echo "${before} B")
notify-send -a end4pC -i edit-clear-all "Clipboard wiped" "History emptied ($freed freed)."
