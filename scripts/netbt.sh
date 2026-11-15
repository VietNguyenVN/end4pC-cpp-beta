#!/usr/bin/env bash
# end4pC Wi-Fi and Bluetooth pages of the control center. nmcli and bluetoothctl do the work.
#   netbt.sh list                  -> ~/.cache/end4pC/netbt.txt: the networks and devices as they are now (no scan)
#   netbt.sh scan wifi|bt          -> a Wi-Fi rescan, or a 15 s Bluetooth discovery; the lists are written before and after
#   netbt.sh wifi-connect SSID     -> joins a network whose password is saved, or an open one
#   netbt.sh wifi-password SSID    -> joins with the password the shell wrote to ~/.cache/end4pC/wifi-psk (read, then removed)
#   netbt.sh wifi-down SSID        -> disconnects the Wi-Fi card (SSID only names the action)
#   netbt.sh bt-connect|bt-disconnect|bt-pair|bt-forget MAC
#   netbt.sh fake-toggle wifi|bt   -> fake mode only: flips the fake radio (the control center's switch uses it through cc.sh)
# The shell starts this detached and reads two files: netbt.txt (the lists) and netbt-act.txt (the last action: id, action,
# target, status = busy | ok | secrets | fail). A Wi-Fi password is never written on this script's command line.
# END4PC_FAKE_NET=1 imitates a Wi-Fi card and some Bluetooth devices, to try the pages without the hardware.
# Its state is kept in ~/.cache/end4pC/netbt-fake.txt (delete the file to start again).
set -u
dir="${XDG_CACHE_HOME:-$HOME/.cache}/end4pC"
mkdir -p "$dir"
listf="$dir/netbt.txt"; actf="$dir/netbt-act.txt"; psk="$dir/wifi-psk"; fakef="$dir/netbt-fake.txt"
fake="${END4PC_FAKE_NET:-}"

write_atomic() { local tmp="$1.tmp.$$"; cat > "$tmp" && mv -f "$tmp" "$1"; }
set_act() { printf 'id=%s\naction=%s\ntarget=%s\nstatus=%s\n' "$(date +%s%N)" "$1" "$2" "$3" | write_atomic "$actf"; }
scanning() { local f="$dir/netbt-scan-$1"; [ -f "$f" ] && [ -n "$(find "$f" -newermt '-40 seconds' 2>/dev/null)" ]; }

# ---- real hardware
wifi_dev() { nmcli -t -f DEVICE,TYPE dev 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}'; }
wifi_lines() {
    local dev saved line active rest signal sec ssid secure known
    dev="$(wifi_dev)"
    if [ -z "$dev" ]; then echo "wifi=absent"; return; fi
    if [ "$(nmcli -t radio wifi 2>/dev/null)" != enabled ]; then echo "wifi=off"; return; fi
    echo "wifi=on"
    echo "wifiIp=$(nmcli -t -f IP4.ADDRESS dev show "$dev" 2>/dev/null | head -n1 | cut -d: -f2 | cut -d/ -f1)"
    saved="$(nmcli -t -f NAME,TYPE connection show 2>/dev/null | awk -F: '$2=="802-11-wireless"{print $1}')"
    nmcli -t -f ACTIVE,SIGNAL,SECURITY,SSID dev wifi list 2>/dev/null | while IFS= read -r line; do
        active="${line%%:*}"; rest="${line#*:}"
        signal="${rest%%:*}"; rest="${rest#*:}"
        sec="${rest%%:*}"; ssid="${rest#*:}"
        ssid="${ssid//\\:/:}"                       # nmcli escapes the colons inside a value
        [ -n "$ssid" ] || continue
        secure=0; [ -n "$sec" ] && [ "$sec" != "--" ] && secure=1
        known=0; grep -qxF -- "$ssid" <<< "$saved" && known=1
        printf 'ap=%s\t%s\t%s\t%s\t%s\n' "$([ "$active" = yes ] && echo 1 || echo 0)" "$signal" "$secure" "$known" "$ssid"
    done
}
bt_field() { sed -n "s/^[[:space:]]*$2: //p" <<< "$1" | head -n1; }
bt_lines() {
    local mac name info label paired connected icon battery state
    if ! { compgen -G '/sys/class/bluetooth/hci*' >/dev/null && command -v bluetoothctl >/dev/null 2>&1; }; then echo "bt=absent"; return; fi
    if timeout 2 bluetoothctl show 2>/dev/null | grep -q 'Powered: yes'; then echo "bt=on"; else echo "bt=off"; fi
    timeout 3 bluetoothctl devices 2>/dev/null | while read -r _ mac name; do
        [ -n "$mac" ] || continue
        info="$(timeout 3 bluetoothctl info "$mac" 2>/dev/null)"
        label="$(bt_field "$info" Alias)"; [ -n "$label" ] || label="${name:-$mac}"
        paired="$(bt_field "$info" Paired)"; connected="$(bt_field "$info" Connected)"
        icon="$(bt_field "$info" Icon)"
        battery="$(bt_field "$info" 'Battery Percentage' | sed -n 's/.*(\([0-9]*\)).*/\1/p')"
        if [ "$connected" = yes ]; then state=connected; elif [ "$paired" = yes ]; then state=paired; else state=new; fi
        printf 'dev=%s\t%s\t%s\t%s\t%s\n' "$mac" "$state" "$icon" "$battery" "$label"
    done
}

