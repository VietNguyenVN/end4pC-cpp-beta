#!/usr/bin/env python3
"""end4pC system appearance: list and apply the cursor theme, the icon theme and the fonts.

No KDE tools needed. A change is written everywhere an app might look:
  - GTK 3 / 4: ~/.config/gtk-3.0|gtk-4.0/settings.ini, and gsettings when it exists
  - the cursor for the compositor (hyprctl setcursor; on sway `seat xcursor_theme` and ~/.config/sway/appearance) and for old / X11 apps (~/.icons/default/index.theme)
  - Qt: qt5ct / qt6ct, only when their config file is there
It does not touch the GTK theme (colors) on purpose.

Usage:
  system_appearance.py --list cursors|icons|fonts|mono
  system_appearance.py --get                      key=value lines
  system_appearance.py --set cursor THEME SIZE
  system_appearance.py --set icons THEME
  system_appearance.py --set font ui|mono FAMILY SIZE
"""
import configparser
import os
import re
import shutil
import subprocess
import sys

HOME = os.path.expanduser("~")
DATA = os.environ.get("XDG_DATA_HOME", os.path.join(HOME, ".local/share"))
CONF = os.environ.get("XDG_CONFIG_HOME", os.path.join(HOME, ".config"))
ICON_DIRS = ["/usr/share/icons", os.path.join(DATA, "icons"), os.path.join(HOME, ".icons")]


def run(*cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=8).stdout
    except Exception:
        return ""


NIRICONF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "niriconf.py")


def on_niri():
    return bool(os.environ.get("NIRI_SOCKET")) and bool(shutil.which("niri"))


def on_sway():
    return bool(os.environ.get("SWAYSOCK")) and bool(shutil.which("swaymsg"))


def sway_cursor(theme, size):
    """Sway draws its own pointer and takes the theme from `seat seat0 xcursor_theme`: set now (no reload), and written to the
    `set $cursor_theme` / `set $cursor_size` lines of ~/.config/sway/appearance, which the config (and the gsettings it runs at login)
    reads, so the choice is still there the next time sway starts."""
    run("swaymsg", "seat", "seat0", "xcursor_theme", theme, str(size))
    path = os.path.join(CONF, "sway", "appearance")
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return
    name = f'"{theme}"' if re.search(r"\s", theme) else theme
    new = re.sub(r"^set \$cursor_theme .*$", lambda m: f"set $cursor_theme {name}", text, flags=re.M)
    new = re.sub(r"^set \$cursor_size .*$", lambda m: f"set $cursor_size {size}", new, flags=re.M)
    if new != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)


def themes(kind):
    """Icon / cursor themes found on disk. A cursor theme has a cursors folder, an icon theme has directories listed."""
    out = set()
    for base in ICON_DIRS:
        if not os.path.isdir(base):
            continue
        for name in os.listdir(base):
            path = os.path.join(base, name)
            if not os.path.isdir(path) or name in ("default", "hicolor", "locolor"):
                continue
            if kind == "cursors":
                if os.path.isdir(os.path.join(path, "cursors")):
                    out.add(name)
            else:
                idx = os.path.join(path, "index.theme")
                if os.path.isfile(idx):
                    try:
                        with open(idx, errors="ignore") as f:
                            text = f.read()
                    except OSError:
                        continue
                    if re.search(r"^Directories=", text, re.M) and "-Dynamic-" not in name:
                        out.add(name)
    return sorted(out, key=str.lower)


def fonts(mono):
    text = run("fc-list", ":spacing=mono" if mono else ":", "family")
    fams = set()
    for line in text.splitlines():
        fam = line.split(",")[0].strip()
        if fam and not fam.startswith("."):
            fams.add(fam)
    return sorted(fams, key=str.lower)


# ---- reading
def gsettings_get(key):
    if not shutil.which("gsettings"):
        return ""
    return run("gsettings", "get", "org.gnome.desktop.interface", key).strip().strip("'")


def ini_get(key):
    for v in ("gtk-4.0", "gtk-3.0"):
        path = os.path.join(CONF, v, "settings.ini")
        if os.path.isfile(path):
            cp = configparser.ConfigParser(interpolation=None)
            cp.optionxform = str
            try:
                cp.read(path)
                if cp.has_option("Settings", key):
                    return cp.get("Settings", key)
            except configparser.Error:
                pass
    return ""


def split_font(spec, default_size=11):
    m = re.match(r"^(.*?)[ ,]+(\d+(?:\.\d+)?)$", spec.strip())
    if m:
        return m.group(1).strip(), int(float(m.group(2)))
    return spec.strip(), default_size


