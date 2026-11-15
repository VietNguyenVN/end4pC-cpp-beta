#!/usr/bin/env python3
"""Hyprland options for the end4pC settings page.

  hyprconf.py --dump KEY...              print "KEY=VALUE" for each option, as Hyprland has it now
  hyprconf.py --set KEY VALUE ...        save to the overrides file and apply it right away
  hyprconf.py --reset KEY ...            remove the line again (the option goes back to the default on the next reload)
  hyprconf.py --anim-preset NAME         write the animations file (fast | normal | niri | off)
  hyprconf.py --restore                  put the files back as they were before the first change made from here
  hyprconf.py --idle-get [FILE]          print lock=SECONDS, screenOff=..., suspend=... (0 = never) from hypridle.conf
  hyprconf.py --idle NAME SECONDS [FILE] set one of lock | screenOff | suspend (0 removes it) and restart hypridle
  hyprconf.py --monitors                 print one line per screen: name|mode|x|y|scale|transform|disabled|bitdepth|cm|modes
  hyprconf.py --monitor NAME --set FIELD VALUE ...   change a screen (mode, position, scale, transform, bitdepth, cm, disabled)

Before its first change this script keeps a copy of both files in ~/.local/state/end4pC/hypr-baseline.

The file is the same one end4-pC writes: ~/.config/hypr/hyprland/shellOverrides/main.lua (animations.lua for presets).
If it does not exist it is created, and the shell's own hyprland/end4pC.lua loads it when hyprland.lua does not.
KEY is Hyprland's own "category:option" (e.g. decoration:blur:size).
"""
import json
import os
import subprocess
import sys
import tempfile

HOME = os.path.expanduser("~")
OVERRIDES = HOME + "/.config/hypr/hyprland/shellOverrides/main.lua"
ANIMATIONS = HOME + "/.config/hypr/hyprland/shellOverrides/animations.lua"

BOOL_KEYS = {
    "decoration:blur:enabled", "decoration:shadow:enabled", "decoration:dim_inactive", "animations:enabled",
    "general:resize_on_border", "general:allow_tearing",
    "input:numlock_by_default", "input:force_no_accel", "input:left_handed", "input:touchpad:natural_scroll",
    "input:touchpad:disable_while_typing", "input:touchpad:clickfinger_behavior", "input:touchpad:tap-to-click",
    "input:touchpad:tap-and-drag", "input:touchpad:middle_button_emulation",
    "misc:focus_on_activate", "misc:disable_hyprland_logo", "misc:disable_splash_rendering",
    "misc:mouse_move_enables_dpms", "misc:key_press_enables_dpms", "misc:animate_manual_resizes",
    "misc:enable_swallow", "misc:middle_click_paste", "misc:close_special_on_empty",
    "cursor:hide_on_key_press", "dwindle:preserve_split", "dwindle:smart_split", "xwayland:force_zero_scaling",
    "scrolling:fullscreen_on_one_column", "binds:workspace_back_and_forth", "binds:allow_workspace_cycles", "group:auto_group",
}

