[← end4pC docs](README.md)

# Troubleshooting

| Symptom | What to check |
|---|---|
| Nothing appears | Run `~/.config/end4pC/build/end4pC` in a terminal and read the output. Make sure no other bar or notification daemon is running |
| `git clone` says the folder exists | The shell is already there: use `end4pC update`, or `rm -rf ~/.config/end4pC` first (your settings are in `~/.local/share/end4pC`, not in that folder) |
| No blur behind the bar | Check that the layer rules from `hyprland/end4pC.lua` loaded: `hyprctl layers`, then `hyprctl reload` |
| A button does nothing | The tool it calls is probably not installed (see [Requirements](install.md#to-run)) |
| Binds do not work | Another bind may use the same keys; Hyprland runs every bind on a key. Check Tools > Keybinds |
| The commands print nothing | The shell is not running: `end4pC ping` should answer `pong` |
