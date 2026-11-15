# end4pC installer

A light Wayland shell written in C++ (bar, launcher, notifications, wallpaper, widgets, Settings) for **Hyprland, niri and sway
(SwayFX)**, with the dotfiles it was made to work with. **Arch Linux only.**

```bash
git clone https://github.com/pctrade/end4pC-cpp-beta.git ~/.config/end4pC
~/.config/end4pC/installer/install.sh --dry-run    # shows what it would do
~/.config/end4pC/installer/install.sh              # asks before it does anything (including which compositors)
~/.config/end4pC/installer/install.sh --compositors niri,sway     # no question about it: hyprland, niri, sway or all
```

(Or, from a zip made with `tools/make-installer.sh`: `unzip end4pC-installer-*.zip && cd end4pC-installer && ./install.sh`.)

## What it does

0. **Compositors.** You pick Hyprland, niri, sway (SwayFX) or several of them (`1 3`, `all`; or `--compositors hyprland,niri,sway`).
   The default is Hyprland, and with only Hyprland picked the installer does what it always did. The shell runs the same on all of them.
1. **Packages.** The base ones in `packages/pacman.txt`, plus the ones of each compositor you picked
   (`packages/compositors/hyprland.txt`, `niri.txt`, `sway.txt`, and `sway.aur.txt` for SwayFX, which is only in the AUR), with
   `sudo pacman -S --needed` (what you already have is skipped; a name
   your repositories do not know is skipped with a warning). Then it shows the optional programs of `packages/apps.txt` (kitty, fish,
   btop, geany...) and you pick which ones you want, and you can type any other package you want to add. The Bibata cursor is built
   with `makepkg` from `packages/local/`, the same recipe the Quickshell setup (dots-hyprland) uses, because that package is not in
   the AUR. The optional ones in `packages/aur.txt` go with `yay` or `paru`, also with `--needed` (if there is no helper they are skipped).
2. **Dotfiles.** `kitty btop fish geany gtk-3.0 gtk-4.0 hypr matugen kde-material-you-colors Kvantum`, and the files `starship.toml` and `kdeglobals`, go to `~/.config`
   (`hypr` only when Hyprland was picked). A folder you already have is **moved** to
   `~/.local/share/end4pC-installer/backup-<date>/`, never deleted.
3. **The shell.** It goes to `~/.config/end4pC` and is built there with meson and ninja. If `~/.config/end4pC` is already a git
   checkout its source is left as it is and only the build runs.
