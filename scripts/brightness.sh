#!/usr/bin/env bash
# end4pC brightness helper.
#   brightness.sh get [<percent> <output>]   -> prints "<current> <max>", then "done"
#   brightness.sh set <percent> [<output>]   -> prints "done"
# <output> is the connector of the monitor (eDP-1, HDMI-A-1, DP-2...). The screen of a laptop (a /sys/class/backlight device and an internal
# connector) goes through brightnessctl; every other monitor through DDC/CI with ddcutil, on the I2C bus of that connector. The bus of each
# connector is looked up once and cached. Without an output, the first monitor that answers is used.
set -u
verb="${1:-get}"
pct="${2:-50}"
out="${3:-}"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$cache"

has_backlight() { compgen -G "/sys/class/backlight/*" >/dev/null; }
internal=0
case "$out" in
    eDP*|LVDS*|DSI*) internal=1 ;;
    "") has_backlight && internal=1 ;;
esac

if [ "$internal" = 1 ] && has_backlight; then
    case "$verb" in
        get) echo "$(brightnessctl -c backlight g) $(brightnessctl -c backlight m)" ;;
        set) [ "$pct" -lt 5 ] && pct=5   # (a laptop screen at 0% is fully dark)
             brightnessctl -c backlight s "${pct}%" -q ;;
    esac
    echo done
    exit 0
fi

# connector -> bus, one "<connector> <bus>" per line
map_file="$cache/ddc-map"
build_map() {
    ddcutil detect --brief 2>/dev/null | awk '
        /^Display / { if (c != "" && b != "") print c, b; c = ""; b = "" }
        /I2C bus:/ { b = $NF; sub(".*i2c-", "", b) }
        /DRM connector:/ { c = $NF; sub("^card[0-9]+-", "", c) }
        END { if (c != "" && b != "") print c, b }' > "$map_file.tmp" && mv "$map_file.tmp" "$map_file"
}
[ -s "$map_file" ] || build_map
bus=""
if [ -n "$out" ]; then
    bus="$(awk -v o="$out" '$1 == o { print $2; exit }' "$map_file" 2>/dev/null)"
    if [ -z "$bus" ]; then  # (a monitor plugged in since the map was made)
        build_map
        bus="$(awk -v o="$out" '$1 == o { print $2; exit }' "$map_file" 2>/dev/null)"
    fi
else
    bus="$(awk 'NR == 1 { print $2 }' "$map_file" 2>/dev/null)"
fi
[ -n "$bus" ] || { echo fail; exit 1; }

case "$verb" in
    get)
        res="$(ddcutil -b "$bus" getvcp 10 --brief 2>/dev/null)"   # "VCP 10 C 56 100"
        set -- $res
        [ "${5:-}" ] && echo "${5}" > "$cache/ddc-max-$bus"
        echo "${4:-50} ${5:-100}"
        ;;
    set)
        max="$(cat "$cache/ddc-max-$bus" 2>/dev/null || echo 100)"
        ddcutil -b "$bus" setvcp 10 $(( pct * max / 100 )) --noverify >/dev/null 2>&1
        ;;
esac
echo done
