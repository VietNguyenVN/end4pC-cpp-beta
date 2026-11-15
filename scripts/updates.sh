#!/usr/bin/env bash
# end4pC update checker. Counts the pending updates of pacman (checkupdates), the AUR (yay or paru) and Flatpak,
# and writes them to ~/.cache/end4pC/updates.json. Only one check runs at a time.
set -u
dir="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$dir"
exec 9>"$dir/updates.lock"
# END4PC_UPDATES_WAIT=1 (after a system update): wait for a check already running instead of skipping, so the answer is from after the update.
if [ "${END4PC_UPDATES_WAIT:-0}" = 1 ]; then flock -w 180 9 || exit 0; else flock -n 9 || exit 0; fi

# Arguments: 1|0 for the AUR, 1|0 for Flatpak (both off by default: not everybody uses them).
want_aur=${1:-0}; want_flatpak=${2:-0}
pacman=0; aur=0; flatpak=0
if command -v checkupdates >/dev/null 2>&1; then
    pacman=$(checkupdates 2>/dev/null | wc -l)
fi
if [ "$want_aur" = 1 ]; then
    if command -v yay >/dev/null 2>&1; then
        aur=$(yay -Qua 2>/dev/null | wc -l)
    elif command -v paru >/dev/null 2>&1; then
        aur=$(paru -Qua 2>/dev/null | wc -l)
    fi
fi
if [ "$want_flatpak" = 1 ] && command -v flatpak >/dev/null 2>&1; then
    flatpak=$(flatpak remote-ls --updates --columns=application 2>/dev/null | wc -l)
fi
printf '{"pacman": %d, "aur": %d, "flatpak": %d, "time": %d}\n' "$pacman" "$aur" "$flatpak" "$(date +%s)" > "$dir/updates.json.tmp"
mv -f "$dir/updates.json.tmp" "$dir/updates.json"
