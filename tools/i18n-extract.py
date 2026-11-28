#!/usr/bin/env python3
"""Collects the texts the shell can show (string literals in src/) into a catalog template.

  python3 tools/i18n-extract.py            # prints how many texts it found
  python3 tools/i18n-extract.py --write    # rewrites assets/i18n/<lang>.json (keeps the translations already there)

A text is the English sentence itself: the shell looks every text up in the catalog right before drawing it, and shows the
English one when the catalog has no translation. So code never needs wrapping; to translate a new text, add it here (run --write).
Extra texts that code builds at run time (not literal) go in tools/i18n-extra.txt, one per line.
"""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
OUT = ROOT / "assets" / "i18n"

LANGS = ["ar", "be", "ca", "cs", "de", "es", "fr", "gl-ES", "he", "hu", "it", "ja", "ko", "nl", "nn", "pl",
         "pt-BR", "ru", "sv", "tr", "uk-UA", "vi", "zh-Hans", "zh-Hant"]

# language names are shown in their own language, never translated
NATIVE = {"English", "English (UK)", "Deutsch", "Español", "Français", "Italiano", "Magyar", "Polski", "Română", "Català", "Čeština", "Galego",
          "Nederlands", "Norsk nynorsk", "Português (Brasil)", "Svenska", "Türkçe", "Kurdî", "Biełaruskaja", "Русский", "Spanish", "Japanese", "Tiếng Việt", "Google Sans Flex", "0123456789abcdefABCDEF", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:+", "0123456789abcdef", "End4pC Material Symbols", "End4pC Material Symbols Fill", "Play3r"}

LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
COMMENT = re.compile(r'//.*$')

def keep(s: str) -> bool:
    if len(s) < 2 or not re.search(r'[A-Za-z]{2}', s): return False
    if not (s[0].isupper() or s[0].isdigit()): return False       # Keys, commands, CSS names and paths start with a lowercase letter
    if re.search(r'[{}$<>=\\]|%[sdfl0-9.]|://|\.(png|svg|json|txt|sh|py|lua|ttf|tsv|inc|h)\b|^[A-Z0-9_]+$', s): return False
    if re.fullmatch(r'[A-Z][A-Za-z0-9]*', s) and any(c.isupper() for c in s[1:]) and ' ' not in s: return False  # CamelCase ids
    if re.match(r'^(Sans|Monospace)( Bold| Light| Medium)?( [\d.]+)?$|^(Mem|Swap)\w+:$|^Ag$|^Desktop Entry$|^Advanced Micro|^Logitech|^Linux ?$|^Arch$|^Adwaita$', s): return False  # fonts, /proc names, brands
    if re.match(r'^(Ctrl|Shift|Alt|Super)\+', s): return False        # keyboard shortcuts shown next to a tooltip are not translated
    if s in NATIVE: return False
    return True

def collect() -> list[str]:
    found: dict[str, None] = {}
    for f in sorted(SRC.glob("**/*")):
        if f.suffix not in (".cpp", ".inc", ".h"): continue
        if f.name in ("mpris.cpp", "engine.cpp"): continue  # D-Bus and PipeWire names, no texts for the user
        for line in f.read_text(errors="ignore").splitlines():
            code = COMMENT.sub("", line) if '"//' not in line and '://' not in line else line
            for m in LIT.finditer(code):
                s = m.group(1).encode().decode("unicode_escape", errors="ignore") if "\\" in m.group(1) else m.group(1)
                s = s.encode("latin-1", errors="ignore").decode("utf-8", errors="ignore") if "\\" in m.group(1) else s
                if keep(s): found[s] = None
    extra = ROOT / "tools" / "i18n-extra.txt"
    if extra.exists():
        for l in extra.read_text().splitlines():
            if l.strip() and not l.startswith("#"): found[l.strip()] = None
    return sorted(found, key=str.lower)

def main():
    texts = collect()
    print(f"{len(texts)} texts")
    if "--write" not in sys.argv: 
        if "--list" in sys.argv: print("\n".join(texts))
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for lang in ["en"] + LANGS:
        p = OUT / f"{lang}.json"
        old = json.loads(p.read_text()) if p.exists() else {}
        new = {t: old.get(t, t) for t in texts}      # A new text starts as its English value, meaning it is not translated yet
        p.write_text(json.dumps(new, ensure_ascii=False, indent=1) + "\n")
    print(f"wrote {len(LANGS) + 1} files in {OUT}")

main()
