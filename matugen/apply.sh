#!/usr/bin/env bash
# Usage: apply.sh /path/to/wallpaper [dark|light] [scheme-type] [#accent] [theme [primary [secondary [tertiary]]]]
# A theme (a name in schemes/ or ~/.local/share/end4pC/themes/) replaces the generated palette: the wallpaper only decides the picture.
# Writes the wallpaper path and regenerates the palette; the running shell picks it up via inotify.
# With an accent colour the palette is built from that colour instead of the wallpaper.
set -euo pipefail
wall="$(realpath "$1")"
mode="${2:-dark}"
type="${3:-scheme-neutral}"
accent="${4:-}"
theme="${5:-}"
picks=("${6:-}" "${7:-}" "${8:-}")  # accent names inside the theme
state="${XDG_STATE_HOME:-$HOME/.local/state}/end4pC"
mkdir -p "$state/generated"
cfg="$HOME/.config/end4pC/matugen/config.toml"
if [[ "$theme" =~ ^[A-Za-z0-9_-]+$ ]] && python3 -I "$HOME/.config/end4pC/matugen/named_scheme.py" --apply "$theme" "$mode" "${picks[@]}"; then
    :  # the theme's own colours are in place
elif [[ "$accent" =~ ^#[0-9a-fA-F]{6}$ ]]; then
    # Neutral is nearly grey by design, so an accent would barely show: it gets Tonal Spot instead (Monochrome stays grey).
    [ "$type" = scheme-neutral ] && type=scheme-tonal-spot
    matugen color hex "$accent" --mode "$mode" --type "$type" -c "$cfg" >/dev/null
else
    matugen --source-color-index 0 image "$wall" --mode "$mode" --type "$type" -c "$cfg" >/dev/null
fi
printf '%s\n' "$wall" > "$state/wallpaper.txt"
# Tell a running shell (the binary is usually not in PATH, so try the build too). The selector already does this
# itself; this keeps the script useful from a terminal.
bin="$(command -v end4pC || true)"
[ -n "$bin" ] || bin="$HOME/.config/end4pC/build/end4pC"
"$bin" wallpaper "$wall" >/dev/null 2>&1 || true
# The palette written for the other programs (kitty...): only the templates switched on in Settings -> Appearance -> Templates.
python3 -I "$HOME/.config/end4pC/scripts/templates.py" >/dev/null 2>&1 || true
# The user's own matugen templates (~/.config/matugen/config.toml, for the apps the shell has no template for): the same wallpaper,
# mode and type. With an accent, or a named theme (which matugen cannot read), they get a palette built from that colour instead.
user_cfg="${XDG_CONFIG_HOME:-$HOME/.config}/matugen/config.toml"
if [ -f "$user_cfg" ] && command -v matugen >/dev/null 2>&1; then
    src=""
    if [[ "$accent" =~ ^#[0-9a-fA-F]{6}$ ]]; then src="$accent"
    elif [[ "$theme" =~ ^[A-Za-z0-9_-]+$ ]]; then
        src="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("primary",""))' "$state/generated/colors.json" 2>/dev/null || true)"
    fi
    if [[ "$src" =~ ^#[0-9a-fA-F]{6}$ ]]; then
        [ "$type" = scheme-neutral ] && type=scheme-tonal-spot
        matugen color hex "$src" --mode "$mode" --type "$type" -c "$user_cfg" >/dev/null 2>&1 || true
    else
        matugen --source-color-index 0 image "$wall" --mode "$mode" --type "$type" -c "$user_cfg" >/dev/null 2>&1 || true
    fi
fi
