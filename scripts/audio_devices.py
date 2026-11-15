#!/usr/bin/env python3
"""The audio devices of the bar's volume popup (src/parts/14p_audio_menu.inc), read and set through pactl and bluetoothctl.

  audio_devices.py [--fixture FILE] list
      One line per row, tab separated:
        kind  action  target  extra  title  sub  icon  tags  default(0|1)  volume  mute(0|1)  active(0|1)
      kind: sink | source (the section). action: set (make target the default; extra = its port, may be empty),
      profile (switch card target to profile extra, then use the device that appears), connect (connect the Bluetooth
      device target, then use the device that appears). tags: comma list of bt, usb, hdmi, virtual, profile, and
      battery=NN. volume: the device's own volume in percent (-1: none, for profile and connect rows); mute: its mute;
      active: 1 for the row that carries the device's volume (the active port, or the device without ports), 0 for the
      other ports of the same device.
      Then one line per playback stream (an app): app  index  name  icon  volume  mute(0|1)  corked(0|1)  media
      (icon: the theme icon name of the app, may be empty; media: what it plays, may be empty).
      Then one "input" line: the default source's volume in percent and its mute (0|1).
  audio_devices.py [--fixture FILE] set sink|source NAME [PORT]
  audio_devices.py [--fixture FILE] profile sink|source CARD PROFILE
  audio_devices.py [--fixture FILE] connect sink|source MAC
      The actions. set makes NAME the default, sets its port, and moves the playing streams to it (sink inputs, and source
      outputs that record a microphone, not a monitor).

Rules: monitor sources (what every sink exposes for recording) are left out. A device whose ports are all unplugged is left
out, and so is a port that is unplugged. Card profiles that would give an output or an input (HDMI, DisplayPort...) are
offered when their availability is yes or unknown. Paired Bluetooth audio devices that are not connected are offered to connect.
--fixture reads pactl's JSON and the Bluetooth text from a file instead of the system (for the tests).
"""
import glob
import json
import re
import subprocess
import sys
import time

AUDIO_UUIDS = {"0000110b": "sink", "00001108": "sink", "0000111e": "sink", "0000110a": "source"}  # A2DP sink, headset, handsfree, A2DP source
WAIT_SECONDS = 6.0
WAIT_STEP = 0.2


def avail(value):
    """pactl's availability (an int 0/1/2 or a word) as "yes", "no" or "unknown"."""
    if isinstance(value, int):
        return {1: "no", 2: "yes"}.get(value, "unknown")
    word = str(value or "").lower()
    if word in ("no", "not available", "unavailable"):
        return "no"
    if word in ("yes", "available"):
        return "yes"
    return "unknown"


def clean(text):
    return re.sub(r"[\t\r\n]+", " ", str(text or "")).strip()


def is_monitor(dev):
    return dev.get("name", "").endswith(".monitor") or dev.get("properties", {}).get("device.class") == "monitor"


class Live:
    """The real system: pactl and bluetoothctl."""
    fixture = False

    def lists(self, what):
        try:
            out = subprocess.run(["pactl", "-f", "json", "list", what], capture_output=True, text=True, timeout=5).stdout
            return json.loads(out) if out.strip() else []
        except (OSError, subprocess.SubprocessError, ValueError):
            return []

    def default(self, kind):
        try:
            return subprocess.run(["pactl", f"get-default-{kind}"], capture_output=True, text=True, timeout=5).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""

    def input_volume_text(self):
        try:
            return subprocess.run(["wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"], capture_output=True, text=True, timeout=5).stdout
        except (OSError, subprocess.SubprocessError):
            return ""

    def bt(self, *args):
        if not glob.glob("/sys/class/bluetooth/hci*"):  # no adapter: bluetoothctl would only wait for one
            return ""
        try:
            return subprocess.run(["bluetoothctl", *args], capture_output=True, text=True, timeout=4).stdout
        except (OSError, subprocess.SubprocessError):
            return ""

    def act(self, *args):
        try:
            subprocess.run(args, capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError):
            pass

    def wait(self):
        time.sleep(WAIT_STEP)


class Fixture:
    """A recorded system: pactl's JSON and bluetoothctl's text from a file. Actions are recorded in `calls`, not run."""
    fixture = True

    def __init__(self, path):
        with open(path, encoding="utf-8") as fh:
            self.data = json.load(fh)
        self.calls = []

    def lists(self, what):
        return self.data.get(what, [])

    def default(self, kind):
        return self.data.get(f"default-{kind}", "")

    def input_volume_text(self):
        return self.data.get("input-volume", "")

    def bt(self, *args):
        if args[:2] == ("devices", "Paired"):
            return self.data.get("bt-paired", "")
        if args[:1] == ("info",):
            return self.data.get("bt-info", {}).get(args[1], "")
        return ""

    def act(self, *args):
        self.calls.append(list(args))

    def wait(self):
        pass  # a recorded system does not change: the wait looks once