ANIM_PRESETS = {
    "fast": """\
hl.curve("pc_wobble", { type = "bezier", points = { {0.15, 1.15}, {0.35, 1.0}  } })
hl.curve("pc_decel",  { type = "bezier", points = { {0.05, 0.9},  {0.1,  1.05} } })
hl.curve("pc_accel",  { type = "bezier", points = { {0.3,  0},    {0.8,  0.15} } })
hl.animation({ leaf = "windowsIn",           enabled = true, speed = 5, bezier = "pc_wobble", style = "slide"     })
hl.animation({ leaf = "windowsOut",          enabled = true, speed = 5, bezier = "pc_accel",  style = "slide"     })
hl.animation({ leaf = "windowsMove",         enabled = true, speed = 5, bezier = "pc_decel",  style = "slide"     })
hl.animation({ leaf = "fadeIn",              enabled = true, speed = 4, bezier = "pc_decel"                       })
hl.animation({ leaf = "fadeOut",             enabled = true, speed = 4, bezier = "pc_accel"                       })
hl.animation({ leaf = "layersIn",            enabled = true, speed = 4, bezier = "pc_decel",  style = "slide"     })
hl.animation({ leaf = "layersOut",           enabled = true, speed = 4, bezier = "pc_accel",  style = "slide"     })
hl.animation({ leaf = "workspaces",          enabled = true, speed = 6, bezier = "pc_decel",  style = "slide"     })
hl.animation({ leaf = "specialWorkspaceIn",  enabled = true, speed = 2, bezier = "pc_wobble", style = "slidevert" })
hl.animation({ leaf = "specialWorkspaceOut", enabled = true, speed = 2, bezier = "pc_accel",  style = "slidevert" })
""",
    "normal": """\
hl.curve("emphasizedDecel", { type = "bezier", points = { {0.05, 0.7},  {0.1,  1}    } })
hl.curve("emphasizedAccel", { type = "bezier", points = { {0.3,  0},    {0.8,  0.15} } })
hl.curve("menu_decel",      { type = "bezier", points = { {0.1,  1},    {0,    1}    } })
hl.curve("menu_accel",      { type = "bezier", points = { {0.52, 0.03}, {0.72, 0.08} } })
hl.curve("stall",           { type = "bezier", points = { {1,    -0.1}, {0.7,  0.85} } })
hl.animation({ leaf = "windowsIn",           enabled = true, speed = 3,   bezier = "emphasizedDecel", style = "popin 80%" })
hl.animation({ leaf = "windowsOut",          enabled = true, speed = 2,   bezier = "emphasizedDecel", style = "popin 90%" })
hl.animation({ leaf = "windowsMove",         enabled = true, speed = 3,   bezier = "emphasizedDecel", style = "slide"     })
hl.animation({ leaf = "fadeIn",              enabled = true, speed = 3,   bezier = "emphasizedDecel"  })
hl.animation({ leaf = "fadeOut",             enabled = true, speed = 2,   bezier = "emphasizedDecel"  })
hl.animation({ leaf = "border",              enabled = true, speed = 10,  bezier = "emphasizedDecel"  })
hl.animation({ leaf = "layersIn",            enabled = true, speed = 2.7, bezier = "emphasizedDecel", style = "popin 93%" })
hl.animation({ leaf = "layersOut",           enabled = true, speed = 2.4, bezier = "menu_accel",      style = "popin 94%" })
hl.animation({ leaf = "fadeLayersIn",        enabled = true, speed = 0.5, bezier = "menu_decel"       })
hl.animation({ leaf = "fadeLayersOut",       enabled = true, speed = 2.7, bezier = "stall"            })
hl.animation({ leaf = "workspaces",          enabled = true, speed = 7,   bezier = "menu_decel",      style = "slide"     })
hl.animation({ leaf = "specialWorkspaceIn",  enabled = true, speed = 2.8, bezier = "emphasizedDecel", style = "slidevert" })
hl.animation({ leaf = "specialWorkspaceOut", enabled = true, speed = 1.2, bezier = "emphasizedAccel", style = "slidevert" })
""",
    "niri": """\
hl.curve("niri_wobble", { type = "bezier", points = { {0.15, 1.15}, {0.35, 1.0}  } })
hl.curve("niri_decel",  { type = "bezier", points = { {0.05, 0.9},  {0.1,  1.05} } })
hl.curve("niri_accel",  { type = "bezier", points = { {0.3,  0},    {0.8,  0.15} } })
hl.animation({ leaf = "windowsIn",           enabled = true, speed = 5, bezier = "niri_wobble", style = "slide"     })
hl.animation({ leaf = "windowsOut",          enabled = true, speed = 5, bezier = "niri_accel",  style = "slide"     })
hl.animation({ leaf = "windowsMove",         enabled = true, speed = 5, bezier = "niri_decel",  style = "slide"     })
hl.animation({ leaf = "fadeIn",              enabled = true, speed = 4, bezier = "niri_decel"                       })
hl.animation({ leaf = "fadeOut",             enabled = true, speed = 4, bezier = "niri_accel"                       })
hl.animation({ leaf = "layersIn",            enabled = true, speed = 4, bezier = "niri_decel",  style = "slide"     })
hl.animation({ leaf = "layersOut",           enabled = true, speed = 4, bezier = "niri_accel",  style = "slide"     })
hl.animation({ leaf = "workspaces",          enabled = true, speed = 6, bezier = "niri_decel",  style = "slidevert" })
hl.animation({ leaf = "specialWorkspaceIn",  enabled = true, speed = 4, bezier = "niri_wobble", style = "slidevert" })
hl.animation({ leaf = "specialWorkspaceOut", enabled = true, speed = 4, bezier = "niri_accel",  style = "slidevert" })
""",
}


