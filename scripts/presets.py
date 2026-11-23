#!/usr/bin/env python3
"""Local presets for end4pC: a preset is a snapshot of the look of the shell (config.json) and of the wallpaper.

  presets.py --list                      one line per preset: name TAB description TAB wallpaper TAB mode TAB scheme TAB theme
  presets.py --save "NAME[, DESCRIPTION]"  snapshot the shell as it is now (a name that exists is overwritten)
  presets.py --overwrite NAME            snapshot again, keeping the description
  presets.py --remove NAME
  presets.py --rename OLD NEW
  presets.py --apply NAME                put the preset's settings in config.json (the shell reloads it), set its wallpaper and
                                         show the desktop widgets it had (and hide the others), where they were

Presets live in ~/.local/share/end4pC/presets/NAME.json. Only the look is saved: what belongs to this machine or to the person
(language, units, weather city, pinned apps, volume limits, the terminal, ...) is left out, so a preset never changes those.
"""
import json
import os
import re
import socket
import sys
import tempfile
import time

DATA = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
STATE = os.environ.get("XDG_STATE_HOME") or os.path.expanduser("~/.local/state")
CONFIG = DATA + "/end4pC/config.json"
PRESETS = DATA + "/end4pC/presets"
WALLPAPER_FILE = STATE + "/end4pC/wallpaper.txt"
WIDGETS = ["todo", "weather", "player", "resources", "calendar", "clock", "usercard", "image"]  # Each widget keeps its state in ~/.local/state/end4pC/NAME.txt

# Keys of config.json that are not part of the look.
EXCLUDE = {
    "language", "clock24", "volumeMax", "volumeStep", "batteryLow", "batteryCritical", "batteryNotify",
    "peripheralNotify", "peripheralLow", "peripheralCritical", "unlockKeyring", "lockOnStart",
    "screenshotCopy", "recordAudio", "recordDesktop", "recordFps", "screenshotDir", "recordDir",
    "notificationTimeout", "doNotDisturb", "keepAwake", "updatesEnable", "updatesAur", "updatesFlatpak", "updatesInterval",
    "weatherCity", "weatherInterval", "weatherUnit", "preferredPlayer", "terminal", "worldClocks", "searchEngine",
    "lyricsProvider", "aiClaude", "aiCodex", "aiTokenLimit", "avatarPath", "avatarPicture", "dockPinned",
    "trayExceptions", "barScreens", "widgetScreen",
}


def clean_name(raw: str) -> str:
    name = re.sub(r'[\\/:*?"<>|\x00-\x1f]', "-", raw).strip()
    name = re.sub(r"\s+", " ", name)
    return name[:40].strip(" .")


def read_config() -> dict:
    with open(CONFIG, encoding="utf-8") as f:
        return json.load(f)


def current_wallpaper() -> str:
    try:
        with open(WALLPAPER_FILE, encoding="utf-8") as f:
            return f.readline().strip()
    except OSError:
        return ""


def preset_path(name: str) -> str:
    return os.path.join(PRESETS, name + ".json")


def read_preset(name: str) -> dict:
    with open(preset_path(name), encoding="utf-8") as f:
        return json.load(f)


def write_json(path: str, data: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), suffix=".tmp")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


