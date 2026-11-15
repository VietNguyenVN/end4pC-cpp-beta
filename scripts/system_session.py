#!/usr/bin/env python3
"""end4pC session settings: default apps, audio devices, power profile. Everything is read from and applied to the system itself.

Usage:
  system_session.py --apps-get                 kind=desktop-file lines
  system_session.py --apps-list KIND           "desktop-file|Name" lines of the apps that can open that kind
  system_session.py --apps-set KIND DESKTOP
  system_session.py --audio-get                sink=ID, source=ID, micmute=0|1
  system_session.py --audio-list sinks|sources "ID|Name" lines
  system_session.py --audio-set ID | --mic-mute 0|1
  system_session.py --power-get | --power-list | --power-set PROFILE
  system_session.py --bundle                   everything the System page lists and reads, in one process:
                                               "@@name" lines, each followed by that command's lines
KIND is one of: browser mail files video music image pdf text
"""
import glob
import os
import re
import subprocess
import sys

HOME = os.path.expanduser("~")
DATA = os.environ.get("XDG_DATA_HOME", os.path.join(HOME, ".local/share"))
APP_DIRS = [os.path.join(DATA, "applications"), "/usr/local/share/applications", "/usr/share/applications",
            "/var/lib/flatpak/exports/share/applications", os.path.join(DATA, "flatpak/exports/share/applications")]

# kind -> (the mime types it sets, the one that decides which apps are offered)
KINDS = {
    "browser": (["x-scheme-handler/http", "x-scheme-handler/https", "text/html"], "x-scheme-handler/https"),
    "mail": (["x-scheme-handler/mailto"], "x-scheme-handler/mailto"),
    "files": (["inode/directory"], "inode/directory"),
    "video": (["video/mp4", "video/x-matroska", "video/webm", "video/mpeg"], "video/mp4"),
    "music": (["audio/mpeg", "audio/flac", "audio/x-wav", "audio/ogg"], "audio/mpeg"),
    "image": (["image/png", "image/jpeg", "image/webp", "image/gif"], "image/png"),
    "pdf": (["application/pdf"], "application/pdf"),
    "text": (["text/plain"], "text/plain"),
}


def run(*cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=8).stdout
    except Exception:
        return ""


def desktop_entries():
    out = {}
    for d in APP_DIRS:
        for path in glob.glob(os.path.join(d, "*.desktop")):
            did = os.path.basename(path)
            if did in out:
                continue
            name, mimes, hidden = "", "", False
            try:
                with open(path, errors="ignore") as f:
                    in_entry = False
                    for line in f:
                        line = line.strip()
                        if line.startswith("["):
                            in_entry = line == "[Desktop Entry]"
                        elif in_entry and line.startswith("Name=") and not name:
                            name = line[5:]
                        elif in_entry and line.startswith("MimeType="):
                            mimes = line[9:]
                        elif in_entry and line in ("NoDisplay=true", "Hidden=true"):
                            hidden = True
            except OSError:
                continue
            if not hidden:
                out[did] = (name or did, set(m for m in mimes.split(";") if m))
    return out


def default_for(mime):
    """The default app of a mime type, read from the files xdg-mime writes (running xdg-mime itself costs 50 ms each)."""
    cfg = os.environ.get("XDG_CONFIG_HOME", os.path.join(HOME, ".config"))
    for path in (os.path.join(cfg, "mimeapps.list"), "/etc/xdg/mimeapps.list", "/usr/share/applications/mimeapps.list"):
        try:
            sect = ""
            with open(path, errors="ignore") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("["):
                        sect = line
                    elif sect == "[Default Applications]" and line.startswith(mime + "="):
                        for did in line.split("=", 1)[1].split(";"):
                            if did:
                                return did
        except OSError:
            pass
    for d in APP_DIRS:
        try:
            with open(os.path.join(d, "mimeinfo.cache"), errors="ignore") as f:
                for line in f:
                    if line.startswith(mime + "="):
                        return line.split("=", 1)[1].split(";")[0]
        except OSError:
            pass
    return ""


def apps_get():
    for kind, (_, key) in KINDS.items():
        print(f"{kind}={default_for(key)}")


def apps_list(kind):
    if kind not in KINDS:
        return
    key = KINDS[kind][1]
    rows = [(name.lower(), did, name) for did, (name, mimes) in desktop_entries().items() if key in mimes]
    for _, did, name in sorted(rows):
        print(f"{did}|{name}")