def lua_value(key, value):
    if key in BOOL_KEYS:
        return "false" if value in ("0", "false", "") else "true"
    try:
        return str(int(value))
    except ValueError:
        pass
    try:
        return str(float(value))
    except ValueError:
        pass
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parts_of(key):
    return key.replace("-", "_").replace(":", ".").split(".")


def lua_line(key, value):
    parts = parts_of(key)
    inner = "{ %s = %s }" % (parts[-1], lua_value(key, value))
    for part in reversed(parts[:-1]):
        inner = "{ %s = %s }" % (part, inner)
    return "hl.config(%s)\n" % inner


def marker(key):  # Text that identifies the line which sets this key, without its value
    parts = parts_of(key)
    head = " = { ".join(parts[:-1])
    return (head + " = { " + parts[-1] + " =") if head else (parts[-1] + " =")


MONITORS = HOME + "/.config/hypr/monitors.lua"
BASELINE = HOME + "/.local/state/end4pC/hypr-baseline"


def snapshot():  # Keep the user's own setup once per file, before anything here changes it
    os.makedirs(BASELINE, exist_ok=True)
    for path in (OVERRIDES, ANIMATIONS, MONITORS):
        name = os.path.basename(path)
        if os.path.exists(BASELINE + "/" + name) or os.path.exists(BASELINE + "/" + name + ".absent"):
            continue
        if os.path.exists(path):
            with open(path) as f, open(BASELINE + "/" + name, "w") as o:
                o.write(f.read())
        else:
            open(BASELINE + "/" + name + ".absent", "w").close()


def restore():
    if not os.path.isdir(BASELINE):
        return
    for path in (OVERRIDES, ANIMATIONS, MONITORS, HYPRIDLE):
        name = os.path.basename(path)
        if os.path.exists(BASELINE + "/" + name):
            with open(BASELINE + "/" + name) as f:
                write_atomic(path, f.read())
            if path == HYPRIDLE:
                idle_restart()
        elif os.path.exists(BASELINE + "/" + name + ".absent") and os.path.exists(path):  # The file did not exist before, so it is removed again
            os.remove(path)
        # A file without a backup copy was never changed here, so it is left as it is
    subprocess.run(["hyprctl", "reload"], capture_output=True)
    import time
    time.sleep(0.8)


def write_atomic(path, content):
    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=folder)
    with os.fdopen(fd, "w") as f:
        f.write(content)
    if os.path.exists(path):
        os.chmod(tmp, os.stat(path).st_mode)
    os.replace(tmp, path)


def edit(sets, resets):
    snapshot()
    try:
        with open(OVERRIDES) as f:
            lines = f.readlines()
    except FileNotFoundError:
        lines = []
    keys = list(dict(sets)) + list(resets)
    out, done = [], set()
    for line in lines:
        hit = next((k for k in keys if marker(k) in line), None)
        if hit is None:
            out.append(line)
        elif hit in resets:
            continue
        else:
            out.append(lua_line(hit, dict(sets)[hit]))
            done.add(hit)
    for k, v in sets:
        if k not in done:
            out.append(lua_line(k, v))
    write_atomic(OVERRIDES, "".join(out))
    for k, v in sets:  # Apply the values live, without waiting for a reload
        subprocess.run(["hyprctl", "eval", lua_line(k, v).strip()], capture_output=True)


def dump(keys):
    for key in keys:
        try:
            out = subprocess.run(["hyprctl", "getoption", key, "-j"], capture_output=True, text=True, timeout=3).stdout
            d = json.loads(out)
        except Exception:
            continue
        for t in ("bool", "int", "float", "str", "css"):  # css values are gaps, in the form "top right bottom left"
            if t in d:
                v = d[t]
                if t == "css":
                    v = str(v).split()[0] if str(v).split() else 0
                if t == "str" and v == "[[EMPTY]]":
                    v = ""
                if isinstance(v, bool):
                    v = int(v)
                print("%s=%s" % (key, v))
                break



# ---------------------------------------------------------------- screens (monitors.lua)

import re

MON_STRING = {"output", "mode", "position", "cm", "mirror"}
MON_BOOL = {"disabled"}
MON_ORDER = ["output", "mode", "position", "scale", "transform", "bitdepth", "cm", "disabled", "mirror", "vrr"]