# ---- fake hardware (END4PC_FAKE_NET=1)
fake_aps() { printf '%s\n' 'Home-5G|82|1' 'Office|64|1' 'CoffeeShop|48|0' 'Neighbour-2.4|35|1' 'Guest|22|0' 'Library|30|1' 'Studio|18|1' 'Lab-Net|12|0'; }   # ssid|signal|secured
fake_devices() {                                                                                                # mac|name|bluez icon|battery
    printf '%s\n' 'AA:BB:CC:00:00:01|Headphones WH|audio-headset|80' 'AA:BB:CC:00:00:02|Keyboard K|input-keyboard|' \
        'AA:BB:CC:00:00:03|Phone|phone|64' 'AA:BB:CC:00:00:04|Mouse M|input-mouse|' 'AA:BB:CC:00:00:05|Speaker S|audio-card|'
}
fake_get() { sed -n "s/^$1=//p" "$fakef" 2>/dev/null | head -n1; }
fake_set() { local t="$fakef.tmp.$$"; { grep -v "^$1=" "$fakef" 2>/dev/null; echo "$1=$2"; } > "$t" && mv -f "$t" "$fakef"; }
fake_init() {
    [ -f "$fakef" ] && return
    printf 'wifi=on\nactive=Home-5G\nknown=Home-5G|Guest\nbt=on\n' > "$fakef"
    fake_devices | while IFS='|' read -r mac _ _ _; do
        case "$mac" in *:01) s=connected ;; *:02|*:03) s=paired ;; *) s=new ;; esac
        echo "dev.$mac=$s" >> "$fakef"
    done
}
fake_secured() { fake_aps | awk -F'|' -v s="$1" '$1==s{print $3; exit}' | grep -q 1; }
fake_known() { case "|$(fake_get known)|" in *"|$1|"*) return 0 ;; esac; return 1; }
fake_lines() {
    local wifi active ssid signal secure a k mac name icon battery state
    fake_init
    wifi="$(fake_get wifi)"; active="$(fake_get active)"
    if [ "$wifi" != on ]; then echo "wifi=off"; else
        echo "wifi=on"
        [ -n "$active" ] && echo "wifiIp=192.168.1.20" || echo "wifiIp="
        while IFS='|' read -r ssid signal secure; do
            a=0; k=0
            [ "$ssid" = "$active" ] && a=1
            fake_known "$ssid" && k=1
            printf 'ap=%s\t%s\t%s\t%s\t%s\n' "$a" "$signal" "$secure" "$k" "$ssid"
        done < <(fake_aps)
    fi
    echo "bt=$(fake_get bt)"
    while IFS='|' read -r mac name icon battery; do
        state="$(fake_get "dev.$mac")"; [ -n "$state" ] || state=new
        printf 'dev=%s\t%s\t%s\t%s\t%s\n' "$mac" "$state" "$icon" "$battery" "$name"
    done < <(fake_devices)
}

# ---- lists
lists() {
    {
        if [ -n "$fake" ]; then fake_lines; else wifi_lines; bt_lines; fi
        if scanning wifi; then echo "wifiScan=1"; else echo "wifiScan=0"; fi
        if scanning bt; then echo "btScan=1"; else echo "btScan=0"; fi
    } | write_atomic "$listf"
}

