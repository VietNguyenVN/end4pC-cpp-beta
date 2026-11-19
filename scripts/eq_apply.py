#!/usr/bin/env python3
"""Applies the equalizer of the media panel through EasyEffects.

The ten bands and the preamp are merged into the output preset EasyEffects has loaded (so the rest of its chain stays), or into a preset of
our own when there is none, and that preset is loaded again. Off turns the global bypass on.

Usage: eq_apply.py <preset dir> <enabled 0|1> <preamp dB> <ten band gains dB>
"""
import json
import os
import subprocess
import sys
import time

FREQS = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
OWN = "end4pC-equalizer"


def easyeffects(*args):
    try:
        return subprocess.run(["easyeffects", *args], capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def running():
    return subprocess.run(["pidof", "easyeffects"], capture_output=True).returncode == 0


def band(freq, gain):
    return {"frequency": float(freq), "gain": float(gain), "mode": "RLC (BT)", "mute": False, "q": 1.41, "slope": "x1", "solo": False, "type": "Bell", "width": 4.0}


def main():
    directory, enabled, preamp = sys.argv[1], sys.argv[2] == "1", float(sys.argv[3])
    gains = [float(x) for x in sys.argv[4:14]]
    if not enabled:
        easyeffects("-b", "1")
        return
    if not running():
        subprocess.Popen(["easyeffects", "--hide-window", "--service-mode"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(3)
    active = easyeffects("-a", "output")
    name = active if active and os.path.isfile(os.path.join(directory, active + ".json")) else OWN
    path = os.path.join(directory, name + ".json")
    try:
        with open(path) as f:
            preset = json.load(f)
    except (OSError, ValueError):
        preset = {}
    output = preset.setdefault("output", {})
    output.setdefault("blocklist", [])
    order = output.setdefault("plugins_order", [])
    key = next((p for p in order if p.startswith("equalizer#")), "equalizer#0")
    if key not in order:
        order.append(key)
    bands = {f"band{i}": band(FREQS[i], gains[i]) for i in range(10)}
    output[key] = {"balance": 0.0, "bypass": False, "input-gain": 0.0, "output-gain": preamp, "mode": "IIR", "num-bands": 10,
                   "split-channels": False, "left": bands, "right": dict(bands)}
    os.makedirs(directory, exist_ok=True)
    tmp = path + ".tmp"  # (EasyEffects watches the folder: written whole, then renamed)
    with open(tmp, "w") as f:
        json.dump(preset, f, indent=2)
    os.replace(tmp, path)
    easyeffects("-b", "2")
    easyeffects("-l", name)


main()
