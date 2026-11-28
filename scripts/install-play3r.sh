#!/bin/sh
# Puts Play3r in the application menu (and the launcher of the shell): writes ~/.local/share/applications/end4pC-play3r.desktop with the real
# path of this folder. It does not change the programs that open your music files.
set -e
dir=$(cd "$(dirname "$0")/.." && pwd)
target="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$target"
grep -v '^# @DIR@' "$dir/assets/end4pC-play3r.desktop" | sed "s|@DIR@|$dir|g" > "$target/end4pC-play3r.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$target" 2>/dev/null || true
echo "Play3r is in the application menu: $target/end4pC-play3r.desktop"
