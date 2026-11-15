#!/usr/bin/env python3
"""end4pC themes: ready-made palettes (Catppuccin, Gruvbox, Nord...) as one JSON file each.

Built-in ones live in <project>/schemes, yours in ~/.local/share/end4pC/themes (same file name = yours wins). The format is the same as
end4-pC's, see schemes/README.md.

  named_scheme.py --list [MODE]         one tab separated line per theme: id, name, source, then for MODE (dark|light):
                                        surface, surface_container, surface_container_high, on_surface, primary, secondary, tertiary
  named_scheme.py --validate FILE       prints OK, or the problems (exit 1)
  named_scheme.py --import FILE         validates, copies it to the user folder, prints its id
  named_scheme.py --accents ID MODE     the accent colors a theme offers: "name<TAB>#hex" lines, then "@defaults<TAB>primary<TAB>secondary<TAB>tertiary"
  named_scheme.py --apply ID MODE [PRIMARY [SECONDARY [TERTIARY]]]
                                        writes the palette (colors.json) the shell reads; the three names pick accents of the theme (default: its own)
"""
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
BUILTIN = PROJECT / "schemes"
USER = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "end4pC" / "themes"
STATE = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "end4pC"
TEMPLATE = HERE / "colors.json.tpl"
HEX = re.compile(r"^#[0-9a-fA-F]{6}$")
SURFACE_KEYS = ["surface_dim", "surface", "surface_bright", "surface_container_lowest", "surface_container_low", "surface_container",
                "surface_container_high", "surface_container_highest", "on_surface", "on_surface_variant", "outline", "outline_variant",
                "error", "on_accent"]


def validate(data):
    errors = []
    if not isinstance(data, dict):
        return ["the file is not a JSON object"]
    if not isinstance(data.get("name"), str) or not data["name"].strip():
        errors.append('"name" must be a non-empty string')
    names = {}
    for mode in ("dark", "light"):
        block = data.get(mode)
        if not isinstance(block, dict):
            errors.append(f'"{mode}" block is missing')
            continue
        for key in SURFACE_KEYS:
            if not (isinstance(block.get(key), str) and HEX.match(block[key])):
                errors.append(f'{mode}.{key} must be a hex color like "#1e1e2e"')
        acc = block.get("accents")
        if not isinstance(acc, dict) or not acc:
            errors.append(f"{mode}.accents must list at least one accent color")
            acc = {}
        for n, v in acc.items():
            if not (isinstance(v, str) and HEX.match(v)):
                errors.append(f"{mode}.accents.{n} must be a hex color")
        names[mode] = set(acc)
    d = data.get("defaults")
    if not isinstance(d, dict):
        errors.append('"defaults" must set primary, secondary and tertiary accent names')
    else:
        for slot in ("primary", "secondary", "tertiary"):
            for mode, ns in names.items():
                if d.get(slot) not in ns:
                    errors.append(f'defaults.{slot} "{d.get(slot)}" is not an accent in {mode}.accents')
    return errors


def load(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError) as e:
        return None, [f"cannot read JSON: {e}"]
    errs = validate(data)
    return (None, errs) if errs else (data, [])


def discover():
    found = {}
    for source, folder in (("builtin", BUILTIN), ("user", USER)):
        if folder.is_dir():
            for f in sorted(folder.glob("*.json")):
                if f.stem.startswith("_"):
                    continue
                data, errs = load(f)
                if data is None:
                    print(f"[named_scheme] skipping {f}: {'; '.join(errs)}", file=sys.stderr)
                    continue
                found[f.stem] = (source, data)
    return found


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def hexs(c):
    return "#{:02x}{:02x}{:02x}".format(*[max(0, min(255, round(x))) for x in c])


def mix(a, b, t):
    return hexs(tuple(x + (y - x) * t for x, y in zip(rgb(a), rgb(b))))


