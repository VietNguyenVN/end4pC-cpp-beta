#!/usr/bin/env bash
# Pick any colour on screen with hyprpicker and make it the shell's accent colour (Esc cancels).
hex="$(hyprpicker -b -f hex 2>/dev/null | head -n1)"
[[ "$hex" =~ ^#[0-9a-fA-F]{6}$ ]] || exit 0
bin="$(command -v end4pC || true)"
[ -n "$bin" ] || bin="$HOME/.config/end4pC/build/end4pC"
"$bin" "set accent $hex" >/dev/null 2>&1