def widget_state(name: str) -> dict:
    """enabled, size and pos of a desktop widget, from its state file (the other lines, like the To-Do items, are not part of a look)."""
    state = {"enabled": 0}
    try:
        with open("%s/end4pC/%s.txt" % (STATE, name), encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if parts[0] == "enabled" and len(parts) > 1:
                    state["enabled"] = 1 if parts[1] == "1" else 0
                elif parts[0] in ("size", "pos") and len(parts) > 2:
                    state[parts[0]] = [int(parts[1]), int(parts[2])]
    except (OSError, ValueError):
        pass
    return state


def set_widget_state(name: str, state: dict) -> None:
    """Writes enabled, size and pos into the widget's state file; every other line stays as it is."""
    path = "%s/end4pC/%s.txt" % (STATE, name)
    try:
        with open(path, encoding="utf-8") as f:
            lines = [l.rstrip("\n") for l in f]
    except OSError:
        lines = []
    wanted = {"enabled": "enabled\t%d" % state.get("enabled", 0)}
    for key in ("size", "pos"):
        if key in state:
            wanted[key] = "%s\t%d\t%d" % (key, state[key][0], state[key][1])
    out = []
    for line in lines:
        key = line.split("\t")[0]
        if key in wanted:
            out.append(wanted.pop(key))
        else:
            out.append(line)
    out = list(wanted.values()) + out
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


VIDEO_FILE = STATE + "/end4pC/video-wallpaper.txt"


def current_video() -> str:
    """The video wallpaper that is playing (scripts/video_wallpaper.sh), or "" when the wallpaper is a picture."""
    try:
        with open(VIDEO_FILE, encoding="utf-8") as f:
            return f.readline().strip()
    except OSError:
        return ""


def snapshot(description: str) -> dict:
    cfg = read_config()
    data = {k: v for k, v in cfg.items() if k not in EXCLUDE and not k.startswith("prefix")}
    data["_widgets"] = {name: widget_state(name) for name in WIDGETS}
    data["_preset"] = {"description": description, "wallpaper": current_wallpaper(), "video": current_video(), "saved": int(time.time())}
    return data


def cmd_list() -> None:
    if not os.path.isdir(PRESETS):
        return
    entries = []
    for file in os.listdir(PRESETS):
        if not file.endswith(".json"):
            continue
        path = os.path.join(PRESETS, file)
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            continue
        meta = data.get("_preset", {})
        entries.append((os.path.getmtime(path), file[:-5], meta.get("description", ""), meta.get("wallpaper", ""),
                        data.get("colorMode", ""), data.get("colorScheme", ""), data.get("theme", "")))
    for _, name, desc, wall, mode, scheme, theme in sorted(entries, reverse=True):
        print("\t".join(x.replace("\t", " ").replace("\n", " ") for x in (name, desc, wall, mode, scheme, theme)))


def cmd_save(raw: str) -> int:
    name, _, description = raw.partition(",")
    name = clean_name(name)
    if not name:
        print("empty name", file=sys.stderr)
        return 1
    write_json(preset_path(name), snapshot(description.strip()))
    print(name)
    return 0


def cmd_overwrite(name: str) -> int:
    try:
        description = read_preset(name).get("_preset", {}).get("description", "")
    except (OSError, ValueError):
        description = ""
    write_json(preset_path(name), snapshot(description))
    return 0


def cmd_remove(name: str) -> int:
    try:
        os.remove(preset_path(name))
    except OSError:
        return 1
    return 0


def cmd_rename(old: str, new: str) -> int:
    new = clean_name(new)
    if not new or not os.path.exists(preset_path(old)) or os.path.exists(preset_path(new)):
        return 1
    os.rename(preset_path(old), preset_path(new))
    return 0


def ipc(command: str) -> str:
    path = (os.environ.get("XDG_RUNTIME_DIR") or "/tmp") + "/end4pC.sock"
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect(path)
        s.sendall(command.encode())
        s.shutdown(socket.SHUT_WR)
        reply = b""
        while True:
            chunk = s.recv(512)
            if not chunk:
                break
            reply += chunk
        s.close()
        return reply.decode(errors="replace")
    except OSError:
        return ""


# A widget that is open now and is open in the preset too glides to the preset's place and size (the shell animates it) instead of
# closing and opening again. The clock reshapes itself when its face changes, so it still closes first.
GLIDES = {"todo", "weather", "player", "resources", "calendar", "clock", "usercard", "image"}


def glides(name: str, state: dict) -> bool:
    return name in GLIDES and bool(state.get("enabled")) and "size" in state and "pos" in state and bool(widget_state(name)["enabled"])


def close_widgets(widgets: dict) -> list:
    """Closes the open desktop widgets the preset covers, before the settings change: a widget that is open while its face changes
    (the clock's style) reshapes itself and saves that shape, which is not the one the preset has. The ones that glide stay open."""
    todo = [name for name in WIDGETS if name in widgets]
    closed = False
    for name in todo:
        if glides(name, widgets[name]):
            continue
        if widget_state(name)["enabled"]:
            ipc(name + " toggle")
            closed = True
    if closed:
        time.sleep(0.4)
    return todo


def open_widgets(widgets: dict, todo: list, gliding: set) -> None:
    """Shows the widgets the preset had on, in the size and place it saved them, and hides the rest (through the shell: it keeps the
    place in memory, so a changed file would be ignored). The ones that were already open glide to their place."""
    for name in todo:
        state = widgets[name]
        if name in gliding:
            ipc("widget %s move %d %d %d %d" % (name, state["size"][0], state["size"][1], state["pos"][0], state["pos"][1]))
        elif state.get("enabled") and "size" in state and "pos" in state:
            ipc("widget %s on %d %d %d %d" % (name, state["size"][0], state["size"][1], state["pos"][0], state["pos"][1]))
        elif state.get("enabled"):
            ipc("widget %s on" % name)
        else:
            ipc("widget %s off" % name)


def cmd_apply(name: str) -> int:
    try:
        preset = read_preset(name)
    except (OSError, ValueError):
        return 1
    meta = preset.pop("_preset", {})
    widgets = preset.pop("_widgets", {})
    gliding = {name for name in WIDGETS if name in widgets and glides(name, widgets[name])}
    todo = close_widgets(widgets)
    cfg = read_config()
    for key, value in preset.items():
        if key not in EXCLUDE and not key.startswith("_"):
            cfg[key] = value
    write_json(CONFIG, cfg)
    wallpaper = meta.get("wallpaper", "")
    video = meta.get("video", "")
    if not video and "/video-stills/" in wallpaper:  # (saved before presets knew about videos: the still remembers its video)
        try:
            with open(wallpaper + ".src", encoding="utf-8") as f:
                video = f.readline().strip()
        except OSError:
            pass
    if video and os.path.exists(video):
        wallpaper = video  # (the shell paints its first frame and plays it; a picture would stop a video)
    time.sleep(0.8)  # The shell reads the new config.json first, then generates the colors for the wallpaper and mode
    ipc("applytheme " + (wallpaper if wallpaper and os.path.exists(wallpaper) else ""))
    open_widgets(widgets, todo, gliding)
    return 0


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    action, rest = args[0], args[1:]
    if action == "--list":
        cmd_list()
        return 0
    if action == "--save" and rest:
        return cmd_save(rest[0])
    if action == "--overwrite" and rest:
        return cmd_overwrite(rest[0])
    if action == "--remove" and rest:
        return cmd_remove(rest[0])
    if action == "--rename" and len(rest) >= 2:
        return cmd_rename(rest[0], rest[1])
    if action == "--apply" and rest:
        return cmd_apply(rest[0])
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
