#!/usr/bin/env bash
# Video wallpapers with mpvpaper.
#   video_wallpaper.sh still FILE     print the path of a still picture of FILE (its first frame, cached); the shell paints it and builds the palette from it
#   video_wallpaper.sh start FILE     play FILE as the wallpaper on every screen and remember it
#   video_wallpaper.sh stop           stop the video and forget it (a picture was chosen)
#   video_wallpaper.sh restore        play again what was playing (at login or after the shell restarts)
#   video_wallpaper.sh pause|resume   stop / continue the player (the shell does it: fullscreen window, battery, lock screen)
#   video_wallpaper.sh covered        print 1 when every screen has a fullscreen window, else 0
# The shell keeps drawing its own wallpaper (the still) underneath; mpvpaper covers it.
set -u
state="${XDG_STATE_HOME:-$HOME/.local/state}/end4pC"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC/video-stills"
remembered="$state/video-wallpaper.txt"
# No sound, loops, the picture fills the screen. load-scripts=no keeps the user's mpv scripts out of it.
opts="no-audio loop hwdec=auto panscan=1.0 load-scripts=no"

still() {
    local file key out
    file="$(realpath "$1")" || exit 1
    key="$(printf '%s|%s' "$file" "$(stat -c '%Y.%s' "$file")" | sha1sum | cut -c1-16)"
    out="$cache/$key.png"
    if [ ! -s "$out" ]; then
        mkdir -p "$cache"
        ffmpeg -nostdin -loglevel error -y -i "$file" -frames:v 1 "$out.tmp.png" && mv "$out.tmp.png" "$out" || { rm -f "$out.tmp.png"; exit 1; }
    fi
    printf '%s\n' "$file" > "$out.src"  # (which video this still belongs to: a preset saved with only the still can find it again)
    printf '%s\n' "$out"
}

stop_player() { pkill -KILL -x mpvpaper 2>/dev/null; return 0; }  # (KILL: a stopped player would not hear TERM)

play() {
    command -v mpvpaper >/dev/null 2>&1 || { echo "mpvpaper is not installed" >&2; exit 1; }
    stop_player
    # -f: mpvpaper detaches itself. ALL: every output.
    setsid -f mpvpaper -f -o "$opts" ALL "$1" >/dev/null 2>&1
}

# 1 when every screen has a fullscreen window (Hyprland), else 0.
covered() {
    command -v hyprctl >/dev/null 2>&1 && command -v jq >/dev/null 2>&1 || { echo 0; return; }
    local m c
    m="$(hyprctl -j monitors 2>/dev/null)"; c="$(hyprctl -j clients 2>/dev/null)"
    [ -n "$m" ] && [ -n "$c" ] || { echo 0; return; }
    jq -n --argjson m "$m" --argjson c "$c" '($m | map(.activeWorkspace.id)) as $ws | if ($ws | length) > 0 and all($ws[]; . as $w | any($c[]; .mapped and .fullscreen > 0 and .workspace.id == $w)) then 1 else 0 end'
}

case "${1:-}" in
    still) [ -f "${2:-}" ] || exit 1; still "$2" ;;
    start) [ -f "${2:-}" ] || exit 1; mkdir -p "$state"; realpath "$2" > "$remembered"; play "$(realpath "$2")" ;;
    stop) stop_player; rm -f "$remembered" ;;
    pause) pkill -STOP -x mpvpaper 2>/dev/null; exit 0 ;;
    resume) pkill -CONT -x mpvpaper 2>/dev/null; exit 0 ;;
    covered) covered ;;
    restore) [ -f "$remembered" ] || exit 0; pgrep -x mpvpaper >/dev/null && exit 0; f="$(cat "$remembered")"; [ -f "$f" ] && play "$f" ;;
    *) echo "usage: video_wallpaper.sh still|start FILE | stop | restore" >&2; exit 2 ;;
esac
