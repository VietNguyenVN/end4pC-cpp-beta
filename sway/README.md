# sway (SwayFX) files

- `end4pC.sway`: the blur behind the shell's panels (`layer_effects`). Include it from `~/.config/sway/config`:
  `include ~/.config/end4pC/sway/end4pC.sway`.
- Start the shell from the sway config with `exec ~/.config/end4pC/build/end4pC run` (it does nothing when it is already running).
- The shell talks to sway through `swaymsg` (workspaces, windows, focus, keyboard layouts, outputs) and a `swaymsg -m -t subscribe`
  child for the events (src/parts/13c_compositor.inc, src/parts/16_registry_ipc.inc). It runs when `SWAYSOCK` is set and
  neither `HYPRLAND_INSTANCE_SIGNATURE` nor `NIRI_SOCKET` is.
- Limits: the window switcher shows no thumbnails (grim cannot capture one sway window by id here), the focus order of the
  switcher follows sway's focus lists (the focused window first, then the rest of its workspace, then the other workspaces by use),
  and the Settings page of sway is a placeholder (the bar, the widgets, the dock and the rest of the pages work).

## Title bars: the shell's colours and a glyph per app

- `scripts/sway-palette` writes `~/.config/sway/end4pC-colors` (`$xbg`, `$xfg`, `$x0`..`$x15`, the names the sway config always used) from the
  shell's palette (`~/.local/state/end4pC/generated/colors.json`: wallpaper, mode, scheme or theme). `~/.config/sway/config` includes that file
  instead of the original `colors`.
- `scripts/title-glyphs` is a small daemon (start it from the sway autostart: `exec ~/.config/sway/scripts/title-glyphs`; one instance, it
  ends with sway). It sets every window's `title_format` to a big dot in the app's colour with the app's Material Symbols glyph drawn inside it (the filled glyph, dark on the dot), followed by the title, and it
  watches the palette: when the shell's colours change it runs `sway-palette`, reloads sway and re-applies the glyphs.
  `title-glyphs --refresh` asks it to re-apply now.
- `app-glyphs.tsv` maps an app (regex on the app id, or the X11 class) to a glyph (a Material Symbols name) and a palette slot. Add a line
  to add an app; the daemon picks it up within two seconds. Copy the three files to `~/.config/sway/` (and `scripts/` to `~/.config/sway/scripts/`).
- The glyph font is the system's `Material Symbols Rounded` (package `ttf-material-symbols-variable`, already in `installer/packages/aur.txt`).
- The pill is made of three pango spans (`scripts/title-glyphs` has the constants and the markup): a `●` in `DejaVu Sans 18` whose negative
  `letter_spacing` pulls the next span back over its centre, the glyph in `Material Symbols Rounded 12 @FILL=1`, and the title in
  `Google Sans Flex 11` raised 1 px. Measured on the 28 px title bar (`titlebar_padding 2 4` (the first number, the horizontal padding, only moves the pill sideways; the vertical 4 sets the bar height)): 5 px above and below the dot, dot, glyph
  and the title's x-height all centred on the middle of the bar (row 14), glyph within 1 px of the dot's centre horizontally. If you
  change the title font or `titlebar_padding`, retune `DOT_RISE`, `GLYPH_RISE`, `TITLE_RISE` and `DOT_PULL` in the script.
- No `for_window ... title_format` rule in the config on purpose: sway runs `for_window` again on every reload and would wipe the glyphs.