def roles(data, mode, picks=()):
    p = dict(data[mode])
    acc = p.pop("accents")
    p.pop("term", None)
    chosen = dict(data["defaults"])
    for slot, name in zip(("primary", "secondary", "tertiary"), picks):
        if name in acc:  # an unknown name (another theme's) is ignored
            chosen[slot] = name
    for slot, name in chosen.items():
        p[slot] = acc[name]
    on_accent, surface, on_surface = p.pop("on_accent"), p["surface"], p["on_surface"]
    r = dict(p)
    amount = 0.30 if mode == "dark" else 0.22
    for n in ("primary", "secondary", "tertiary", "error"):
        a = p[n]
        r[f"on_{n}"] = on_accent
        r[f"{n}_container"] = mix(surface, a, amount)
        r[f"on_{n}_container"] = on_surface
        if n != "error":
            r[f"{n}_fixed"] = a
            r[f"{n}_fixed_dim"] = mix(a, surface, 0.25)
            r[f"on_{n}_fixed"] = on_accent
            r[f"on_{n}_fixed_variant"] = mix(on_accent, a, 0.3)
    r["background"], r["on_background"] = surface, on_surface
    r["surface_variant"] = p["surface_container_high"]
    r["surface_tint"] = p["primary"]
    r["inverse_surface"], r["inverse_on_surface"] = on_surface, surface
    r["inverse_primary"] = mix(p["primary"], surface, 0.45)
    r["shadow"] = r["scrim"] = "#000000"
    r["source_color"] = p["primary"]
    return r


def main(a):
    if a[:1] == ["--validate"] and len(a) > 1:
        _, errs = load(a[1])
        print("\n".join(errs) if errs else "OK", file=sys.stderr if errs else sys.stdout)
        return 1 if errs else 0
    if a[:1] == ["--import"] and len(a) > 1:
        _, errs = load(a[1])
        if errs:
            print("\n".join(errs))
            return 1
        USER.mkdir(parents=True, exist_ok=True)
        stem = re.sub(r"[^A-Za-z0-9_-]", "_", Path(a[1]).stem).lstrip("_") or "theme"
        (USER / f"{stem}.json").write_text(Path(a[1]).read_text())
        print(stem)
        return 0
    if a[:1] == ["--list"]:
        mode = a[1] if len(a) > 1 and a[1] in ("dark", "light") else "dark"
        for sid, (source, d) in discover().items():
            r = roles(d, mode)
            cols = [r[k] for k in ("surface", "surface_container", "surface_container_high", "on_surface", "primary", "secondary", "tertiary")]
            print("\t".join([sid, d["name"].replace("\t", " "), source] + cols))
        return 0
    if a[:1] == ["--accents"] and len(a) > 2:
        found = discover()
        if a[1] not in found:
            return 1
        d = found[a[1]][1]
        mode = a[2] if a[2] in ("dark", "light") else "dark"
        for name, hx in d[mode]["accents"].items():
            print(f"{name}\t{hx}")
        df = d["defaults"]
        print(f"@defaults\t{df['primary']}\t{df['secondary']}\t{df['tertiary']}")
        return 0
    if a[:1] == ["--apply"] and len(a) > 2:
        found = discover()
        if a[1] not in found:
            print(f"unknown or invalid theme '{a[1]}'", file=sys.stderr)
            return 1
        picks = [x for x in a[3:6]] + [""] * (3 - len(a[3:6]))
        r = roles(found[a[1]][1], a[2] if a[2] in ("dark", "light") else "dark", picks)
        tpl = TEMPLATE.read_text()
        out = re.sub(r"\{\{\s*colors\.(\w+)\.default\.hex\s*\}\}", lambda m: r.get(m.group(1), m.group(0)), tpl)
        (STATE / "generated").mkdir(parents=True, exist_ok=True)
        tmp = STATE / "generated/colors.json.tmp"
        tmp.write_text(out)
        os.replace(tmp, STATE / "generated/colors.json")
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
