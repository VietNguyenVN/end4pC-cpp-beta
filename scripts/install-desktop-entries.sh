#!/bin/sh
# Puts the separate programs of the shell (Play3r, Fil3s) in the application menu (and the launcher of the shell): for each assets/end4pC-*.desktop
# whose program was built, writes ~/.local/share/applications/<name>.desktop with the real path of this folder.
# It does not change the programs that open your files (Fil3s is not made the default file manager; see the README for that).
set -e
dir=$(cd "$(dirname "$0")/.." && pwd)
target="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$target"
for template in "$dir"/assets/end4pC-*.desktop; do
    name=$(basename "$template" .desktop)          # end4pC-play3r
    [ -x "$dir/build/$name" ] || continue          # only what was built
    grep -v '^# @DIR@' "$template" | sed "s|@DIR@|$dir|g" > "$target/$name.desktop"
    echo "in the application menu: $target/$name.desktop"
done
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$target" 2>/dev/null || true