def get():
    cursor = gsettings_get("cursor-theme") or ini_get("gtk-cursor-theme-name")
    size = gsettings_get("cursor-size") or ini_get("gtk-cursor-theme-size") or "24"
    if on_niri():  # what niri itself uses wins: it is what the pointer really is
        got = dict(l.split("=", 1) for l in run(NIRICONF, "--dump", "cursorTheme", "cursorSize").splitlines() if "=" in l)
        cursor = got.get("cursorTheme") or cursor
        size = got.get("cursorSize") or size
    icons = gsettings_get("icon-theme") or ini_get("gtk-icon-theme-name")
    ui = gsettings_get("font-name") or ini_get("gtk-font-name")
    mono = gsettings_get("monospace-font-name")
    uf, us = split_font(ui or "Sans 11")
    mf, ms = split_font(mono or "Monospace 11")
    print(f"cursor={cursor}")
    print(f"cursorSize={int(float(size)) if size.replace('.', '', 1).isdigit() else 24}")
    print(f"icons={icons}")
    print(f"font={uf}")
    print(f"fontSize={us}")
    print(f"mono={mf}")
    print(f"monoSize={ms}")


# ---- writing
def ini_set(values):
    for v in ("gtk-3.0", "gtk-4.0"):
        d = os.path.join(CONF, v)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "settings.ini")
        cp = configparser.ConfigParser(interpolation=None)
        cp.optionxform = str
        if os.path.isfile(path):
            try:
                cp.read(path)
            except configparser.Error:
                pass
        if not cp.has_section("Settings"):
            cp.add_section("Settings")
        for k, val in values.items():
            cp.set("Settings", k, str(val))
        with open(path, "w") as f:
            cp.write(f, space_around_delimiters=False)


def gsettings_set(key, value):
    if shutil.which("gsettings"):
        run("gsettings", "set", "org.gnome.desktop.interface", key, str(value))


def qt_set(section, key, value):
    """qt5ct / qt6ct: only when the app has made its file (we do not create a Qt setup out of nothing)."""
    for app in ("qt5ct", "qt6ct"):
        path = os.path.join(CONF, app, f"{app}.conf")
        if not os.path.isfile(path):
            continue
        cp = configparser.ConfigParser(interpolation=None)
        cp.optionxform = str
        try:
            cp.read(path)
        except configparser.Error:
            continue
        if not cp.has_section(section):
            cp.add_section(section)
        cp.set(section, key, value)
        with open(path, "w") as f:
            cp.write(f, space_around_delimiters=False)


def set_cursor(theme, size):
    gsettings_set("cursor-theme", f"'{theme}'")
    gsettings_set("cursor-size", size)
    ini_set({"gtk-cursor-theme-name": theme, "gtk-cursor-theme-size": size})
    d = os.path.join(HOME, ".icons", "default")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "index.theme"), "w") as f:
        f.write(f"[Icon Theme]\nName=Default\nComment=Default cursor theme (end4pC)\nInherits={theme}\n")
    # Qt / KDE apps read kcminputrc
    if shutil.which("kwriteconfig6"):
        run("kwriteconfig6", "--file", "kcminputrc", "--group", "Mouse", "--key", "cursorTheme", theme)
        run("kwriteconfig6", "--file", "kcminputrc", "--group", "Mouse", "--key", "cursorSize", str(size))
    if os.environ.get("HYPRLAND_INSTANCE_SIGNATURE") and shutil.which("hyprctl"):
        run("hyprctl", "setcursor", theme, str(size))
    if on_niri():
        # niri takes its cursor (and the XCURSOR_* of the programs it starts) from its own config: the end4pC file of Settings -> Niri
        run(NIRICONF, "--set", "cursorTheme", theme, "cursorSize", str(size))
    if on_sway():
        sway_cursor(theme, size)


def set_icons(theme):
    gsettings_set("icon-theme", f"'{theme}'")
    ini_set({"gtk-icon-theme-name": theme})
    qt_set("Appearance", "icon_theme", theme)


def set_font(which, family, size):
    if which == "ui":
        gsettings_set("font-name", f"'{family} {size}'")
        ini_set({"gtk-font-name": f"{family} {size}"})
        qt_set("Fonts", "general", f'"{family},{size},-1,5,50,0,0,0,0,0"')
    else:
        gsettings_set("monospace-font-name", f"'{family} {size}'")
        qt_set("Fonts", "fixed", f'"{family},{size},-1,5,50,0,0,0,0,0"')


def main(argv):
    if len(argv) >= 2 and argv[0] == "--list":
        kind = argv[1]
        items = themes("cursors") if kind == "cursors" else themes("icons") if kind == "icons" else fonts(kind == "mono")
        print("\n".join(items))
    elif argv[:1] == ["--get"]:
        get()
    elif len(argv) >= 4 and argv[:2] == ["--set", "cursor"]:
        set_cursor(argv[2], int(argv[3]))
    elif len(argv) >= 3 and argv[:2] == ["--set", "icons"]:
        set_icons(argv[2])
    elif len(argv) >= 5 and argv[:2] == ["--set", "font"]:
        set_font(argv[2], argv[3], int(argv[4]))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