4. **Compositor setup** (niri and sway; Hyprland is the `hypr` folder of step 2). The configs are in `templates/`:
   - **niri**: `config.kdl` (inputs, layout, key binds), `hyprland-parity.kdl`, `end4pC-colors.kdl` and an empty `end4pC-user.kdl` (what
     Settings -> Niri writes) go to `~/.config/niri`. The shell's own files stay in `~/.config/end4pC/niri` and the config *includes* them:
     `end4pC-session.kdl` (the Qt/KDE theme, Electron on Wayland, the keyring, the clipboard history...), `end4pC-look.kdl`, `end4pC.kdl`
     (starts the shell, layer rules) and `end4pC-binds.kdl` (the shell's keys), then the two files in `~/.config/niri`.
   - **sway**: the whole `~/.config/sway` tree (config, globals, keys, modes, rules, appearance, input, output, idle, autostart, bugfix, colours,
     the glyph list of the title bars and `scripts/`, with `start-sway`, `title-glyphs`, `sway-palette`...), included as is. It starts the shell
     and includes `~/.config/end4pC/sway/end4pC.sway` (the blur behind the panels).
   - **A config you already have** is never overwritten: the installer asks. *Keep* (the default; also what `-y` does) leaves your file as
     it is and only adds, at its end, the include lines above (niri) or the include of `end4pC.sway` and the line that starts the shell
     (sway); a copy of your file goes to the backup folder. *Replace* moves your folder to the backup folder and installs the template.
   - Both configs are checked at the end (`niri validate`, `sway -C`) and a warning says if the compositor refuses them.
   - niri and sway start apps through a few helper scripts of the Hyprland setup (`launch_first_available.sh`...): when Hyprland was not
     picked, those three are put in `~/.config/hypr/hyprland/scripts` (never over a file that exists).
5. **First colours.** A palette from the first picture in `~/Pictures/Wallpapers` (or a green accent), the kitty colours and, with sway,
   the colours of its title bars (`~/.config/sway/end4pC-colors`, kept up to date by `title-glyphs`).

Options: `-y` (no questions), `--compositors LIST` (`hyprland`, `niri`, `sway`, comma separated, or `all`), `--no-packages`, `--no-dots`
(it skips the niri and sway configs too), `--no-build`, `--skip-aur`, `--dry-run` (prints, for each compositor, what it would copy or add).

**SwayFX** (the sway of this package, for shadows, rounded corners and blur) is only in the AUR (`packages/compositors/sway.aur.txt`, `yay -S swayfx`).
Without an AUR helper the installer says so; the sway config needs it (plain sway reports those lines as errors). It replaces the `sway` package.

**niri** needs a version with `include` support (the one in the repositories has it).

## After it

Log out and pick the session you set up (the packages bring their own entries), or start it from a TTY:

| Compositor | From a TTY | What starts the shell |
|---|---|---|
| Hyprland | `start-hyprland` (inside a session: `hyprctl reload`) | `~/.config/hypr/hyprland.lua` loads `~/.config/end4pC/hyprland/end4pC.lua` |
| niri | `niri-session` | `spawn-at-startup` in `~/.config/end4pC/niri/end4pC.kdl` |
| sway | `~/.config/sway/scripts/start-sway` | `exec ... end4pC run` in `~/.config/sway/autostart` |

The binds are the same on the three (Super is the main key).

| Key | |
|---|---|
| `SUPER` (alone) | launcher |
| `SUPER + W` | wallpapers |
| `SUPER + M` | media player |
| `SUPER + Escape` | Settings |
| `SUPER + /` | Tools |

The `end4pC` command is linked in `~/.local/bin`: `end4pC run` starts the shell on its own (it keeps running if you close the terminal), `end4pC "settings"`, `end4pC "launcher toggle"`... talk to a running shell.

Desktop widgets (clock, weather, calendar, resources, player...) are switched on from the desktop menu (right click on the wallpaper). The
package ships no widget positions: each widget is placed for **your** screen, whatever its size (weather, calendar and resources stack
in the top right and spill into a column to the left on a small screen, the player and the clock go bottom left). A widget you drag
stays where you put it; if you change the resolution or the screen the widgets live on (Settings -> Desktop), the others are placed
again, and each screen remembers its own layout. The state is `~/.local/state/end4pC/<widget>.txt` (`pos`, `screen`, `pinned`).

Settings -> Appearance -> **Templates** writes the shell's colours for kitty, GTK and KDE.

Settings -> Appearance -> **Material Symbols** switches every icon of the shell (bar, panels, popups, Settings, Tools) to Google's Material
Symbols instead of the built-in Solar set. Nothing has to be installed: the package carries a 80 KB subset of the font
(`assets/fonts/MaterialSymbolsRounded-{Outline,Fill}.ttf`, Apache-2.0, licence next to it) and the name table
`assets/icons/material-map.tsv`; both are generated by `tools/gen-material-assets.py` (developers only: it needs python-fonttools and
the `ttf-material-symbols-variable` font). An icon without a Material glyph keeps the built-in look. `END4PC_ICON_LOG=1 end4pC` prints
the names that have none.

## Go back, and make a new package

- **Go back**: copy what you want from `~/.local/share/end4pC-installer/backup-<date>/` over `~/.config/<folder>`. For a niri or sway config you kept,
  delete the lines the installer added at its end (a comment starts them: `end4pC shell (added by its installer)`).
- **Templates**: `tools/sync-installer-templates.sh` refreshes `installer/templates/{sway,niri}` from the live `~/.config/sway` and
  `~/.config/niri` of the machine that makes the package (verbatim; only the home folder becomes `@HOME@`, which `install.sh` puts back),
  and `tools/make-installer.sh` runs it first, then builds the zip and checks that nothing of the machine or its owner is inside.

## Know before you use it

- The hypr config started in another setup (dots-hyprland) and is cleaned of its Quickshell parts: the binds of the shell are in
  `~/.config/end4pC/hyprland/end4pC.lua`, and the rest are the usual window, workspace, volume and app keys.
- `hypr/monitors.lua` is generic (any monitor, preferred mode): put your own layout there.
- Stop any other bar, notification daemon or wallpaper tool before the first run (waybar, mako, swaync, swww, Quickshell...).
- The package has no wallpapers and no personal files: the first palette uses yours, or a default accent.
