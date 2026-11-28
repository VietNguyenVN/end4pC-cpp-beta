#!/usr/bin/env python3
"""Builds the Material Symbols assets of the optional "Material Symbols" icon set (Settings -> Interface -> Material Symbols).

  tools/gen-material-assets.py [--font PATH] [--check]

Reads the variable font "Material Symbols Rounded" (Google, Apache-2.0; the Arch package ttf-material-symbols-variable installs it at
/usr/share/fonts/ttf-material-symbols-variable/) and writes, into assets/:

  fonts/MaterialSymbolsRounded-Outline.ttf   a STATIC instance (FILL 0, wght 400, opsz 24, GRAD 0) with only the glyphs the shell uses
  fonts/MaterialSymbolsRounded-Fill.ttf      the same with FILL 1 (the active / hover look)
  icons/material-map.tsv                     "icon name used by the code <TAB> Material Symbols name <TAB> code point (hex)"

The families are renamed ("End4pC Material Symbols" and "... Fill") so they never clash with a copy installed in the system.
This script is only for developers (it needs python-fonttools); the generated files are committed and shipped.

MAP below is the source of truth for the icon names that are NOT already a Material Symbols name (the shell's own icons are the
Solar set: "sun-2", "gps"...). A name that is itself a Material Symbols name needs no entry. `--check` only prints the coverage:
which names the code uses, and which of them have no Material Symbols glyph (those keep the built-in icon at run time).
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = "/usr/share/fonts/ttf-material-symbols-variable/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf"

# Solar name (as the code asks for it) -> Material Symbols name.
MAP = {
    "add-circle": "add_circle", "alarm": "alarm",
    "alt-arrow-down": "keyboard_arrow_down", "alt-arrow-left": "keyboard_arrow_left",
    "alt-arrow-right": "keyboard_arrow_right", "alt-arrow-up": "keyboard_arrow_up",
    "arrow-down": "arrow_downward", "arrow-left": "arrow_back", "arrow-up": "arrow_upward",
    "arrow-right-up": "north_east", "square-arrow-right-up": "open_in_new",
    "battery-charge": "battery_charging_full", "battery-full": "battery_full", "battery-half": "battery_3_bar",
    "battery-low": "battery_alert", "bell": "notifications", "bolt": "bolt", "bonfire": "local_fire_department",
    "boombox": "speaker", "boombox/lyrics": "lyrics", "box": "inventory_2", "calendar": "calendar_month", "calendar-date": "today",
    "camera": "photo_camera", "chat-line": "chat_bubble", "chat-round-dots": "sms", "check-circle": "check_circle",
    "check-read": "done_all", "checklist": "checklist", "city": "location_city", "claude": "auto_awesome",
    "openai": "smart_toy", "clipboard-list": "assignment", "clock-circle": "schedule", "cloud": "cloud",
    "cloud-bolt": "thunderstorm", "cloud-rain": "rainy", "cloud-snowfall": "weather_snowy",
    "cloud-sun": "partly_cloudy_day", "cloudy-moon": "partly_cloudy_night", "close-circle": "cancel",
    "copy": "content_copy", "cpu": "memory", "cup-hot": "coffee", "cursor": "arrow_selector_tool",
    "danger-circle": "error", "danger-triangle": "warning", "database": "database", "devices": "devices",
    "diskette": "save", "download-minimalistic": "download", "download-square": "download", "earth": "public",
    "eye": "visibility", "eye-closed": "visibility_off", "fog": "foggy", "folder": "folder",
    "folder-with-files": "folder_copy", "gallery": "image", "gallery-check": "photo_library",
    "gamepad": "sports_esports", "global": "language", "gps": "my_location", "graph-up": "trending_up",
    "hashtag-circle": "tag", "headphones-round": "headphones", "heart": "favorite", "home-2": "home",
    "info-circle": "info", "key": "key", "laptop": "laptop", "layers-minimalistic": "layers", "leaf": "eco",
    "letter": "mail", "link-broken": "link_off", "list": "format_list_bulleted", "lock-keyhole": "lock",
    "lock-keyhole-minimalistic": "lock", "login": "login", "logout-2": "logout", "magic-stick-3": "auto_fix_high",
    "magnifer": "search", "map-point": "location_on", "menu-dots": "more_horiz", "microphone": "mic",
    "microphone-3": "mic", "moon": "dark_mode", "moon-sleep": "bedtime", "mouse": "mouse",
    "music-note": "music_note", "music-notes": "music_note", "notebook": "book", "pause-circle": "pause_circle",
    "pen": "edit", "phone": "call", "phone-calling": "phone_in_talk", "pipette": "colorize", "plain": "flight",
    "plain-3": "flight", "play": "play_arrow", "power": "power_settings_new", "pulse": "monitor_heart",
    "radar": "radar", "radio": "radio", "record": "radio_button_checked", "record-circle": "radio_button_checked",
    "refresh-circle": "refresh", "reply": "reply", "restart": "restart_alt", "ruler-pen": "handyman", "ruler-pen/list": "toolbar",  # (the Tools button; "/list": its row in the Bar page of Settings)
    "server-square": "dns", "shield": "shield", "shield-keyhole": "shield_lock", "shield-warning": "gpp_maybe",
    "skip-next": "skip_next", "skip-previous": "skip_previous", "slider-minimalistic-horizontal": "toolbar",
    "smile-circle": "mood", "smartphone": "smartphone", "soundwave": "graphic_eq", "speaker": "speaker",
    "stop-circle": "stop_circle", "sun": "light_mode", "sun-2": "sunny", "sunrise": "wb_twilight",
    "sunset": "wb_twilight", "tablet": "tablet", "target": "my_location", "temperature_2m": "device_thermostat",
    "apparent_temperature": "device_thermostat", "text": "text_fields", "thermometer": "device_thermostat",
    "transfer-horizontal": "swap_horiz", "transmission": "cell_tower", "trash-bin-minimalistic": "delete",
    "undo-left": "undo", "undo-right": "redo", "usb": "usb", "user": "person", "user-circle": "account_circle",
    "videocamera-record": "videocam", "volume": "volume_up", "volume-cross": "volume_off",
    "volume-loud": "volume_up", "volume-small": "volume_down", "watch-round": "watch", "waterdrop": "water_drop",
    "wi-fi-router": "router", "widget": "widgets", "widget-2": "widgets", "widget-4": "widgets", "wind": "air",
    "window-frame": "web_asset", "pill": "medication", "close": "close", "x": "close",
    # the Tools window (src/tools/) and a few names that are only built at run time
    "clipboard": "content_paste", "eraser": "ink_eraser", "export": "ios_share", "file-right": "drive_file_move",
    "folder-open": "folder_open", "gallery-add": "add_photo_alternate", "history-2": "history", "muted": "volume_off",
    "scissors": "content_cut", "video-frame": "movie", "video-frame-cut": "movie_edit", "zip-file": "folder_zip",
    "link_broken": "link_off", "notes": "notes",
    # the Markdown toolbar of the Notes editor ("/context" aliases: the built-in icon is the name before the slash)
    "text-bold": "format_bold", "text-italic": "format_italic", "text-cross": "strikethrough_s", "code": "code",
    "hashtag/heading": "title", "list-1": "format_list_numbered", "chat-line/quote": "format_quote", "link": "link",
    "code-square": "data_object", "minus-square/hr": "horizontal_rule",
    # Tools, screenshot tool, power, drives, timers, keybinds... (names the shell uses that Material Symbols spells differently)
    "alarm-add": "alarm_add", "align-bottom": "vertical_align_bottom", "battery-charge-minimalistic": "battery_charging_full",
    "battery-full-minimalistic": "battery_full", "battery-half-minimalistic": "battery_3_bar", "battery-low-minimalistic": "battery_alert",
    "bell-off": "notifications_off", "bomb-minimalistic": "dangerous", "calculator-minimalistic": "calculate", "card": "credit_card",
    "cat": "pets", "cup": "coffee", "display": "desktop_windows", "dollar-minimalistic": "attach_money", "emoji": "emoji_emotions",
    "eraser-square": "ink_eraser", "eye-scan": "document_scanner", "flash-drive": "usb", "full-screen": "fullscreen",
    "gamepad-minimalistic": "sports_esports", "list-check": "checklist", "checklist-minimalistic": "checklist", "playlist": "queue_music", "repeat-one": "repeat_one", "repeat": "repeat", "magnifer-zoom-in": "zoom_in", "maximize-square-2": "open_in_full",
    "mouse_down": "mouse", "mouse_up": "mouse", "notebook-minimalistic": "book", "page_down": "keyboard_double_arrow_down",
    "page_up": "keyboard_double_arrow_up", "pen-2": "edit", "pen-new-round": "edit_square", "pills": "medication",
    "playback-speed": "speed", "qr-code": "qr_code_2", "rocket-2": "rocket_launch", "ruler": "straighten", "running": "directions_run",
    "screencast": "cast", "sd-card": "sd_card", "server": "dns", "server-minimalistic": "dns", "server-path": "lan",
    "shield-check": "verified_user", "smartphone-2": "smartphone", "ssd-square": "hard_drive", "stopwatch": "timer",
    "text-field": "text_fields", "text-selection": "select_all", "undo-left-round": "undo", "undo-right-round": "redo",
    "tuning-2": "tune", "stop_circle": "stop_circle", "weigher": "scale", "wi-fi-router-minimalistic": "router", "widget-add": "widgets",
}


def used_names():
    """Every string in the sources that is an icon the shell can draw: a Solar file name or a name of the icon map."""
    files = []
    for pattern in ("src/parts/*.inc", "src/*.cpp", "src/*.h", "src/*.inc", "src/tools/*"):
        files += glob.glob(f"{ROOT}/{pattern}")
    lits = set()
    for f in files:
        with open(f, encoding="utf-8", errors="ignore") as fh:
            lits |= set(re.findall(r'"([a-z0-9_\-]{2,40})"', fh.read()))
    assets = set()
    for d in ("outline", "bold", "broken", "line-duotone", "bold-duotone"):
        assets |= {os.path.basename(p)[:-4] for p in glob.glob(f"{ROOT}/assets/icons/{d}/*.svg")}
    imap = set()
    with open(f"{ROOT}/assets/icon-map.txt", encoding="utf-8") as fh:
        for line in fh:
            if "=" in line and not line.startswith("#"):
                imap.add(line.split("=")[0])
    return sorted({n for n in lits if n in assets or n in imap} | set(MAP)), imap


def resolve(names, glyph_cp):
    """name -> (material name, code point) for every name that has a glyph."""
    out, missing = {}, []
    for n in names:
        m = MAP.get(n, n)
        if m in glyph_cp:
            out[n] = (m, glyph_cp[m])
        else:
            missing.append(n if m == n else f"{n} -> {m}")
    return out, missing


def load_glyphs(font):
    """name -> code point of every icon, with the aliases: the font's glyphs carry one canonical name, but the ligatures (the way
    the font is normally used: the text "smartphone" shows the icon) know every name Google ever gave an icon."""
    cmap = font.getBestCmap()
    cp = {}
    for code, gname in sorted(cmap.items()):
        if code >= 0xE000:  # the private use area holds the icons
            cp.setdefault(gname, code)
    glyph_cp = {}
    for code, gname in cmap.items():
        if code >= 0xE000:
            glyph_cp.setdefault(gname, code)
    char = {gname: chr(code) for code, gname in cmap.items() if code < 0x80}
    gsub = font["GSUB"].table
    for lookup in gsub.LookupList.Lookup:
        for st in lookup.SubTable:
            st = getattr(st, "ExtSubTable", st)
            for first, ligs in getattr(st, "ligatures", {}).items():
                if first not in char:
                    continue
                for lig in ligs:
                    if all(c in char for c in lig.Component) and lig.LigGlyph in glyph_cp:
                        cp.setdefault(char[first] + "".join(char[c] for c in lig.Component), glyph_cp[lig.LigGlyph])
    return cp


def instance(path, fill, family):
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
    f = TTFont(path)
    f = instancer.instantiateVariableFont(f, {"FILL": fill, "wght": 400, "opsz": 24, "GRAD": 0})
    return f


def subset(font, unicodes, family):
    from fontTools import subset as ss
    opts = ss.Options()
    opts.layout_features = []  # the icons are drawn by code point: no ligatures needed
    opts.glyph_names = False
    opts.notdef_outline = True
    opts.hinting = False
    opts.name_IDs = [1, 2, 3, 4, 6]
    opts.drop_tables += ["DSIG", "GDEF", "GPOS", "GSUB", "STAT", "avar", "fvar", "gvar", "HVAR", "MVAR"]
    sub = ss.Subsetter(opts)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    name = font["name"]
    ps = family.replace(" ", "")
    for rec in list(name.names):
        if rec.nameID in (1, 16):
            rec.string = family
        elif rec.nameID in (2, 17):
            rec.string = "Regular"
        elif rec.nameID == 4:
            rec.string = family
        elif rec.nameID == 6:
            rec.string = ps
        elif rec.nameID == 3:
            rec.string = ps + "-gen"
    return font


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=FONT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    names, imap = used_names()
    from fontTools.ttLib import TTFont
    base = TTFont(a.font)
    glyph_cp = load_glyphs(base)
    got, missing = resolve(names, glyph_cp)
    print(f"icon names used by the code: {len(names)}; with a Material Symbols glyph: {len(got)}")
    if missing:
        print("no glyph (they keep the built-in icon):", ", ".join(missing))
    if a.check:
        return 0
    codes = sorted({cp for _, cp in got.values()})
    os.makedirs(f"{ROOT}/assets/fonts", exist_ok=True)
    for fill, tag, fam in ((0, "Outline", "End4pC Material Symbols"), (1, "Fill", "End4pC Material Symbols Fill")):
        f = subset(instance(a.font, fill, fam), codes, fam)
        out = f"{ROOT}/assets/fonts/MaterialSymbolsRounded-{tag}.ttf"
        f.save(out)
        print(out, os.path.getsize(out), "bytes")
    with open(f"{ROOT}/assets/icons/material-map.tsv", "w", encoding="utf-8") as fh:
        fh.write("# icon name used by the code <TAB> Material Symbols name <TAB> code point (hex). Written by tools/gen-material-assets.py.\n")
        for n in sorted(got):
            m, cp = got[n]
            fh.write(f"{n}\t{m}\t{cp:x}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
