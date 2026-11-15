#!/usr/bin/env bash
# Refreshes installer/templates/{sway,niri} from this machine's working configs, so the installer ships what you actually use.
#
#   tools/sync-installer-templates.sh
#
# The copy is VERBATIM: nothing is rewritten or generalised. The only change is that the home folder (an absolute path of this machine)
# becomes the placeholder @HOME@, which install.sh puts back on the new machine. Generated files that depend on your palette
# (sway end4pC-colors, niri end4pC-colors.kdl) go along as they are: install.sh regenerates the sway one at the first start (sway-palette),
# and the niri one is replaced the first time the colours are written. Settings -> Niri's file (end4pC-user.kdl) goes as an empty one:
# it holds what you changed in Settings, which is yours. Caches, backups and notes of this machine are left out.
# At the end it prints every line that differs from the live config, and stops if a personal string is still in the templates.
set -euo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
T="$PROJECT/installer/templates"
mkdir -p "$T"

echo "==> sway  ($CONFIG/sway -> installer/templates/sway)"
[ -f "$CONFIG/sway/config" ] || { echo "no $CONFIG/sway/config"; exit 1; }
rm -rf "$T/sway"; mkdir -p "$T/sway"
cp -a "$CONFIG/sway/." "$T/sway/"
find "$T/sway" -name '__pycache__' -type d -prune -exec rm -rf {} +
find "$T/sway" \( -name '*.bak' -o -name '*.log' -o -name '*~' -o -name '*.orig' \) -delete

echo "==> niri  ($CONFIG/niri -> installer/templates/niri)"
[ -f "$CONFIG/niri/config.kdl" ] || { echo "no $CONFIG/niri/config.kdl"; exit 1; }
rm -rf "$T/niri"; mkdir -p "$T/niri"
for f in config.kdl hyprland-parity.kdl end4pC-colors.kdl; do
    [ -f "$CONFIG/niri/$f" ] && cp -a "$CONFIG/niri/$f" "$T/niri/$f"
done
chmod 644 "$T"/niri/* 2>/dev/null || true
cat > "$T/niri/end4pC-user.kdl" <<'KDL'
// end4pC: made by Settings -> Niri (scripts/niriconf.py). This file is rewritten on every change: edit it from there.
// state: {"rules":{},"values":{}}

KDL

echo "==> the home folder becomes @HOME@ (the only edit)"
subst_log="$(mktemp)"; trap 'rm -f "$subst_log"' EXIT
while IFS= read -r -d '' f; do
    if grep -q -F -- "$HOME" "$f"; then
        grep -n -F -- "$HOME" "$f" | sed "s|^|${f#$PROJECT/installer/templates/}:|" >> "$subst_log"
        sed -i "s|$HOME|@HOME@|g" "$f"
    fi
done < <(find "$T" -type f -print0)
if [ -s "$subst_log" ]; then echo "    lines substituted:"; sed 's/^/      /' "$subst_log"; else echo "    none: the configs hold no absolute path of this machine"; fi

echo "==> check: nothing personal left"
mapfile -t patterns < <("$PROJECT/tools/personal-patterns.sh")
bad=0
for pattern in "${patterns[@]}"; do
    hits="$(grep -rIn -F -e "$pattern" "$T" 2>/dev/null | head -5 || true)"
    if [ -n "$hits" ]; then echo "found '$pattern':"; echo "$hits"; bad=1; fi
done
[ "$bad" = 0 ] || { echo "!! the templates still have personal data"; exit 1; }

echo "==> difference from the live configs (besides the substitution and what is left out)"
for pair in "sway:sway" "niri:niri"; do
    n="${pair%%:*}"
    diff -rq "$CONFIG/$n" "$T/$n" 2>/dev/null | grep -v "__pycache__\|end4pC-backup\|config.kdl.end4pC" | sed 's/^/    /' || true
done
echo "done: $(find "$T" -type f | wc -l) files in installer/templates"
