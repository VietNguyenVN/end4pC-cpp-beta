#!/usr/bin/env bash
# end4pC brightness helper.
#   brightness.sh get            -> prints "<current> <max>", then "done"
#   brightness.sh set <percent>  -> prints "done"
# Laptops (a /sys/class/backlight device) go through brightnessctl; external monitors through DDC/CI with ddcutil,
# whose bus number is looked up once and cached.
set -u
verb="${1:-get}"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$cache"

if compgen -G "/sys/class/backlight/*" >/dev/null; then
    case "$verb" in
        get) echo "$(brightnessctl -c backlight g) $(brightnessctl -c backlight m)" ;;
        set) brightnessctl -c backlight s "${2:-50}%" -q ;;
    esac
    echo done
    exit 0
fi

bus_file="$cache/ddc-bus"
if [ ! -s "$bus_file" ]; then
    ddcutil detect --brief 2>/dev/null | sed -n 's|.*I2C bus: */dev/i2c-\([0-9]*\).*|\1|p' | head -1 > "$bus_file"
fi
bus="$(cat "$bus_file" 2>/dev/null)"
[ -n "$bus" ] || { echo fail; exit 1; }

case "$verb" in
    get)
        out="$(ddcutil -b "$bus" getvcp 10 --brief 2>/dev/null)"   # "VCP 10 C 56 100"
        set -- $out
        [ "${5:-}" ] && echo "${5}" > "$cache/ddc-max"
        echo "${4:-50} ${5:-100}"
        ;;
    set)
        max="$(cat "$cache/ddc-max" 2>/dev/null || echo 100)"
        ddcutil -b "$bus" setvcp 10 $(( ${2:-50} * max / 100 )) --noverify >/dev/null 2>&1
        ;;
esac
echo done
