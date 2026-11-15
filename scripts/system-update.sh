#!/usr/bin/env bash
# end4pC system update, the one every entry point runs in the user's terminal (the bar's updates module, the control center tile and
# Settings -> About): update pacman (+ the AUR with yay or paru, + Flatpak), then check what is left, so updates.json (the file the
# bar, the control center and Settings read) is fresh when this ends, whichever of them started it.
#   system-update.sh AUR FLATPAK        AUR and FLATPAK are 1 or 0 (the sources that are switched on)
set -u
want_aur=${1:-0}; want_flatpak=${2:-0}
here="$(cd "$(dirname "$0")" && pwd)"

if [ "$want_aur" = 1 ]; then
    if command -v yay >/dev/null; then yay -Syu --combinedupgrade=false
    elif command -v paru >/dev/null; then paru -Syu
    else sudo pacman -Syu; fi
else
    sudo pacman -Syu
fi
if [ "$want_flatpak" = 1 ] && command -v flatpak >/dev/null; then
    echo; echo '== Flatpak =='; flatpak update
fi

echo; echo 'Checking what is left...'
# Wait for a check that was already running (it may have started before the update ended and be stale), then check again.
END4PC_UPDATES_WAIT=1 "$here/updates.sh" "$want_aur" "$want_flatpak"
left=$(sed -n 's/.*"pacman": *\([0-9]*\).*"aur": *\([0-9]*\).*"flatpak": *\([0-9]*\).*/\1 \2 \3/p' "${XDG_CACHE_HOME:-$HOME/.cache}/end4pC/updates.json" 2>/dev/null)
total=0; for n in $left; do total=$((total + n)); done
if [ -n "$left" ] && [ "$total" -eq 0 ]; then echo 'Up to date.'; elif [ -n "$left" ]; then echo "$total still pending."; fi
echo; read -n 1 -s -r -p 'Done. Press any key to close.'
