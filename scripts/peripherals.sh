#!/usr/bin/env bash
# end4pC device batteries. Asks upower for every battery that is not the laptop's (mouse, keyboard, headset, ...) and
# writes them to ~/.cache/end4pC/peripherals.txt, one per line:  name|percent|state|kind|id
#   peripherals.sh [low% critical% notify(0|1)]
# With notify=1 it sends one notification when a device drops to `low` and one when it drops to `critical`, and
# does not repeat them until the device has been charged or is back above `low`.
set -u
low="${1:-20}"; crit="${2:-5}"; notify="${3:-1}"
dir="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$dir"
exec 9>"$dir/peripherals.lock"
flock -n 9 || exit 0
command -v upower >/dev/null 2>&1 || { : > "$dir/peripherals.txt"; exit 0; }

out=""
while read -r path; do
    case "$path" in */line_power*|*DisplayDevice|*battery_BAT*|*battery_BAT*) continue ;; esac
    info="$(upower -i "$path" 2>/dev/null)" || continue
    [[ "$info" == *"present:"*"yes"* ]] || continue
    [[ "$info" == *"power supply:"*"no"* ]] || continue
    name="$(sed -n 's/^ *model: *//p' <<<"$info" | head -n1)"
    kind="$(awk '/^  [a-z-]+$/ {print $1; exit}' <<<"$info")"
    pct="$(sed -n 's/^ *percentage: *\([0-9]*\)%.*/\1/p' <<<"$info" | head -n1)"
    state="$(sed -n 's/^ *state: *//p' <<<"$info" | head -n1)"
    [ -n "$pct" ] || continue
    [ -n "$name" ] || name="${kind:-Device}"
    id="${path##*/}"
    out+="$name|$pct|$state|${kind:-device}|$id"$'\n'
done < <(upower -e 2>/dev/null)
printf '%s' "$out" > "$dir/peripherals.txt.tmp"
mv -f "$dir/peripherals.txt.tmp" "$dir/peripherals.txt"

[ "$notify" = 1 ] || exit 0
mem="$dir/peripherals.notified"   # lines:  id|level  (level: low | critical)
touch "$mem"
new=""
while IFS='|' read -r name pct state kind id; do
    [ -n "$id" ] || continue
    prev="$(sed -n "s/^$id|//p" "$mem" | head -n1)"
    level=""
    if [ "$state" != charging ] && [ "$state" != fully-charged ] && [ "$pct" -gt 0 ]; then
        if [ "$pct" -le "$crit" ]; then level=critical
        elif [ "$pct" -le "$low" ]; then level=low; fi
    fi
    if [ "$level" = critical ] && [ "$prev" != critical ]; then
        notify-send -a "Shell" -u critical --hint=int:transient:1 "$name battery critically low" "$pct% remaining, please charge it"
    elif [ "$level" = low ] && [ -z "$prev" ]; then
        notify-send -a "Shell" --hint=int:transient:1 "$name battery low" "$pct% remaining"
    fi
    # keep the highest level reached until the device is charged or back above `low`
    if [ -n "$level" ]; then
        [ "$prev" = critical ] && level=critical
        new+="$id|$level"$'\n'
    fi
done < "$dir/peripherals.txt"
printf '%s' "$new" > "$mem"
