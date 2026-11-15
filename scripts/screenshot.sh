#!/usr/bin/env bash
# end4pC screenshot tool.
#   screenshot.sh region|screen|window|edit [border-colour #rrggbb]
# Saves a PNG in <Pictures>/Screenshots, copies it to the clipboard and tells you where it went.
# region: drag a rectangle (Esc cancels). screen: the monitor with the focus. window: the active window.
# edit: like region, but the picture opens in swappy (draw, crop, text) before it is saved. Every other mode shows an
# "Edit" button on the notification that opens the saved picture in swappy.
set -u
mode="${1:-region}"
accent="${2:-#ffffff}"
folder="${3:--}"   # a folder, or - for Pictures/Screenshots
copy="${4:-1}"     # 1: copy the picture to the clipboard too

if [ "$folder" = "-" ]; then
    dir="$(xdg-user-dir PICTURES 2>/dev/null || true)"
    dir="${dir:-$HOME/Pictures}/Screenshots"
else
    dir="${folder/#\~/$HOME}"
fi
mkdir -p "$dir"
file="$dir/Screenshot_$(date +%Y-%m-%d_%H.%M.%S.%3N).png"

case "$mode" in
  region|edit)
    # Dimmed screen, the chosen area stays clear, border in the shell's accent colour, size shown while dragging.
    geometry="$(slurp -d -b '#00000070' -c "${accent}ff" -s '#00000000' -w 2)" || exit 0
    [ -n "$geometry" ] || exit 0
    grim -g "$geometry" "$file" ;;
  screen)
    monitor="$(hyprctl activeworkspace -j | jq -r '.monitor')"
    grim -o "$monitor" "$file" ;;
  window)
    geometry="$(hyprctl activewindow -j | jq -r 'select(.address != null) | "\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"')"
    [ -n "$geometry" ] || { echo "no active window" >&2; exit 1; }
    grim -g "$geometry" "$file" ;;
  *)
    echo "usage: $0 region|screen|window|edit [#rrggbb]" >&2; exit 2 ;;
esac

[ -s "$file" ] || { echo "screenshot failed" >&2; exit 1; }

if [ "$mode" = edit ]; then
    # swappy saves the edited picture over the file (Ctrl+S) or copies it (Ctrl+C); nothing else to do here.
    exec swappy -f "$file" -o "$file"
fi

[ "$copy" = 1 ] && { wl-copy < "$file" 2>/dev/null || true; }
echo "$file"
# Needs a notification daemon to show anything; harmless without one.
# The notification has an Edit button; notify-send waits for the answer (up to 15 minutes: the button also works from
# the notification panel), so it runs in the background.
(
    answer="$(timeout 900 notify-send -a end4pC -i "$file" -A edit=Edit -w "Screenshot saved" "$file" 2>/dev/null || true)"
    [ "$answer" = edit ] && exec swappy -f "$file" -o "$file"
) >/dev/null 2>&1 &
