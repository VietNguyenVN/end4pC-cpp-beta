#!/usr/bin/env python3
"""Templates: the shell's palette written for other programs (kitty for now).

  templates.py            writes every template that is switched on in Settings -> Appearance -> Templates
  templates.py kitty      writes that one (kitty, gtk or kde), whatever the switch says
  templates.py --all      writes all of them (a dots installer can call this once the palette exists)
  templates.py --manifest prints, as JSON, every template: its config switch, the files it writes and what else it touches

The folders follow XDG_* (and each file can be moved with END4PC_KITTY_THEME, END4PC_KITTY_CONF, END4PC_GTK3_CSS, END4PC_GTK4_CSS,
END4PC_KDE_SCHEME_DIR), so an installer can point every template where its own layout wants it.

Each template is one function in TEMPLATES, switched on by a `template<Name>` key of config.json. To add one: write the function, add it to
TEMPLATES, and add its switch to the Templates chip of the Settings (src/settings.cpp). The palette comes from the colors.json the shell
generates (~/.local/state/end4pC/generated/colors.json), so every template follows the wallpaper, the mode and the themes.
"""
import json
import math
import os
import signal
import subprocess
import sys

HOME = os.path.expanduser("~")
DATA = os.environ.get("XDG_DATA_HOME") or HOME + "/.local/share"
STATE = os.environ.get("XDG_STATE_HOME") or HOME + "/.local/state"
CONFIG = os.environ.get("XDG_CONFIG_HOME") or HOME + "/.config"


def read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


# ---------------------------------------------------------------- colour helpers
# The terminal colours follow the original end4-pC generator (scripts/colors/generate_colors_material.py of that shell): every one of the 16
# base colours has its hue turned towards the palette's primary colour (`harmony`, at most `threshold` degrees), then its tone is raised
# (`boost`) so the colours read on the dark background. That generator works in HCT; here the same steps are done in CIELAB LCh (HCT's tone
# is CIELAB's L*), which gives the same colours to within a few units, and needs no library.
HARMONY, THRESHOLD, FG_BOOST = 0.6, 100.0, 0.35  # what the original uses (illogical-impulse's config.json has harmony 0.6)

_M = [[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]]
_MI = [[3.2404542, -1.5371385, -0.4985314], [-0.9692660, 1.8760108, 0.0415560], [0.0556434, -0.2040259, 1.0572252]]
_W = (0.95047, 1.0, 1.08883)


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


def hexs(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v * 255))) for v in c)