def mon_read():
    """Every hl.monitor({...}) in the file, one dict per output (a later block overrides an earlier one).
    Anything that is not a block is ignored, so a file with stray lines is read as far as it makes sense."""
    try:
        text = open(MONITORS).read()
    except FileNotFoundError:
        return {}
    out = {}
    for block in re.finditer(r"hl\.monitor\s*\(\s*\{(.*?)\}\s*\)", text, re.S):
        fields = {}
        for m in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*("(?:[^"\\]|\\.)*"|[^,\n}]+)', block.group(1)):
            raw = m.group(2).strip()
            fields[m.group(1)] = raw[1:-1] if raw.startswith('"') else raw
        if "output" in fields:
            out.setdefault(fields["output"], {}).update(fields)
    return out


def mon_lua(fields):
    parts = []
    for key in MON_ORDER + [k for k in fields if k not in MON_ORDER]:
        if key not in fields:
            continue
        v = str(fields[key])
        if key in MON_STRING:
            parts.append('%s = "%s"' % (key, v))
        elif key in MON_BOOL:
            parts.append("%s = %s" % (key, "true" if v in ("1", "true", "True") else "false"))
        else:
            parts.append("%s = %s" % (key, v))
    return "hl.monitor({ " + ", ".join(parts) + " })"


def mon_write(mons):
    write_atomic(MONITORS, "".join(mon_lua(f) + "\n" for f in mons.values()))


def hypr_monitors():
    try:
        return json.loads(subprocess.run(["hyprctl", "monitors", "all", "-j"], capture_output=True, text=True, timeout=3).stdout)
    except Exception:
        return []


def monitors_dump():
    saved = mon_read()
    for m in hypr_monitors():
        f = saved.get(m["name"], {})
        mode = "%dx%d@%.2fHz" % (m["width"], m["height"], m["refreshRate"])
        modes = []
        for md in m.get("availableModes", []):
            if md not in modes:
                modes.append(md)
        print("|".join([m["name"], mode, str(m["x"]), str(m["y"]), str(m["scale"]), str(m["transform"]),
                        "1" if m.get("disabled") else "0", f.get("bitdepth", "8"), f.get("cm", "auto"), ",".join(modes),
                        m.get("description", "")]))


def monitor_set(name, sets):
    snapshot()
    mons = mon_read()
    current = {m["name"]: m for m in hypr_monitors()}
    if name not in current:
        return
    f = mons.setdefault(name, {"output": name})
    live = current[name]
    # A screen without a block yet starts from its current state, so changing one setting changes only that setting
    f.setdefault("mode", "%dx%d@%.2fHz" % (live["width"], live["height"], live["refreshRate"]))
    f.setdefault("position", "%dx%d" % (live["x"], live["y"]))
    f.setdefault("scale", str(live["scale"]))
    f.setdefault("transform", str(live["transform"]))
    enabled_others = [m for n, m in current.items() if n != name and not m.get("disabled")]
    for key, value in sets:
        if key == "disabled" and value in ("1", "true") and not enabled_others:
            continue  # Never turn off the only active screen
        f[key] = value
    mon_write(mons)
    subprocess.run(["hyprctl", "eval", mon_lua(f)], capture_output=True)


# ---------------------------------------------------------------- idle (hypridle.conf)

HYPRIDLE = HOME + "/.config/hypr/hypridle.conf"
IDLE_LISTENERS = {
    "lock": {"match": ["lock-session"], "on-timeout": "loginctl lock-session"},
    "screenOff": {"match": ["dpms"], "on-timeout": "hyprctl dispatch 'hl.dsp.dpms({ action = \"disable\" })'",
                  "on-resume": "hyprctl dispatch 'hl.dsp.dpms({ action = \"enable\" })'"},
    "suspend": {"match": ["suspend"], "on-timeout": "systemctl suspend || loginctl suspend"},
}


def idle_blocks(lines):
    """The file as a list of ("raw", [lines]) and ("listener", [lines])."""
    out, cur, depth = [], None, 0
    for line in lines:
        if cur is None:
            if re.match(r"^\s*listener\s*\{", line):
                cur, depth = [line], line.count("{") - line.count("}")
                if depth <= 0:
                    out.append(("listener", cur)); cur = None
                continue
            out.append(("raw", [line]))
            continue
        cur.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            out.append(("listener", cur)); cur = None
    if cur is not None:
        out.append(("raw", cur))
    return out


def idle_name(block_lines):
    text = "".join(block_lines)
    cmd = " ".join(l for l in block_lines if "on-timeout" in l)
    for name, spec in IDLE_LISTENERS.items():
        if all(m in cmd for m in spec["match"]):
            return name
    return None


