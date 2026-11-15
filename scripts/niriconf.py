#!/usr/bin/env python3
"""niri options for the end4pC settings page (Settings -> Niri).

  niriconf.py --dump KEY...                 print "KEY=VALUE" for each option (what is set here, or the default)
  niriconf.py --set KEY VALUE [KEY VALUE]   save and apply (niri reloads its config by itself)
  niriconf.py --reset KEY...                forget the value: niri goes back to what the other files say
  niriconf.py --rules                       the apps that have a rule, one per line
  niriconf.py --rule-get APP                print "FIELD=VALUE" for one app
  niriconf.py --rule-set APP FIELD VALUE    FIELD: float | maximize | blur | opacity | width | height
  niriconf.py --rule-add APP                a rule with nothing set yet
  niriconf.py --rule-remove APP
  niriconf.py --windows                     the app ids of the windows that are open now (without the shell's own)
  niriconf.py --restore                     put the file back as it was before the first change made from here
  niriconf.py --ensure-include              make sure ~/.config/niri/config.kdl includes the file (and that it exists)
  niriconf.py --status                      "ok", or "error: ..." for the last change

The script owns ONE file, ~/.config/niri/end4pC-user.kdl, written atomically. Its second line holds the state as JSON, and the KDL
under it is made from that, so the file is rewritten on every change: edit things from the settings page, not by hand.
niri's config.kdl is only touched to add one include line at its end (a copy is kept as config.kdl.end4pC-backup6).
Every change is checked with `niri validate`; when niri would refuse it, the file goes back to what it was and the status says why.
Before its first change a copy of the file is kept in ~/.local/state/end4pC/niri-baseline.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HOME = os.path.expanduser("~")
NIRI_DIR = os.path.join(HOME, ".config", "niri")
CONFIG = os.path.join(NIRI_DIR, "config.kdl")
USER_FILE = os.path.join(NIRI_DIR, "end4pC-user.kdl")
INCLUDE_LINE = 'include "~/.config/niri/end4pC-user.kdl"'
STATE_DIR = os.path.join(os.environ.get("XDG_STATE_HOME", os.path.join(HOME, ".local", "state")), "end4pC")
BASELINE = os.path.join(STATE_DIR, "niri-baseline")
STATUS = os.path.join(STATE_DIR, "niri-status.txt")
LOOK_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "niri", "end4pC-look.kdl")
HEADER = "// end4pC: made by Settings -> Niri (scripts/niriconf.py). This file is rewritten on every change: edit it from there."
SHELL_WINDOWS = r"^end4pC-(settings|tools)$"  # draw their own corners: the catch-all rounding leaves them alone

# key -> (kind, min, max); the kinds: int (clamped), bool, choice (the allowed values), color (#rrggbb or #rrggbbaa)
KEYS = {
    "gaps": ("int", 0, 40),
    "strut": ("int", 0, 60),
    "ring": ("bool",),
    "ringWidth": ("int", 1, 10),
    "border": ("bool",),
    "borderWidth": ("int", 1, 10),
    "borderActive": ("color",),    # the palette border colours of Settings (empty: the wallpaper ones of end4pC-colors.kdl)
    "borderInactive": ("color",),
    "radius": ("int", 0, 40),
    "shadow": ("bool",),
    "shadowSoftness": ("int", 0, 100),
    "center": ("choice", "never", "always", "on-overflow"),
    "colWidth": ("int", 10, 100),
    "follow": ("bool",),
    "repeatDelay": ("int", 100, 1000),
    "repeatRate": ("int", 5, 100),
    "accel": ("int", -10, 10),
    "anim": ("choice", "off", "fast", "normal", "slow"),
    "cursorTheme": ("name",),      # the pointer theme (Settings -> System -> Cursor): niri takes its cursor from its own config
    "cursorSize": ("int", 8, 128),
}
ANIM_SLOWDOWN = {"fast": "0.5", "slow": "2.0"}  # normal: nothing written
RULE_FIELDS = {"float", "maximize", "blur", "opacity", "width", "height"}
APP_ID = re.compile(r"^[A-Za-z0-9._+-]{1,120}$")


def read(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return None


def write_atomic(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".niriconf-")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)


def set_status(text):
    try:
        os.makedirs(STATE_DIR, exist_ok=True)
        with open(STATUS, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    except OSError:
        pass


def look_defaults():
    """What the shell's own niri/end4pC-look.kdl gives: the values shown while nothing is set here."""
    d = {"gaps": 4, "strut": 1, "radius": 22, "ring": 0, "border": 1, "borderWidth": 1, "ringWidth": 4, "shadow": 0, "shadowSoftness": 30,
         "center": "never", "colWidth": 50, "follow": 0, "repeatDelay": 600, "repeatRate": 25, "accel": 0, "anim": "normal"}
    look = read(LOOK_FILE) or ""
    m = re.search(r"layout\s*\{\s*gaps\s+(\d+)", look)
    if m:
        d["gaps"] = int(m.group(1))
    m = re.search(r"struts\s*\{\s*left\s+(\d+)", look)
    if m:
        d["strut"] = int(m.group(1))
    m = re.search(r"window-rule\s*\{\s*geometry-corner-radius\s+(\d+)", look)
    if m:
        d["radius"] = int(m.group(1))
    return d