def _lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _gamma(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def _f(t):
    return t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116


def _finv(t):
    return t ** 3 if t ** 3 > 216 / 24389 else (116 * t - 16) / (24389 / 27)


def to_lch(color):
    r, g, b = (_lin(v) for v in rgb(color))
    x, y, z = (_M[i][0] * r + _M[i][1] * g + _M[i][2] * b for i in range(3))
    fx, fy, fz = _f(x / _W[0]), _f(y / _W[1]), _f(z / _W[2])
    a, bb = 500 * (fx - fy), 200 * (fy - fz)
    return 116 * fy - 16, math.hypot(a, bb), math.degrees(math.atan2(bb, a)) % 360


def from_lch(lightness, chroma, hue):
    """LCh -> hex; a colour outside the sRGB gamut loses chroma until it fits (as HCT does)."""
    lightness = max(0.0, min(100.0, lightness))
    lin = (0, 0, 0)
    for _ in range(80):
        a, b = chroma * math.cos(math.radians(hue)), chroma * math.sin(math.radians(hue))
        fy = (lightness + 16) / 116
        x, y, z = _finv(fy + a / 500) * _W[0], _finv(fy) * _W[1], _finv(fy - b / 200) * _W[2]
        lin = tuple(_MI[i][0] * x + _MI[i][1] * y + _MI[i][2] * z for i in range(3))
        if all(-1e-4 <= v <= 1 + 1e-4 for v in lin):
            break
        chroma *= 0.95
    return hexs(tuple(_gamma(max(0.0, min(1.0, v))) for v in lin))


def harmonize(color, source, harmony=HARMONY, threshold=THRESHOLD, tone=1.0, chroma=1.0):
    """The hue of `color` turned towards the hue of `source`; its tone and chroma multiplied by `tone` and `chroma`."""
    lightness, c, hue = to_lch(color)
    diff = ((to_lch(source)[2] - hue) + 540) % 360 - 180  # the shortest way round, -180..180
    shift = min(abs(diff) * harmony, threshold) * (1 if diff >= 0 else -1)
    return from_lch(lightness * tone, c * chroma, (hue + shift) % 360)


# The 16 terminal colours before they are pulled towards the palette (the same bases the original end4-pC uses).
TERM_DARK = ["#282828", "#CC241D", "#98971A", "#D79921", "#458588", "#B16286", "#689D6A", "#A89984",
             "#928374", "#FB4934", "#B8BB26", "#FABD2F", "#83A598", "#D3869B", "#8EC07C", "#EBDBB2"]
TERM_LIGHT = ["#FDF9F3", "#FF6188", "#A9DC76", "#FC9867", "#FFD866", "#F47FD4", "#78DCE8", "#333034",
              "#121212", "#FF6188", "#A9DC76", "#FC9867", "#FFD866", "#F47FD4", "#78DCE8", "#333034"]


def terminal_colors(p, dark):
    base = TERM_DARK if dark else TERM_LIGHT
    tone = 1 + FG_BOOST * (1 if dark else -1)  # lighter on a dark background, darker on a light one
    term = [harmonize(c, p["primary"], tone=tone) for c in base]
    # the terminal's black is the shell's own surface (a little more colourful), and its brightest white the text colour with a lot of chroma
    term[0] = harmonize(p.get("surface_container_low", p["background"]), p["primary"], harmony=0, tone=0.95, chroma=1.2)
    term[15] = harmonize(p["on_surface"], p["primary"], harmony=0, chroma=3)
    return term


def status_colors(p, dark):
    """Positive, neutral, link and visited colours (not in the palette): bases pulled towards the primary colour."""
    base = ("#00e479", "#fcb38a", "#8fc9fc", "#ebb2ff") if dark else ("#00873d", "#b35900", "#005ac1", "#8e24aa")
    return [harmonize(c, p["primary"], 0.15, 20) for c in base]  # (only a little: a link must stay blue, a warning orange)


# ---------------------------------------------------------------- kitty

# Where the original end4-pC (Quickshell) setup keeps the kitty colours, and where its kitty.conf includes them from: the same file,
# so a machine that already has that setup keeps working, and a new one gets the folder and the include line.
KITTY_THEME = os.environ.get("END4PC_KITTY_THEME") or STATE + "/quickshell/user/generated/terminal/kitty-theme.conf"
KITTY_CONF = os.environ.get("END4PC_KITTY_CONF") or CONFIG + "/kitty/kitty.conf"
KITTY_SEQUENCES = os.path.join(os.path.dirname(KITTY_THEME), "sequences.txt")  # the colours as escape sequences: the original setup's fish config prints this file at every shell start
KITTY_INCLUDE = "include ~/.local/state/quickshell/user/generated/terminal/kitty-theme.conf"


def write_sequences(term):
    """The same colours as terminal escape sequences (sequences.txt). Shells of the original end4-pC setup print this file when they start, and it
    would paint the old colours over kitty's own; the open terminals get it at once, as that setup does."""
    e, st = "\x1b", "\x1b\\"
    seq = "".join("%s]4;%d;%s%s" % (e, i, term[i], st) + ("%s]1;0;%s%s" % (e, term[0], st) if i == 0 else "") for i in range(16))
    seq += "".join("%s]%s%s" % (e, part, st) for part in (
        "10;" + term[7], "11;[100]" + term[0], "12;" + term[7], "13;" + term[7], "17;" + term[7], "19;" + term[0],
        "4;232;" + term[7], "4;256;" + term[7], "708;[100]" + term[0], "11;" + term[0]))
    os.makedirs(os.path.dirname(KITTY_SEQUENCES), exist_ok=True)
    with open(KITTY_SEQUENCES, "w", encoding="utf-8") as f:
        f.write(seq)
    try:  # the terminals that are open now (only ours, and never waiting for one that does not read)
        pts = os.path.join("/dev", "pts")
        for name in os.listdir(pts):
            path = os.path.join(pts, name)
            if name.isdigit() and os.stat(path).st_uid == os.getuid():
                try:
                    fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK | os.O_NOCTTY)
                    try:
                        os.write(fd, seq.encode())
                    finally:
                        os.close(fd)
                except OSError:
                    pass
    except OSError:
        pass


