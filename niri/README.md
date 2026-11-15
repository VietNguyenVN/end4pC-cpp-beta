# niri files

- `end4pC.kdl`: starts the shell and the layer rules. Include it from `~/.config/niri/config.kdl`.
- `end4pC-session.kdl`: the session environment (Qt/KDE theme, Electron on Wayland, the KDE menu) and services (D-Bus
  activation environment, keyring, clipboard history, geoclue, GTK cursor from the KDE settings). niri sets none of this by
  itself; include it so the shell works whatever the rest of config.kdl holds.
- `end4pC-binds.kdl`: the shell's key binds (include it last: later binds win).
- `end4pC-look.kdl`: rounding, gaps and the rules of the shell's own windows.
- `~/.config/niri/end4pC-colors.kdl`: window border colors, written by matugen from the wallpaper.
- `~/.config/niri/end4pC-user.kdl`: what Settings → Niri changes (gaps, borders, rounding, animations, key repeat, per-app rules).
  It is written by `scripts/niriconf.py`, which checks every change with `niri validate` and puts the file back if niri would
  refuse it. The first change adds one `include` line at the end of `config.kdl` (a copy is kept as `config.kdl.end4pC-backup6`).
  "Reset all" on that page restores the file as it was before the first change. Do not edit it by hand: it is rewritten.