# ---- the file: the state in line 2, the KDL made from it
def load_state():
    text = read(USER_FILE) or ""
    for line in text.splitlines():
        if line.startswith("// state: "):
            try:
                st = json.loads(line[len("// state: "):])
                if isinstance(st, dict):
                    st.setdefault("values", {})
                    st.setdefault("rules", {})
                    return st
            except ValueError:
                break
    return {"values": {}, "rules": {}}


def kdl_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def app_regex(app):
    return "^" + re.sub(r"([.+])", r"\\\1", app) + "$"


def render(st):
    v, rules = st["values"], st["rules"]
    out = [HEADER, "// state: " + json.dumps(st, separators=(",", ":"), sort_keys=True), ""]

    layout = []
    if "gaps" in v:
        layout.append("    gaps %d" % v["gaps"])
    if "strut" in v:
        n = v["strut"]
        layout.append("    struts {\n        left %d\n        right %d\n        top %d\n        bottom %d\n    }" % (n, n, n, n))
    if "center" in v:
        layout.append("    center-focused-column %s" % kdl_str(v["center"]))
    if "colWidth" in v:
        layout.append("    default-column-width { proportion %s; }" % ("%.4f" % (v["colWidth"] / 100.0)).rstrip("0").rstrip("."))
    colors = []  # the focus ring follows the border colours, so a ring that is switched on never shows a second colour
    if "borderActive" in v:
        colors.append("        active-color %s" % kdl_str(v["borderActive"]))
    if "borderInactive" in v:
        colors.append("        inactive-color %s" % kdl_str(v["borderInactive"]))
    if "ring" in v or "ringWidth" in v or colors:
        body = []
        if "ring" in v:
            body.append("        %s" % ("on" if v["ring"] else "off"))
        if "ringWidth" in v:
            body.append("        width %d" % v["ringWidth"])
        layout.append("    focus-ring {\n" + "\n".join(body + colors) + "\n    }")
    if "border" in v or "borderWidth" in v or colors:
        body = []
        if "border" in v:
            body.append("        %s" % ("on" if v["border"] else "off"))
        if "borderWidth" in v:
            body.append("        width %d" % v["borderWidth"])
        layout.append("    border {\n" + "\n".join(body + colors) + "\n    }")
    if "shadow" in v or "shadowSoftness" in v:
        body = ["        %s" % ("on" if v.get("shadow", 0) else "off")]
        if "shadowSoftness" in v:
            body.append("        softness %d" % v["shadowSoftness"])
        layout.append("    shadow {\n" + "\n".join(body) + "\n    }")
    if layout:
        out += ["layout {"] + layout + ["}", ""]

    inp = []
    if "follow" in v and v["follow"]:
        inp.append('    focus-follows-mouse max-scroll-amount="0%"')
    kb = []
    if "repeatDelay" in v:
        kb.append("        repeat-delay %d" % v["repeatDelay"])
    if "repeatRate" in v:
        kb.append("        repeat-rate %d" % v["repeatRate"])
    if kb:
        inp.append("    keyboard {\n" + "\n".join(kb) + "\n    }")
    if "accel" in v:
        inp.append("    touchpad {\n        accel-speed %s\n    }\n    mouse {\n        accel-speed %s\n    }" % ((v["accel"] / 10.0,) * 2))
    if inp:
        out += ["input {"] + inp + ["}", ""]

    if "cursorTheme" in v or "cursorSize" in v:
        cur = []
        if "cursorTheme" in v:
            cur.append("    xcursor-theme %s" % kdl_str(v["cursorTheme"]))
        if "cursorSize" in v:
            cur.append("    xcursor-size %d" % v["cursorSize"])
        out += ["cursor {"] + cur + ["}", ""]

    if v.get("anim") == "off":
        out += ["animations {", "    off", "}", ""]
    elif v.get("anim") in ANIM_SLOWDOWN:
        out += ["animations {", "    slowdown %s" % ANIM_SLOWDOWN[v["anim"]], "}", ""]

    if "radius" in v:  # every window but the shell's own two (they have corners of their own)
        out += ["window-rule {", '    exclude app-id=r#"%s"#' % SHELL_WINDOWS, "    geometry-corner-radius %d" % v["radius"],
                "    clip-to-geometry true", "}", ""]

    for app in sorted(rules):
        r = rules[app]
        body = ['    match app-id=r#"%s"#' % app_regex(app)]
        if "float" in r:
            body.append("    open-floating %s" % ("true" if r["float"] else "false"))
        if "maximize" in r:
            body.append("    open-maximized %s" % ("true" if r["maximize"] else "false"))
        if "opacity" in r and r["opacity"] < 100:
            body.append("    opacity %s" % ("%.2f" % (r["opacity"] / 100.0)).rstrip("0").rstrip("."))
        if r.get("width"):
            body.append("    default-column-width { proportion %s; }" % ("%.4f" % (r["width"] / 100.0)).rstrip("0").rstrip("."))
        if r.get("height"):
            body.append("    default-window-height { proportion %s; }" % ("%.4f" % (r["height"] / 100.0)).rstrip("0").rstrip("."))
        if "blur" in r:
            body.append("    background-effect {\n        blur %s\n    }" % ("true" if r["blur"] else "false"))
        out += ["window-rule {"] + body + ["}", ""]
    return "\n".join(out).rstrip("\n") + "\n"


