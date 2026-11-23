#!/usr/bin/env python3
"""Starts the apps of Tools > Autostart, one after another, when the session starts.

  autostart.py            start every enabled app of ~/.local/share/end4pC/autostart.tsv, in order
  autostart.py --list     print what would start (enabled, workspace, name, command), starts nothing

Each app is started, and the next one waits until that app has shown a window (at most WAIT seconds: an app that lives in the tray shows
none). With Hyprland an app with a workspace opens there without switching to it (hl.dsp.exec_cmd with workspace "N silent", or "special
silent" for the special one). Two things the rule alone does not cover are handled here: an app that is already running when its turn comes
(many start themselves) has its windows moved to the workspace instead of being started again, and an app that opens a loading window first
and its main window later has that one moved too (for CLAIM seconds the windows of the app, known by its StartupWMClass, its desktop file name
and the class of its first window, are moved to the workspace). Other compositors just start the apps in order.
The shell runs this once per login (a marker in XDG_RUNTIME_DIR), not each time it restarts. The log is ~/.local/state/end4pC/autostart.log.
"""
import json
import os
import re
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
CLAIM = 25.0  # seconds after an app starts during which its windows are put on its workspace
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


def clients() -> list[dict]:
    try:
        out = subprocess.run(["hyprctl", "-j", "clients"], capture_output=True, text=True, timeout=3).stdout
        return [c for c in json.loads(out) if c.get("mapped")]
    except (OSError, ValueError, subprocess.SubprocessError):
        return []


def windows() -> set[str]:
    return {c["address"] for c in clients()}


def lua_string(s: str) -> str:
    """A Lua long string that holds s as it is, whatever it contains."""
    level = 0
    while "]" + "=" * level + "]" in s:
        level += 1
    return "[" + "=" * level + "[" + s + "]" + "=" * level + "]"


def target(ws: int) -> str:
    return "special" if ws == SPECIAL else str(ws)


def start(app: dict) -> None:
    if hypr():
        opts = ', {workspace = "%s silent"}' % target(app["ws"]) if app["ws"] != 0 else ""
        subprocess.run(["hyprctl", "dispatch", "hl.dsp.exec_cmd(%s%s)" % (lua_string(app["cmd"]), opts)], capture_output=True, timeout=5)
    else:
        subprocess.Popen(["sh", "-c", app["cmd"]], start_new_session=True, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def move(address: str, ws: int) -> None:
    subprocess.run(["hyprctl", "dispatch", 'hl.dsp.window.move({workspace = "%s", follow = false, window = "address:%s"})' % (target(ws), address)],
                   capture_output=True, timeout=5)


def on_workspace(c: dict, ws: int) -> bool:
    name = str(c.get("workspace", {}).get("name", ""))
    if ws == SPECIAL:
        return name.startswith("special")
    return c.get("workspace", {}).get("id") == ws


def classes_of(cmd: str) -> set[str]:
    """The window classes an app is known by: from its desktop file when it is started through one (`gio launch 'file'`), else its program."""
    out: set[str] = set()
    m = re.fullmatch(r"gio launch '(.*)'", cmd, re.S)
    if m:
        path = m.group(1).replace("'\\''", "'")
        out.add(os.path.basename(path)[: -len(".desktop")].lower() if path.endswith(".desktop") else os.path.basename(path).lower())
        try:
            in_entry = False
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("["):
                        in_entry = line == "[Desktop Entry]"
                    elif in_entry and line.startswith("StartupWMClass="):
                        out.add(line.split("=", 1)[1].strip().lower())
        except OSError:
            pass
    else:
        first = cmd.split(" ", 1)[0]
        out.add(os.path.basename(first).lower())
    out.discard("")
    return out


class Claim:
    """The windows of one app that belong on its workspace, for a while after it started."""

    def __init__(self, name: str, ws: int, classes: set[str]) -> None:
        self.name, self.ws, self.classes = name, ws, set(classes)
        self.until = time.monotonic() + CLAIM
        self.seen: set[str] = set()

    def matches(self, c: dict) -> bool:
        return (c.get("class", "") or "").lower() in self.classes or (c.get("initialClass", "") or "").lower() in self.classes

    def settle(self, current: list[dict]) -> int:
        """Moves the matching windows seen for the first time that are not on the workspace; returns how many it moved."""
        moved = 0
        for c in current:
            if c["address"] in self.seen or not self.matches(c):
                continue
            self.seen.add(c["address"])
            if self.ws != 0 and not on_workspace(c, self.ws):
                move(c["address"], self.ws)
                log("%s: moved a window to %s" % (self.name, target(self.ws)))
                moved += 1
        return moved


def settle_all(claims: list[Claim]) -> None:
    live = [c for c in claims if time.monotonic() < c.until]
    if live:
        current = clients()
        for c in live:
            c.settle(current)


def wait_for_window(before: set[str], claims: list[Claim]) -> list[dict]:
    """The windows that appear after the app started (the first one that does), settling the claims of the apps started earlier meanwhile."""
    end = time.monotonic() + WAIT
    while time.monotonic() < end:
        current = clients()
        new = [c for c in current if c["address"] not in before]
        if new:
            return new
        for c in claims:
            if time.monotonic() < c.until:
                c.settle(current)
        time.sleep(0.15)
    return []


def main() -> int:
    apps = [a for a in load() if a["on"]]
    if "--list" in sys.argv:
        for a in apps:
            print("%s\t%d\t%s\t%s" % ("1", a["ws"], a["name"], a["cmd"]))
        return 0
    log("starting %d app(s)" % len(apps))
    claims: list[Claim] = []
    for a in apps:
        if not hypr():
            try:
                start(a)
            except (OSError, subprocess.SubprocessError) as e:
                log("%s: could not start (%s)" % (a["name"], e))
                continue
            log("%s: started" % a["name"])
            time.sleep(1.5)
            continue
        claim = Claim(a["name"], a["ws"], classes_of(a["cmd"]))
        claims.append(claim)
        existing = [c for c in clients() if claim.matches(c)]
        if existing:  # already running (many apps start themselves): its windows go where it was asked to be, it is not started again
            log("%s: already running" % a["name"])
            claim.settle(existing)
            claim.until = time.monotonic() + 3  # (a second window may follow)
            continue
        before = windows()
        try:
            start(a)
        except (OSError, subprocess.SubprocessError) as e:
            log("%s: could not start (%s)" % (a["name"], e))
            continue
        new = wait_for_window(before, claims[:-1])
        if new:
            for c in new[:1]:  # the class of the first window is known too (a loading window may have another name than the main one)
                claim.classes.update(x.lower() for x in (c.get("class", ""), c.get("initialClass", "")) if x)
            claim.settle(new)
            log("%s: window shown" % a["name"])
            time.sleep(SETTLE)
        else:
            log("%s: no window within %ds" % (a["name"], WAIT))
    if hypr():  # the windows that open late go to their workspaces too
        while any(time.monotonic() < c.until for c in claims):
            settle_all(claims)
            time.sleep(0.5)
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
