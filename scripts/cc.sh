#!/usr/bin/env bash
# end4pC control center helper.
#   cc.sh status          -> writes ~/.cache/end4pC/cc.txt: wifi, wifiName, bt, btName, night (absent | off | on)
#   cc.sh toggle wifi|bt|night
# Hardware that is not there reports "absent"; the panel shows those tiles dimmed.
set -u
dir="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$dir"

wifi_dev() { nmcli -t -f DEVICE,TYPE dev 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}'; }
bt_present() { compgen -G '/sys/class/bluetooth/hci*' >/dev/null; }
night_tool() {
    # hyprsunset only works on Hyprland (its own colour-management protocol); everywhere else (niri...) wlsunset does the job.
    if [ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] && command -v wlsunset >/dev/null 2>&1; then echo wlsunset
    elif command -v hyprsunset >/dev/null 2>&1; then echo hyprsunset
    elif command -v wlsunset >/dev/null 2>&1; then echo wlsunset
    fi
}

# Night light: off | on | auto. Auto turns it on between nightFrom and nightTo (config.json, default 19:00-06:30).
state_file="${XDG_STATE_HOME:-$HOME/.local/state}/end4pC/night-mode"
cfg="${XDG_DATA_HOME:-$HOME/.local/share}/end4pC/config.json"
[ -f "$cfg" ] || cfg="$(dirname "$0")/../config.json"   # (the old place, until the shell has copied it over)
night_mode() { local m; m="$(cat "$state_file" 2>/dev/null)"; case "$m" in on|auto) echo "$m" ;; *) echo off ;; esac; }
cfg_str() { sed -n "s/.*\"$1\": *\"\([^\"]*\)\".*/\1/p" "$cfg" 2>/dev/null | head -n1; }
cfg_int() { sed -n "s/.*\"$1\": *\([0-9]*\).*/\1/p" "$cfg" 2>/dev/null | head -n1; }
night_want() {   # prints on/off for the current mode
    local mode from to t f e; mode="$(night_mode)"
    [ "$mode" = auto ] || { [ "$mode" = on ] && echo on || echo off; return; }
    from="$(cfg_str nightFrom)"; from="${from:-19:00}"; to="$(cfg_str nightTo)"; to="${to:-06:30}"
    t=$((10#$(date +%H) * 60 + 10#$(date +%M))); f=$((10#${from%:*} * 60 + 10#${from#*:})); e=$((10#${to%:*} * 60 + 10#${to#*:}))
    if [ "$f" -lt "$e" ]; then { [ "$t" -ge "$f" ] && [ "$t" -lt "$e" ]; } && echo on || echo off
    else { [ "$t" -ge "$f" ] || [ "$t" -lt "$e" ]; } && echo on || echo off; fi
}
night_apply() {
    local tool temp; tool="$(night_tool)"; [ -n "$tool" ] || return 0
    temp="$(cfg_int nightTemp)"; temp="${temp:-4000}"
    if [ "$(night_want)" = on ]; then
        pgrep -x "$tool" >/dev/null 2>&1 && return 0
        if [ "$tool" = hyprsunset ]; then setsid hyprsunset -t "$temp" >/dev/null 2>&1 &
        else setsid wlsunset -t "$temp" -T "$((temp + 1))" >/dev/null 2>&1 & fi
    else
        pkill -x "$tool" 2>/dev/null
    fi
}

# END4PC_FAKE_NET=1: the radios are the fake ones of scripts/netbt.sh (no hardware needed); the tiles read its list.
fake_radios() {
    local f="$dir/netbt.txt"
    "$(dirname "$0")/netbt.sh" list >/dev/null 2>&1
    wifi="$(sed -n 's/^wifi=//p' "$f" | head -n1)"; wname="$(awk -F'\t' '/^ap=1/{print $5; exit}' "$f")"
    bt="$(sed -n 's/^bt=//p' "$f" | head -n1)"; bname="$(awk -F'\t' '$1 ~ /^dev=/ && $2=="connected"{print $5; exit}' "$f")"
}
status() {
    local wifi=absent wname="" bt=absent bname="" night=absent dev tool
    if [ -n "${END4PC_FAKE_NET:-}" ]; then
        fake_radios
    elif command -v nmcli >/dev/null 2>&1 && dev="$(wifi_dev)" && [ -n "$dev" ]; then
        if [ "$(nmcli -t radio wifi 2>/dev/null)" = "enabled" ]; then
            wifi=on
            wname="$(nmcli -t -f GENERAL.CONNECTION dev show "$dev" 2>/dev/null | cut -d: -f2- | head -n1)"
            [ "$wname" = "--" ] && wname=""
        else
            wifi=off
        fi
    fi
    if [ -n "${END4PC_FAKE_NET:-}" ]; then :
    elif bt_present && command -v bluetoothctl >/dev/null 2>&1; then
        if timeout 2 bluetoothctl show 2>/dev/null | grep -q 'Powered: yes'; then
            bt=on
            bname="$(timeout 2 bluetoothctl devices Connected 2>/dev/null | head -n1 | cut -d' ' -f3-)"
        else
            bt=off
        fi
    fi
    if tool="$(night_tool)" && [ -n "$tool" ]; then
        local mode; mode="$(night_mode)"
        if pgrep -x "$tool" >/dev/null 2>&1; then night=on; else night=off; fi
        [ "$mode" = auto ] && night="auto$night"   # autoon / autooff: auto mode, and whether the filter is active right now
    fi
    printf 'wifi=%s\nwifiName=%s\nbt=%s\nbtName=%s\nnight=%s\n' "$wifi" "$wname" "$bt" "$bname" "$night" > "$dir/cc.txt.tmp"
    mv -f "$dir/cc.txt.tmp" "$dir/cc.txt"
}

case "${1:-}" in
    status) status ;;
    toggle)
        if [ -n "${END4PC_FAKE_NET:-}" ]; then "$(dirname "$0")/netbt.sh" fake-toggle "${2:-}"; sleep 0.4; status; exit 0; fi
        case "${2:-}" in
            wifi)
                if [ "$(nmcli -t radio wifi 2>/dev/null)" = "enabled" ]; then nmcli radio wifi off; else nmcli radio wifi on; fi ;;
            bt)
                if timeout 2 bluetoothctl show 2>/dev/null | grep -q 'Powered: yes'; then
                    timeout 3 bluetoothctl power off >/dev/null 2>&1
                else
                    command -v rfkill >/dev/null 2>&1 && rfkill unblock bluetooth 2>/dev/null
                    timeout 3 bluetoothctl power on >/dev/null 2>&1
                fi ;;
            night)   # off -> on -> auto -> off
                mkdir -p "$(dirname "$state_file")"
                next=off   # (decided before the file is truncated)
                case "$(night_mode)" in off) next=on ;; on) next=auto ;; esac
                echo "$next" > "$state_file"
                night_apply ;;
        esac
        sleep 0.4
        status ;;
    night-apply) night_apply; status ;;
    *) echo "usage: cc.sh status | toggle wifi|bt|night" >&2; exit 2 ;;
esac