def validate():
    """(True, "") when niri accepts the whole config, else (False, the reason)."""
    try:
        p = subprocess.run(["niri", "validate", "-c", CONFIG], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, "niri validate did not run: %s" % e
    text = re.sub(r"\x1b\[[0-9;]*m", "", p.stdout + p.stderr)
    if p.returncode == 0:
        return True, ""
    lines = [l.strip(" │╭╰─·├▶\t") for l in text.splitlines() if "×" in l or "help:" in l]
    skip = ("error loading config", "failed to parse included config", "error parsing KDL")
    parts = [re.sub(r"^(Error:)?\s*", "", l.replace("×", "")).strip() for l in lines]
    msg = "; ".join(p for p in parts if p and not any(p.startswith(k) for k in skip))
    return False, msg[:300] or "niri refused the config"


def keep_baseline():
    if os.path.isdir(BASELINE):
        return
    os.makedirs(BASELINE, exist_ok=True)
    old = read(USER_FILE)
    if old is None:
        open(os.path.join(BASELINE, "absent"), "w").close()
    else:
        write_atomic(os.path.join(BASELINE, "end4pC-user.kdl"), old)


def touch_config():
    try:
        os.utime(CONFIG, None)  # niri watches the files, this makes sure it looks
    except OSError:
        pass


def ensure_include():
    """The file exists (empty) and config.kdl includes it, once, at the end."""
    if not os.path.exists(USER_FILE):
        write_atomic(USER_FILE, render({"values": {}, "rules": {}}))
    cfg = read(CONFIG)
    if cfg is None:
        return False, "no config.kdl found in ~/.config/niri"
    if re.search(r'^\s*include\s+"[^"]*end4pC-user\.kdl"', cfg, re.M):
        return True, ""
    backup = CONFIG + ".end4pC-backup6"
    if not os.path.exists(backup):
        shutil.copy2(CONFIG, backup)
    write_atomic(CONFIG, cfg.rstrip("\n") + "\n\n// end4pC: what Settings -> Niri changes (it wins over the files included before it)\n" + INCLUDE_LINE + "\n")
    ok, why = validate()
    if not ok:
        write_atomic(CONFIG, cfg)  # not ours to break: put config.kdl back as it was
        return False, why
    return True, ""


def commit(st):
    """Writes the state and keeps it only if niri accepts the result. Returns True on success."""
    ok, why = ensure_include()
    if not ok:
        set_status("error: " + why)
        print("error: " + why)
        return False
    keep_baseline()
    old = read(USER_FILE)
    write_atomic(USER_FILE, render(st))
    ok, why = validate()
    if not ok:
        if old is None:
            os.remove(USER_FILE)
        else:
            write_atomic(USER_FILE, old)
        set_status("error: " + why)
        print("error: " + why)
        return False
    set_status("ok")
    touch_config()
    return True


# ---- values
def parse_value(key, raw):
    spec = KEYS[key]
    if spec[0] == "bool":
        return 1 if raw in ("1", "true", "on", "yes") else 0
    if spec[0] == "int":
        return max(spec[1], min(spec[2], int(float(raw))))
    if spec[0] == "color":
        if not re.fullmatch(r"#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?", raw):
            raise ValueError("%s must be #rrggbb or #rrggbbaa" % key)
        return raw.lower()
    if spec[0] == "name":
        if not re.fullmatch(r"[A-Za-z0-9._+ -]{1,80}", raw):
            raise ValueError("%s has characters that a theme name does not have" % key)
        return raw
    if raw not in spec[1:]:
        raise ValueError("%s must be one of %s" % (key, ", ".join(spec[1:])))
    return raw


def cmd_dump(keys):
    st, d = load_state(), look_defaults()
    for k in keys:
        if k in KEYS:
            print("%s=%s" % (k, st["values"].get(k, d.get(k, ""))))


def cmd_set(args):
    st = load_state()
    for i in range(0, len(args) - 1, 2):
        k = args[i]
        if k not in KEYS:
            print("error: unknown option " + k)
            return 1
        try:
            st["values"][k] = parse_value(k, args[i + 1])
        except ValueError as e:
            print("error: %s" % e)
            return 1
    return 0 if commit(st) else 1


def cmd_reset(keys):
    st = load_state()
    for k in keys:
        st["values"].pop(k, None)
    return 0 if commit(st) else 1


# ---- rules
def check_app(app):
    if not APP_ID.match(app or ""):
        print("error: the app id may only have letters, digits and . _ + -")
        return False
    return True


def cmd_rule_get(app):
    r = load_state()["rules"].get(app)
    if r is None:
        return 1
    for f, default in (("float", 0), ("maximize", 0), ("blur", 0), ("opacity", 100), ("width", 0), ("height", 0)):
        val = r.get(f, default)
        print("%s=%s" % (f, int(val) if isinstance(val, bool) else val))
    return 0


def cmd_rule_set(app, field, raw):
    if field not in RULE_FIELDS:
        print("error: unknown field " + field)
        return 1
    st = load_state()
    if app not in st["rules"]:
        print("error: no rule for " + app)
        return 1
    if field in ("float", "maximize", "blur"):
        st["rules"][app][field] = raw in ("1", "true", "on")
    else:
        lo, hi = (10, 100) if field == "opacity" else (0, 100)
        st["rules"][app][field] = max(lo, min(hi, int(float(raw))))
        if field in ("width", "height") and 0 < st["rules"][app][field] < 10:
            st["rules"][app][field] = 10
    return 0 if commit(st) else 1


def cmd_rule_add(app):
    if not check_app(app):
        return 1
    st = load_state()
    st["rules"].setdefault(app, {})
    return 0 if commit(st) else 1


def cmd_rule_remove(app):
    st = load_state()
    st["rules"].pop(app, None)
    return 0 if commit(st) else 1


def cmd_windows():
    try:
        p = subprocess.run(["niri", "msg", "--json", "windows"], capture_output=True, text=True, timeout=5)
        wins = json.loads(p.stdout or "[]")
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return 0
    seen = []
    for w in wins:
        app = w.get("app_id") or ""
        if app and not app.startswith("end4pC-") and APP_ID.match(app) and app not in seen:
            seen.append(app)
    for app in sorted(seen):
        print(app)
    return 0


def cmd_restore():
    if not os.path.isdir(BASELINE):
        print("nothing to restore")
        return 0
    saved = read(os.path.join(BASELINE, "end4pC-user.kdl"))
    write_atomic(USER_FILE, saved if saved is not None else render({"values": {}, "rules": {}}))
    ok, why = validate()
    if not ok:
        set_status("error: " + why)
        print("error: " + why)
        return 1
    shutil.rmtree(BASELINE, ignore_errors=True)
    set_status("ok")
    touch_config()
    return 0


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    a, rest = argv[0], argv[1:]
    if a == "--dump":
        cmd_dump(rest)
    elif a == "--set":
        return cmd_set(rest)
    elif a == "--reset":
        return cmd_reset(rest)
    elif a == "--rules":
        for app in sorted(load_state()["rules"]):
            print(app)
    elif a == "--rule-get" and len(rest) == 1:
        return cmd_rule_get(rest[0])
    elif a == "--rule-set" and len(rest) == 3:
        return cmd_rule_set(*rest)
    elif a == "--rule-add" and len(rest) == 1:
        return cmd_rule_add(rest[0])
    elif a == "--rule-remove" and len(rest) == 1:
        return cmd_rule_remove(rest[0])
    elif a == "--windows":
        return cmd_windows()
    elif a == "--restore":
        return cmd_restore()
    elif a == "--ensure-include":
        ok, why = ensure_include()
        if not ok:
            print("error: " + why)
        return 0 if ok else 1
    elif a == "--status":
        print((read(STATUS) or "ok").strip())
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