def idle_timeout(block_lines):
    for l in block_lines:
        m = re.match(r"\s*timeout\s*=\s*(\d+)", l)
        if m:
            return int(m.group(1))
    return 0


def idle_default_file():
    shell = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return ("$lock_cmd = pidof end4pC-lock || %s/build/end4pC-lock\n$suspend_cmd = systemctl suspend || loginctl suspend\n\n"
            "general {\n    lock_cmd = $lock_cmd\n    before_sleep_cmd = loginctl lock-session\n    inhibit_sleep = 3\n}\n\n" % shell)


def idle_get(path):
    try:
        lines = open(path).readlines()
    except FileNotFoundError:
        lines = []
    values = {n: 0 for n in IDLE_LISTENERS}
    for kind, bl in idle_blocks(lines):
        if kind == "listener":
            n = idle_name(bl)
            if n:
                values[n] = idle_timeout(bl)
    for n in IDLE_LISTENERS:
        print("%s=%d" % (n, values[n]))


def idle_set(path, name, seconds):
    if name not in IDLE_LISTENERS:
        return
    if not os.path.exists(path):
        write_atomic(path, idle_default_file())
    lines = open(path).readlines()
    # Keep the file as it was before the first change made here
    keep = BASELINE + "/hypridle.conf"
    if path == HYPRIDLE and not os.path.exists(keep):
        os.makedirs(BASELINE, exist_ok=True)
        open(keep, "w").write("".join(lines))
    spec = IDLE_LISTENERS[name]
    new, found = [], False
    for kind, bl in idle_blocks(lines):
        if kind == "listener" and idle_name(bl) == name:
            found = True
            if seconds > 0:  # Keep the block's own commands; only the timeout changes
                new.extend(re.sub(r"(\s*timeout\s*=\s*)\d+", r"\g<1>%d" % seconds, l) for l in bl)
            else:
                while new and new[-1].strip() == "":
                    new.pop()
            continue
        new.extend(bl)
    if not found and seconds > 0:
        if new and not new[-1].endswith("\n"):
            new[-1] += "\n"
        cmd = spec["on-timeout"]
        if name == "suspend" and any(l.startswith("$suspend_cmd") for l in lines):  # The file already defines this command
            cmd = "$suspend_cmd"
        block = "\nlistener {\n    timeout = %d\n    on-timeout = %s\n" % (seconds, cmd)
        if "on-resume" in spec:
            block += "    on-resume = %s\n" % spec["on-resume"]
        new.append(block + "}\n")
    write_atomic(path, "".join(new))


def idle_restart():
    subprocess.run(["bash", "-c", "pkill -x hypridle; sleep 0.3; setsid -f hypridle >/dev/null 2>&1"], capture_output=True)


def preset(name):
    snapshot()
    if name == "off":
        write_atomic(ANIMATIONS, "")
        edit([("animations:enabled", "0")], [])
        return
    if name not in ANIM_PRESETS:
        return
    write_atomic(ANIMATIONS, ANIM_PRESETS[name])
    edit([("animations:enabled", "1")], [])
    subprocess.run(["hyprctl", "reload"], capture_output=True)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    if a[0] == "--dump":
        dump(a[1:])
    elif a[0] == "--restore":
        restore()
    elif a[0] == "--idle-get":
        idle_get(a[1] if len(a) > 1 else HYPRIDLE)
    elif a[0] == "--idle" and len(a) > 2:
        path = a[3] if len(a) > 3 else HYPRIDLE
        idle_set(path, a[1], int(a[2]))
        if path == HYPRIDLE:
            idle_restart()
    elif a[0] == "--monitors":
        monitors_dump()
    elif a[0] == "--monitor" and len(a) > 1:
        sets, i = [], 2
        while i + 2 < len(a) + 0 and a[i] == "--set":
            sets.append((a[i + 1], a[i + 2]))
            i += 3
        monitor_set(a[1], sets)
    elif a[0] == "--anim-preset" and len(a) > 1:
        preset(a[1])
    else:
        sets, resets, i = [], [], 0
        while i < len(a):
            if a[i] == "--set" and i + 2 < len(a):
                sets.append((a[i + 1], a[i + 2]))
                i += 3
            elif a[i] == "--reset" and i + 1 < len(a):
                resets.append(a[i + 1])
                i += 2
            else:
                i += 1
        # "" means "back to default": there is no line to keep
        resets += [k for k, v in sets if v == ""]
        sets = [(k, v) for k, v in sets if v != ""]
        edit(sets, resets)


main()
