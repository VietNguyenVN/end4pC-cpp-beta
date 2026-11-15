#!/usr/bin/env python3
"""Coverage of the optional Material Symbols icon set: which icon names the code can draw have no entry in assets/icons/material-map.tsv
(those keep the built-in icon at run time). No font needed; exits 1 when something is missing.

  python3 tools/check-material-map.py

After adding an icon to the code, add its Material Symbols name to MAP in tools/gen-material-assets.py and run that script.
"""
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("gen", os.path.join(ROOT, "tools", "gen-material-assets.py"))
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

used, _ = gen.used_names()
mapped = set()
with open(os.path.join(ROOT, "assets", "icons", "material-map.tsv"), encoding="utf-8") as fh:
    for line in fh:
        if line.strip() and not line.startswith("#"):
            mapped.add(line.split("\t")[0])
missing = [n for n in used if n not in mapped]
print(f"icon names used by the code: {len(used)}; mapped to Material Symbols: {len(used) - len(missing)}")
if missing:
    print("not mapped (built-in icon):", ", ".join(missing))
sys.exit(1 if missing else 0)