def apply_kitty(p, dark):
    term = terminal_colors(p, dark)
    lines = ["background            " + term[0], ""]
    lines += ["color%-2d               %s" % (i, term[i]) for i in range(16)]
    lines += ["", "cursor                " + term[7], "", "foreground            " + term[7], "",
              "selection_background  " + p["on_secondary_container"], "selection_foreground  " + p["secondary_container"], "",
              "# Colours for programs that use the high part of the 256 palette (starship and friends)"]
    for n, k in [(255, "primary"), (254, "primary_container"), (253, "secondary"), (252, "secondary_container"), (251, "tertiary"),
                 (250, "tertiary_container"), (249, "error"), (248, "error_container"), (232, "on_primary"), (233, "on_primary_container"),
                 (234, "on_secondary"), (235, "on_secondary_container"), (236, "on_tertiary"), (237, "on_tertiary_container"),
                 (238, "on_error"), (239, "on_error_container"), (240, "on_primary"), (243, "primary"), (244, "error"), (245, "outline_variant")]:
        lines.append("color%-3d              %s" % (n, p[k]))
    os.makedirs(os.path.dirname(KITTY_THEME), exist_ok=True)
    with open(KITTY_THEME, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    write_sequences(term)
    # kitty has to read the file: add the include line when kitty.conf does not have it yet (nothing else in it is touched)
    os.makedirs(os.path.dirname(KITTY_CONF), exist_ok=True)
    text = ""
    try:
        with open(KITTY_CONF, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        pass
    if "kitty-theme.conf" not in text:
        with open(KITTY_CONF, "a", encoding="utf-8") as f:
            f.write(("\n" if text and not text.endswith("\n") else "") + "\n# Colours of the shell (end4pC > Settings > Appearance > Templates)\n" + KITTY_INCLUDE + "\n")
    # running kitties take the colours at once: through remote control when it is on, else the signal that makes them read their config
    try:
        pids = subprocess.run(["pgrep", "-x", "kitty"], capture_output=True, text=True).stdout.split()
    except OSError:
        pids = []
    for pid in pids:
        ok = subprocess.run(["kitten", "@", "--to", "unix:@kitty-" + pid, "set-colors", "--all", "--configured", KITTY_THEME],
                            capture_output=True).returncode == 0 if shutil_which("kitten") else False
        if not ok:
            try:
                os.kill(int(pid), signal.SIGUSR1)
            except (OSError, ValueError):
                pass


def shutil_which(name):
    for d in os.environ.get("PATH", "").split(os.pathsep):
        if os.access(os.path.join(d, name), os.X_OK):
            return True
    return False


# ---------------------------------------------------------------- shared by gtk and kde

MARK = "Generated by end4pC"  # a file with this line is ours: anything else is backed up once before it is replaced


def write_ours(path, text, comment):
    """Writes `text` to `path`; a file that was not made by this script is first copied to <path>.end4pC-backup (only once)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    try:
        with open(path, encoding="utf-8") as f:
            old = f.read()
    except OSError:
        old = None
    if old is not None and MARK not in old and not os.path.exists(path + ".end4pC-backup"):
        with open(path + ".end4pC-backup", "w", encoding="utf-8") as f:
            f.write(old)
    with open(path, "w", encoding="utf-8") as f:
        f.write(comment.replace("%s", MARK) + text)


# ---------------------------------------------------------------- gtk

GTK3_CSS = os.environ.get("END4PC_GTK3_CSS") or CONFIG + "/gtk-3.0/gtk.css"
GTK4_CSS = os.environ.get("END4PC_GTK4_CSS") or CONFIG + "/gtk-4.0/gtk.css"


def apply_gtk(p, dark):
    good = status_colors(p, dark)[0]
    named = [
        ("accent_color", p["primary"]), ("accent_bg_color", p["primary"]), ("accent_fg_color", p["on_primary"]),
        ("destructive_bg_color", p["error_container"]), ("destructive_fg_color", p["on_error_container"]), ("destructive_color", p["error"]),
        ("success_bg_color", good), ("success_fg_color", p["on_primary"]), ("success_color", good),
        ("window_bg_color", p["background"]), ("window_fg_color", p["on_background"]),
        ("headerbar_bg_color", p["surface_container"]), ("headerbar_fg_color", p["on_surface"]), ("headerbar_backdrop_color", p["surface_container"]),
        ("card_bg_color", p["surface_container"]), ("card_fg_color", p["on_surface"]),
        ("view_bg_color", p["surface_container_lowest"]), ("view_fg_color", p["on_surface"]),
        ("popover_bg_color", p["surface_container_highest"]), ("popover_fg_color", p["on_surface"]),
        ("dialog_bg_color", p["surface_container_high"]), ("dialog_fg_color", p["on_surface"]),
        ("sidebar_bg_color", p["surface_container_low"]), ("sidebar_fg_color", p["on_surface"]), ("sidebar_backdrop_color", p["surface_container_low"]),
        ("sidebar_border_color", p["surface_container_low"]), ("sidebar_row_active_bg_color", p["secondary_container"]),
        ("sidebar_row_active_fg_color", p["on_secondary_container"]),
        ("secondary_sidebar_bg_color", p["surface_container_low"]), ("secondary_sidebar_fg_color", p["on_surface_variant"]),
        ("secondary_sidebar_backdrop_color", p["surface_container_low"]),
        ("thumbnail_bg_color", p["surface_container_high"]), ("thumbnail_fg_color", p["on_surface"]),
        ("overview_bg_color", p["surface_container_lowest"]), ("overview_fg_color", p["on_surface"]),
        ("inverse_on_surface", p["inverse_on_surface"]), ("inverse_primary", p["inverse_primary"]), ("inverse_surface", p["inverse_surface"]),
        ("surface_container_highest", p["surface_container_highest"]), ("surface_container_high", p["surface_container_high"]),
        ("on_surface_variant", p["on_surface_variant"]), ("surface_variant", p["surface_variant"]), ("outline", p["outline"]),
    ]
    body = "".join("@define-color %s %s;\n" % (k, v) for k, v in named)
    comment = "/*\n * %s (scripts/templates.py): the shell's palette as GTK named colours.\n */\n\n"
    for path in (GTK3_CSS, GTK4_CSS):
        write_ours(path, body, comment)


# ---------------------------------------------------------------- kde

KDE_SCHEME_DIR = os.environ.get("END4PC_KDE_SCHEME_DIR") or DATA + "/color-schemes"
KDE_NAME = "End4pC"  # (the scheme alternates between End4pC and End4pC2: Plasma does nothing when the scheme that is asked for is the one in use)


def kde_current():
    try:
        with open(CONFIG + "/kdeglobals", encoding="utf-8") as f:
            for line in f:
                if line.startswith("ColorScheme="):
                    return line.strip().split("=", 1)[1]
    except OSError:
        pass
    return ""


def apply_kde(p, dark):
    positive, neutral, link, visited = status_colors(p, dark)

    def section(name, bg, alt, fg=None, fg_inactive=None, link_c=None, visited_c=None, deco=None):
        fg = fg or p["on_surface"]
        rows = [("BackgroundAlternate", alt), ("BackgroundNormal", bg), ("DecorationFocus", deco or p["primary"]), ("DecorationHover", deco or p["primary"]),
                ("ForegroundActive", fg), ("ForegroundInactive", fg_inactive or p["on_surface_variant"]), ("ForegroundLink", link_c or link),
                ("ForegroundNegative", p["error"]), ("ForegroundNeutral", neutral), ("ForegroundNormal", fg), ("ForegroundPositive", positive),
                ("ForegroundVisited", visited_c or visited)]
        return "[%s]\n" % name + "".join("%s=%s\n" % r for r in rows) + "\n"

    def argb(c):
        return "#ff" + c.lstrip("#")

    def triple(c):
        return ",".join(str(round(v * 255)) for v in rgb(c))

    on_sel = p["on_primary"]
    text = "[ColorEffects:Disabled]\nColor=%s\nColorAmount=0.5\nColorEffect=3\nContrastAmount=0\nContrastEffect=0\nIntensityAmount=0\nIntensityEffect=0\n\n" % p["surface_container"]
    text += "[ColorEffects:Inactive]\nChangeSelectionColor=true\nColor=%s\nColorAmount=0.025\nColorEffect=0\nContrastAmount=0.1\nContrastEffect=0\nEnable=true\nIntensityAmount=0\nIntensityEffect=0\n\n" % p["surface_container_lowest"]
    text += section("Colors:Button", p["surface_container_high"], p["surface_container_highest"])
    text += section("Colors:Complementary", p["surface_container"], p["surface_dim"])
    text += section("Colors:Header", p["surface_container_low"], p["surface_container"])
    text += "[Colors:Header][Inactive]\nBackgroundAlternate=%s\nBackgroundNormal=%s\n\n" % (p["surface_container_low"], p["surface_container_low"])
    text += section("Colors:Selection", p["primary"], p["primary"], fg=on_sel, fg_inactive=on_sel, link_c=on_sel, visited_c=on_sel, deco=p["primary"])
    text += section("Colors:Tooltip", p["inverse_surface"], p["inverse_surface"], fg=p["inverse_on_surface"], fg_inactive=p["inverse_on_surface"], link_c=p["inverse_primary"])
    text += section("Colors:View", p["background"], p["surface_container_low"])
    text += section("Colors:Window", p["surface_container"], p["surface_container_high"])
    name = KDE_NAME + "2" if kde_current() == KDE_NAME else KDE_NAME  # the one that is not in use now: applying it makes every app follow
    text += "[General]\nColorScheme=%s\nName=End4pC %s%s\nshadeSortColumn=true\n\n[KDE]\ncontrast=4\n\n" % (name, "dark" if dark else "light", " 2" if name != KDE_NAME else "")
    text += "[WM]\nactiveBackground=%s\nactiveBlend=%s\nactiveForeground=%s\ninactiveBackground=%s\ninactiveBlend=%s\ninactiveForeground=%s\n" % (
        argb(p["surface_container"]), triple(p["on_surface"]), p["on_surface"], argb(p["surface_container_low"]), triple(p["on_surface_variant"]), p["on_surface_variant"])
    path = KDE_SCHEME_DIR + "/" + name + ".colors"
    os.makedirs(KDE_SCHEME_DIR, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:  # (the name is ours: a file with this name is always ours, nothing to back up)
        f.write(text)
    # take it: Plasma reads the scheme by its name; applying it again makes the running apps follow the new colours
    if shutil_which("plasma-apply-colorscheme") and not os.environ.get("END4PC_NO_APPLY"):
        subprocess.run(["plasma-apply-colorscheme", name], capture_output=True)



# What each template is, for Settings and for an installer: id -> (config switch, what it writes, what else it touches).
MANIFEST = {
    "kitty": {"switch": "templateKitty", "writes": [KITTY_THEME, KITTY_SEQUENCES], "touches": [KITTY_CONF + " (one include line, if missing)", "running kitty windows"]},
    "gtk": {"switch": "templateGtk", "writes": [GTK3_CSS, GTK4_CSS], "touches": ["the old file, copied once to <file>.end4pC-backup"]},
    "kde": {"switch": "templateKde", "writes": [KDE_SCHEME_DIR + "/" + KDE_NAME + ".colors", KDE_SCHEME_DIR + "/" + KDE_NAME + "2.colors (they alternate)"], "touches": ["plasma-apply-colorscheme " + KDE_NAME + " / " + KDE_NAME + "2"]},
}
TEMPLATES = {"kitty": apply_kitty, "gtk": apply_gtk, "kde": apply_kde}



def main():
    args = sys.argv[1:]
    if args == ["--manifest"]:
        print(json.dumps(MANIFEST, indent=2))
        return 0
    cfg = read_json(DATA + "/end4pC/config.json")
    palette = read_json(STATE + "/end4pC/generated/colors.json")
    if "primary" not in palette:
        return 1
    dark = cfg.get("colorMode", "dark") != "light"
    only = args[0] if args and not args[0].startswith("--") else None
    everything = "--all" in args
    for name, fn in TEMPLATES.items():
        if everything or only == name or (only is None and cfg.get(MANIFEST[name]["switch"], 0)):
            fn(palette, dark)
    return 0


if __name__ == "__main__":
    sys.exit(main())