def apps_set(kind, desktop):
    if kind not in KINDS or not desktop.endswith(".desktop"):
        return
    for mime in KINDS[kind][0]:
        run("xdg-mime", "default", desktop, mime)


# ---- audio
def wpctl_list(section):
    """Lines of `wpctl status` under Sinks: / Sources: -> (id, name, is_default)."""
    out, inside = [], False
    for line in run("wpctl", "status").splitlines():
        if re.search(r"[├└]─ (Sinks|Sources|Filters|Streams|Devices|Sink endpoints|Source endpoints):", line):
            inside = f"─ {section}:" in line
            continue
        if re.match(r"^\S", line) and not line.startswith(" "):
            inside = False
        if inside:
            m = re.search(r"(\*)?\s*(\d+)\.\s+(.*?)\s+\[vol:", line)
            if m:
                out.append((m.group(2), m.group(3).strip(), bool(m.group(1))))
    return out


def audio_get():
    for key, section in (("sink", "Sinks"), ("source", "Sources")):
        items = wpctl_list(section)
        cur = next((i for i, _, d in items if d), "")
        if not cur and items:  # No default marked in the listing: look up the default node directly
            m = re.match(r"id (\d+)", run("wpctl", "inspect", "@DEFAULT_AUDIO_SINK@" if key == "sink" else "@DEFAULT_AUDIO_SOURCE@"))
            if m and any(i == m.group(1) for i, _, _ in items):
                cur = m.group(1)
        print(f"{key}={cur}")
    muted = "[MUTED]" in run("wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@")
    print(f"micmute={1 if muted else 0}")


def audio_list(which):
    for i, name, _ in wpctl_list("Sinks" if which == "sinks" else "Sources"):
        print(f"{i}|{name}")


# ---- power profile (via power-profiles-daemon on the system bus; powerprofilesctl takes 250 ms to start)
PP = ["busctl", "--system"]
PP_OBJ = ["net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles"]


def power_get():
    m = re.search(r'"([\w-]+)"', run(*PP, "get-property", *PP_OBJ, "ActiveProfile"))
    print(m.group(1) if m else "")


def power_list():
    for name in re.findall(r'"Profile" s "([\w-]+)"', run(*PP, "get-property", *PP_OBJ, "Profiles")):
        print(name)


def power_set(name):
    run(*PP, "set-property", *PP_OBJ, "ActiveProfile", "s", name)


def bundle():
    """One run for the whole page (nineteen separate Python starts made opening Settings visibly slower)."""
    import contextlib
    import io
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import system_appearance as ap

    def section(name, fn, *args):
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                fn(*args)
        except Exception:
            pass
        print(f"@@{name}")
        print(buf.getvalue(), end="")

    section("get-appearance", ap.get)
    section("get-apps", apps_get)
    section("get-audio", audio_get)
    section("get-power", power_get)
    section("sys:cursor", lambda: print("\n".join(ap.themes("cursors"))))
    section("sys:icons", lambda: print("\n".join(ap.themes("icons"))))
    section("sys:font", lambda: print("\n".join(ap.fonts(False))))
    section("sys:mono", lambda: print("\n".join(ap.fonts(True))))
    for kind in KINDS:
        section("sys:app:" + kind, apps_list, kind)
    section("sys:sink", audio_list, "sinks")
    section("sys:source", audio_list, "sources")
    section("sys:power", power_list)


def main(a):
    if a[:1] == ["--bundle"]:
        bundle()
    elif a[:1] == ["--apps-get"]:
        apps_get()
    elif a[:1] == ["--apps-list"] and len(a) > 1:
        apps_list(a[1])
    elif a[:1] == ["--apps-set"] and len(a) > 2:
        apps_set(a[1], a[2])
    elif a[:1] == ["--audio-get"]:
        audio_get()
    elif a[:1] == ["--audio-list"] and len(a) > 1:
        audio_list(a[1])
    elif a[:1] == ["--audio-set"] and len(a) > 1 and a[1].isdigit():
        run("wpctl", "set-default", a[1])
    elif a[:1] == ["--mic-mute"] and len(a) > 1:
        run("wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "1" if a[1] == "1" else "0")
    elif a[:1] == ["--power-get"]:
        power_get()
    elif a[:1] == ["--power-list"]:
        power_list()
    elif a[:1] == ["--power-set"] and len(a) > 1 and re.fullmatch(r"[\w-]+", a[1]):
        power_set(a[1])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