# ---- actions
# wifi_join SSID PASSWORD: PASSWORD is empty for a saved or an open network.
wifi_join() {
    local ssid="$1" pw="$2" log="$dir/netbt-connect.log" status=fail rc
    set_act wifi-connect "$ssid" busy; lists
    if [ -n "$fake" ]; then
        sleep 1
        if [ -z "$pw" ] && fake_secured "$ssid" && ! fake_known "$ssid"; then status=secrets
        elif [ -n "$pw" ] && [ "${#pw}" -lt 8 ]; then status=fail
        else
            fake_set active "$ssid"
            [ -n "$pw" ] && ! fake_known "$ssid" && fake_set known "$(fake_get known)|$ssid"
            status=ok
        fi
    else
        if [ -n "$pw" ]; then nmcli -w 40 dev wifi connect "$ssid" password "$pw" >"$log" 2>&1 </dev/null; rc=$?
        else nmcli -w 40 dev wifi connect "$ssid" >"$log" 2>&1 </dev/null; rc=$?; fi
        if [ "$rc" -eq 0 ]; then status=ok
        elif grep -qi 'secret\|password' "$log"; then status=secrets; fi
    fi
    set_act wifi-connect "$ssid" "$status"; lists
}
do_wifi_password() {
    local pw; pw="$(cat "$psk" 2>/dev/null)"; rm -f "$psk"
    wifi_join "$1" "$pw"
}
do_wifi_down() {
    local dev rc=1
    set_act wifi-down "$1" busy; lists
    if [ -n "$fake" ]; then sleep 0.6; fake_set active ""; rc=0
    else dev="$(wifi_dev)"; [ -n "$dev" ] && nmcli device disconnect "$dev" >/dev/null 2>&1 </dev/null; rc=$?; fi
    set_act wifi-down "$1" "$([ "$rc" -eq 0 ] && echo ok || echo fail)"; lists
}
do_bt() {  # do_bt ACTION MAC
    local action="$1" mac="$2" st=fail out cur
    set_act "$action" "$mac" busy; lists
    if [ -n "$fake" ]; then
        sleep 0.8
        cur="$(fake_get "dev.$mac")"
        case "$action" in
            bt-connect)    [ "$cur" = paired ] && { fake_set "dev.$mac" connected; st=ok; } ;;
            bt-disconnect) [ "$cur" = connected ] && { fake_set "dev.$mac" paired; st=ok; } ;;
            bt-pair)       [ "$cur" = new ] && { fake_set "dev.$mac" connected; st=ok; } ;;
            bt-forget)     fake_set "dev.$mac" new; st=ok ;;
        esac
    else
        case "$action" in
            bt-connect)    out="$(timeout 25 bluetoothctl connect "$mac" 2>&1 </dev/null)"; grep -q 'Connection successful' <<< "$out" && st=ok ;;
            bt-disconnect) out="$(timeout 15 bluetoothctl disconnect "$mac" 2>&1 </dev/null)"; grep -q 'Successful' <<< "$out" && st=ok ;;
            bt-pair)       # pairs with a built-in agent that needs no PIN; the device must be in pairing mode
                out="$({ printf 'agent NoInputNoOutput\ndefault-agent\npair %s\n' "$mac"; sleep 12
                         printf 'trust %s\nconnect %s\nquit\n' "$mac" "$mac"; sleep 3; } | timeout 45 bluetoothctl 2>&1)"
                grep -q 'Pairing successful' <<< "$out" && st=ok ;;
            bt-forget)     out="$(timeout 10 bluetoothctl remove "$mac" 2>&1 </dev/null)"; grep -q 'removed' <<< "$out" && st=ok ;;
        esac
    fi
    set_act "$action" "$mac" "$st"; lists
}
do_scan() {
    local what="$1"
    touch "$dir/netbt-scan-$what"; lists
    if [ -n "$fake" ]; then sleep 1.5
    elif [ "$what" = wifi ]; then nmcli dev wifi list --rescan yes >/dev/null 2>&1 </dev/null
    else timeout 25 bluetoothctl --timeout 15 scan on >/dev/null 2>&1 </dev/null; fi
    rm -f "$dir/netbt-scan-$what"; lists
}

case "${1:-}" in
    list) lists ;;
    scan) case "${2:-}" in wifi|bt) do_scan "$2" ;; *) echo "usage: netbt.sh scan wifi|bt" >&2; exit 2 ;; esac ;;
    wifi-connect) [ -n "${2:-}" ] && wifi_join "$2" "" ;;
    wifi-password) [ -n "${2:-}" ] && do_wifi_password "$2" ;;
    wifi-down) do_wifi_down "${2:-}" ;;
    bt-connect|bt-disconnect|bt-pair|bt-forget)
        if [[ "${2:-}" =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then do_bt "$1" "$2"; else echo "not a device address" >&2; exit 2; fi ;;
    fake-toggle)
        [ -n "$fake" ] || exit 0
        fake_init
        case "${2:-}" in
            wifi) [ "$(fake_get wifi)" = on ] && fake_set wifi off || fake_set wifi on ;;
            bt)   [ "$(fake_get bt)" = on ] && fake_set bt off || fake_set bt on ;;
        esac ;;
    *) echo "usage: netbt.sh list | scan wifi|bt | wifi-connect SSID | wifi-password SSID | wifi-down SSID | bt-connect|bt-disconnect|bt-pair|bt-forget MAC" >&2; exit 2 ;;
esac