def usable_devices(backend, kind):
    """The devices of a kind that can be offered: no monitors, and no device whose ports are all unplugged."""
    out = []
    for dev in backend.lists(kind + "s"):
        ports = dev.get("ports") or []
        if is_monitor(dev) or (ports and all(avail(p.get("availability")) == "no" for p in ports)):
            continue
        out.append(dev)
    return out


def device_icon(kind, text):
    """A token for the row's icon, from the device's and port's names and form factor (the shell draws it)."""
    text = text.lower()
    if "virtual" in text:
        return "tune"
    if "hdmi" in text or "displayport" in text:
        return "tv"
    if "headphone" in text or "headset" in text:
        return "headphones-round"
    if "webcam" in text:
        return "monitor-camera"
    if "bluetooth" in text:
        return "bluetooth"
    if "usb" in text:
        return "usb"
    if "speaker" in text:
        return "speaker"
    return "microphone" if kind == "source" else "speaker"


def volume_of(dev):
    """A sink's or source's volume in percent: the loudest channel (-1 when pactl gave none)."""
    values = []
    for channel in (dev.get("volume") or {}).values():
        match = re.match(r"\s*(\d+)", str(channel.get("value_percent", "")))
        if match:
            values.append(int(match.group(1)))
    return max(values) if values else -1


def row(kind, action, target, extra, title, sub, icon, tags, is_default, volume=-1, muted=False, active=True):
    return [kind, action, target, extra, clean(title), clean(sub), icon, ",".join(tags), "1" if is_default else "0",
            str(volume), "1" if muted else "0", "1" if active else "0"]


def device_rows(backend, kind, default_name):
    rows = []
    for dev in usable_devices(backend, kind):
        props = dev.get("properties", {})
        name = dev.get("name", "")
        nick = clean(props.get("node.nick")) or clean(dev.get("description")) or name
        ports = [p for p in (dev.get("ports") or []) if avail(p.get("availability")) != "no"]
        bus = clean(props.get("device.bus"))
        virtual = not dev.get("ports") and not bus
        tags = []
        if bus == "bluetooth":
            tags.append("bt")
        elif bus == "usb":
            tags.append("usb")
        if virtual:
            tags.append("virtual")
        address = props.get("api.bluez5.address", "")
        if address:
            battery = battery_of(backend, address)
            if battery is not None:
                tags.append(f"battery={battery}")
        volume, muted = volume_of(dev), bool(dev.get("mute"))
        if ports:
            for port in ports:
                text = f"{name} {port.get('name', '')} {props.get('device.icon_name', '')} {props.get('device.form_factor', '')}"
                is_default = name == default_name and port.get("name") == dev.get("active_port")
                active = port.get("name") == dev.get("active_port") or (len(ports) == 1 and not dev.get("active_port"))
                rows.append(row(kind, "set", name, port.get("name", ""), port.get("description") or port.get("name", ""),
                                nick, device_icon(kind, text), tags, is_default, volume, muted, active))
        else:
            text = f"{name} {props.get('device.icon_name', '')} {props.get('device.form_factor', '')} {dev.get('active_port') or ''}"
            rows.append(row(kind, "set", name, "", nick, "", device_icon(kind, text), tags, name == default_name, volume, muted, True))
    return rows


def battery_of(backend, address):
    match = re.search(r"Battery Percentage:\s*0x[0-9a-fA-F]+\s*\((\d+)\)", backend.bt("info", address))
    return int(match.group(1)) if match else None


def profile_rows(backend, kind):
    """Card profiles that would give devices of this kind, and are not the active profile: offered as "enable"."""
    rows = []
    count_key = "n_sinks" if kind == "sink" else "n_sources"
    for card in backend.lists("cards"):
        profiles = card.get("profiles") or {}
        items = profiles.items() if isinstance(profiles, dict) else [(p.get("name"), p) for p in profiles]
        for pname, prof in items:
            if not pname or pname == card.get("active_profile") or int(prof.get(count_key) or 0) == 0:
                continue
            if avail(prof.get("available")) == "no":
                continue
            text = f"{pname} {prof.get('description', '')}"
            tags = ["profile"]
            if "hdmi" in text.lower() or "displayport" in text.lower():
                tags.append("hdmi")
            rows.append(row(kind, "profile", card.get("name", ""), pname, prof.get("description") or pname,
                            card.get("description") or card.get("name", ""), device_icon(kind, text), tags, False))
    return rows


def paired_audio_rows(backend, kind, shown_addresses):
    """Paired Bluetooth devices that are not connected and have an audio profile of this kind: offered to connect."""
    rows = []
    for line in backend.bt("devices", "Paired").splitlines():
        match = re.match(r"\s*Device\s+([0-9A-Fa-f:]{17})\s+(.*)", line)
        if not match:
            continue
        mac, name = match.group(1), clean(match.group(2))
        if mac.upper() in shown_addresses:
            continue
        info = backend.bt("info", mac)
        if "Connected: yes" in info:
            continue
        uuids = {m.group(1)[:8] for m in re.finditer(r"UUID:[^(]*\(([0-9a-fA-F-]{8})", info)}
        wanted = {"sink": {"0000110b", "00001108", "0000111e"}, "source": {"0000110a"}}[kind]
        if uuids & wanted:
            icon = "headphones-round" if "headset" in info.lower() or "headphone" in info.lower() else "bluetooth"
            rows.append(row(kind, "connect", mac, "", name, "", icon, ["bt"], False))
    return rows


