#!/usr/bin/env python3
"""Starts the apps of Tools > Autostart, one after another, when the session starts.

  autostart.py            start every enabled app of ~/.local/share/end4pC/autostart.tsv, in order
  autostart.py --list     print what would start (enabled, workspace, name, command), starts nothing

Each app is started, and the next one waits until that app has shown a window (at most WAIT seconds: an app that lives in the tray, or
that was already running, shows none). With Hyprland an app with a workspace opens there without switching to it (hl.dsp.exec_cmd with
workspace "N silent", or "special silent" for the special one); other compositors just start the apps in order.
The shell runs this once per login (a marker in XDG_RUNTIME_DIR), not each time it restarts. The log is ~/.local/state/end4pC/autostart.log.
"""
import json
import os
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
DATA = os.environ.get("XDG_DATA_HOME") or HOME + "/.local/share"
STATE = os.environ.get("XDG_STATE_HOME") or HOME + "/.local/state"
TABLE = DATA + "/end4pC/autostart.tsv"
LOG = STATE + "/end4pC/autostart.log"
WAIT = float(os.environ.get("END4PC_AUTOSTART_WAIT", "8"))  # seconds to wait for the window of each app
MAX_WS = 20  # two monitors, ten workspaces each
SPECIAL = -1  # the special workspace of Hyprland
SETTLE = 0.4  # a moment after the window shows, so the next app does not start on top of it


def log(msg: str) -> None:
    try:
        os.makedirs(os.path.dirname(LOG), exist_ok=True)
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(time.strftime("%H:%M:%S ") + msg + "\n")
    except OSError:
        pass


def load() -> list[dict]:
    apps = []
    try:
        with open(TABLE, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                p = line.split("\t")
                if len(p) < 5 or not p[4].strip():
                    continue
                try:
                    ws = max(SPECIAL, min(MAX_WS, int(p[1])))
                except ValueError:
                    ws = 0
                apps.append({"on": p[0] != "0", "ws": ws, "name": p[2], "cmd": p[4]})
    except OSError:
        pass
    return apps


def hypr() -> bool:
    return bool(os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"))


def windows() -> set[str]:
    try:
        out = subprocess.run(["hyprctl", "-j", "clients"], capture_output=True, text=True, timeout=3).stdout
        return {c["address"] for c in json.loads(out) if c.get("mapped")}
    except (OSError, ValueError, subprocess.SubprocessError):
        return set()


def lua_string(s: str) -> str:
    """A Lua long string that holds s as it is, whatever it contains."""
    level = 0
    while "]" + "=" * level + "]" in s:
        level += 1
    return "[" + "=" * level + "[" + s + "]" + "=" * level + "]"


def start(app: dict) -> None:
    if hypr():
        target = "special" if app["ws"] == SPECIAL else str(app["ws"])
        opts = ', {workspace = "%s silent"}' % target if app["ws"] != 0 else ""
        subprocess.run(["hyprctl", "dispatch", "hl.dsp.exec_cmd(%s%s)" % (lua_string(app["cmd"]), opts)], capture_output=True, timeout=5)
    else:
        subprocess.Popen(["sh", "-c", app["cmd"]], start_new_session=True, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_for_window(before: set[str]) -> bool:
    end = time.monotonic() + WAIT
    while time.monotonic() < end:
        if windows() - before:
            return True
        time.sleep(0.15)
    return False


def main() -> int:
    apps = [a for a in load() if a["on"]]
    if "--list" in sys.argv:
        for a in apps:
            print("%s\t%d\t%s\t%s" % ("1", a["ws"], a["name"], a["cmd"]))
        return 0
    log("starting %d app(s)" % len(apps))
    for a in apps:
        before = windows() if hypr() else set()
        try:
            start(a)
        except (OSError, subprocess.SubprocessError) as e:
            log("%s: could not start (%s)" % (a["name"], e))
            continue
        if hypr():
            shown = wait_for_window(before)
            log("%s: %s" % (a["name"], "window shown" if shown else "no window within %ds" % WAIT))
            time.sleep(SETTLE if shown else 0)
        else:
            log("%s: started" % a["name"])
            time.sleep(1.5)
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