def connected_bt_addresses(backend):
    return {dev.get("properties", {}).get("api.bluez5.address", "").upper() for dev in backend.lists("sinks") + backend.lists("sources")}


def input_line(backend):
    text = backend.input_volume_text()
    match = re.search(r"Volume:\s*([\d.]+)", text)
    if not match:
        return ""
    return f"input\t{round(float(match.group(1)) * 100)}\t{1 if '[MUTED]' in text else 0}"


def list_rows(backend):
    rows = []
    for kind in ("sink", "source"):
        default_name = backend.default(kind)
        devices = device_rows(backend, kind, default_name)
        rows += devices
        rows += profile_rows(backend, kind)
        shown = {addr.upper() for addr in connected_bt_addresses(backend) if addr}
        rows += paired_audio_rows(backend, kind, shown)
    return rows


IGNORED_APPS = {"com.github.wwmm.easyeffects", "easyeffects"}  # the equalizer's own streams are not apps


def app_lines(backend):
    """One line per playback stream that belongs to an app (sink inputs), with its own volume and mute."""
    lines = []
    for stream in backend.lists("sink-inputs"):
        props = stream.get("properties") or {}
        name = clean(props.get("application.name")) or clean(props.get("node.name")) or clean(props.get("application.process.binary"))
        if not name or props.get("application.id") in IGNORED_APPS or name.lower() in IGNORED_APPS:
            continue
        icon = clean(props.get("application.icon_name")) or clean(props.get("application.process.binary")).lower()
        media = clean(props.get("media.name"))
        if media.lower() in ("playback", "audio stream", "playback stream", name.lower()):
            media = ""
        lines.append("\t".join(["app", str(stream.get("index", 0)), name, icon, str(volume_of(stream) if volume_of(stream) >= 0 else 100),
                                "1" if stream.get("mute") else "0", "1" if stream.get("corked") else "0", media]))
    return lines


def list_lines(backend):
    lines = ["\t".join(fields) for fields in list_rows(backend)]
    lines += app_lines(backend)
    volume = input_line(backend)
    if volume:
        lines.append(volume)
    return lines


def device_names(backend, kind):
    return {dev.get("name") for dev in usable_devices(backend, kind)}


def wait_for_new(backend, kind, before):
    """The first device of this kind that was not there before (a profile or a Bluetooth connection made it appear)."""
    for _ in range(int(WAIT_SECONDS / WAIT_STEP) if not backend.fixture else 1):
        new = [name for name in device_names(backend, kind) if name not in before]
        if new:
            return new[0]
        backend.wait()
    return ""


def move_streams(backend, kind, name):
    if kind == "sink":
        for stream in backend.lists("sink-inputs"):
            backend.act("pactl", "move-sink-input", str(stream["index"]), name)
        return
    monitors = {str(dev["index"]): is_monitor(dev) for dev in backend.lists("sources")}
    for stream in backend.lists("source-outputs"):
        if monitors.get(str(stream.get("source"))) is False:
            backend.act("pactl", "move-source-output", str(stream["index"]), name)


def set_default(backend, kind, name, port=""):
    if name not in device_names(backend, kind):
        return 1
    if port:
        backend.act("pactl", f"set-{kind}-port", name, port)
    backend.act("pactl", f"set-default-{kind}", name)
    move_streams(backend, kind, name)
    return 0


def run_action(backend, argv):
    if len(argv) >= 3 and argv[0] == "set" and argv[1] in ("sink", "source"):
        return set_default(backend, argv[1], argv[2], argv[3] if len(argv) > 3 else "")
    if len(argv) == 4 and argv[0] == "profile" and argv[1] in ("sink", "source"):
        kind = argv[1]
        before = device_names(backend, kind)
        backend.act("pactl", "set-card-profile", argv[2], argv[3])
        new = wait_for_new(backend, kind, before)
        return set_default(backend, kind, new) if new else 1
    if len(argv) == 3 and argv[0] == "connect" and argv[1] in ("sink", "source"):
        kind = argv[1]
        before = device_names(backend, kind)
        backend.act("bluetoothctl", "connect", argv[2])
        new = wait_for_new(backend, kind, before)
        return set_default(backend, kind, new) if new else 1
    return 2


def main(argv, backend=None):
    if argv[:1] == ["--fixture"] and len(argv) >= 2:
        backend = Fixture(argv[1])
        argv = argv[2:]
    backend = backend or Live()
    if argv == ["list"]:
        print("\n".join(list_lines(backend)))
        return 0
    if argv[:1] in (["set"], ["profile"], ["connect"]):
        return run_action(backend, argv)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
