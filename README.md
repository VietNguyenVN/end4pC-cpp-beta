# end4pC

A light desktop shell for [Hyprland](https://hyprland.org), [niri](https://github.com/YaLTeR/niri) and [sway](https://github.com/WillPower3309/swayfx) (SwayFX), written in C++ on the Wayland layer-shell protocol. It draws the bar, the
launcher, notifications, the control center, the media player, the wallpaper (with transitions), the lock screen, desktop widgets and its
own Settings app. No Qt, no QML, no GTK widgets: just cairo + pango, one small process.

It started as a C++ port of my Quickshell config and keeps its Material look.

> **Beta.** The project changes quickly and things may break.

Tested on Arch Linux with Hyprland 0.57.1 (Lua config); niri and sway (SwayFX) are supported too.

---

## What is in it

- **Bar** (top, bottom, left or right) with several styles: Panel, Float, Island, Hug, Notch and Split hug. Workspaces, media, clock (8
  looks), system resources, quick actions, tray, network, volume, battery, notifications, launcher and power buttons, weather, the Dock's apps inside the bar ("Dock to panel": click to launch or cycle windows, middle click for a new window, right click to pin, drag to reorder), AI usage (Claude Code and Codex, read from the files they leave on your machine) and dividers (a soft line or a dot).
- **Launcher**, **power menu**, **control center**, **notification popups and panel**, **media player card** with lyrics, **Alt+Tab
  window switcher** with live previews.
- **Wallpaper selector** (local folders, favourites, Wallhaven / Unsplash / Pexels), light/dark and generated palettes (matugen) plus
  ready-made themes.
- **Screenshot and screen-record tool** with annotations, audio and fps options.
- **Desktop widgets**: clock (many faces), weather, to-do, calendar, player, resources, user card, image.
- **Lock screen** (`ext-session-lock`, checks the password with PAM).
- **Password dialog** (polkit authentication agent): the shell asks for the password itself when a program needs administrator rights. Do not
  run another agent (polkit-kde, polkit-gnome, hyprpolkitagent...) at the same time: only one can be registered per session.
- **Settings app** for almost everything; it saves to `~/.local/share/end4pC/config.json`, apart from the project.

---

## Requirements

### To build

On Arch (package names):

```
meson ninja gcc pkgconf
wayland wayland-protocols wlr-protocols
cairo pango gdk-pixbuf2 librsvg
libxkbcommon pam systemd-libs
```

The build looks for the protocol XML files in `/usr/share/wayland-protocols` and `/usr/share/wlr-protocols`.

### To run

- `hyprland` **0.55 or newer** (the Hyprland rules and binds in this repo are written in Lua).
- Tools the shell calls. Most are optional: whatever is missing only disables the feature that uses it.

| Feature | Packages |
|---|---|
| Palette from the wallpaper | `matugen`, `python` |
| AI usage in the bar | `python` (reads `~/.claude` and `~/.codex`; nothing is sent anywhere) |
| Screenshots, recording | `grim`, `slurp`, `wf-recorder`, `wl-clipboard`, `swappy` (optional editor), `jq` |
| Audio, microphone | `wireplumber` (`wpctl`), `pipewire-pulse` (`pactl`) |
| Media player | `playerctl`, `cava` (optional visualizer) |
| Wi-Fi, Bluetooth, battery | `networkmanager`, `bluez-utils`, `upower` |
| Power profile | `power-profiles-daemon` |
| Brightness | `ddcutil` (external monitors) or `brightnessctl` |
| Online wallpaper keys | `libsecret` (`secret-tool`) and a keyring such as `gnome-keyring` |
| Clipboard history | `cliphist` |
| File choosers, thumbnails | `zenity`, `imagemagick` |
| Weather | `curl` (Open-Meteo, no key; the city is found by your IP unless you set one) |
| Night light | `hyprsunset` or `wlsunset` |
| Updates counter | `pacman-contrib` (`checkupdates`), `paru` or `yay`, `flatpak` |

Stop any other bar / notification daemon / wallpaper tool first (waybar, mako, swaync, swww, Quickshell...). end4pC owns the
notification service, the tray and the wallpaper.

---

## Install

The shell has to live in **`~/.config/end4pC`**: the Hyprland, niri and sway files of this repository point there.

### Arch Linux: the installer

```bash
git clone https://github.com/pctrade/end4pC-cpp-beta.git ~/.config/end4pC
~/.config/end4pC/installer/install.sh --dry-run    # shows what it would do, changes nothing
~/.config/end4pC/installer/install.sh              # asks before it does anything
```

It installs the packages, sets up the compositors you pick (Hyprland, niri, sway) with their configs and the dotfiles, and builds the
shell. A config folder you already have is moved to `~/.local/share/end4pC-installer/backup-<date>/`, never deleted. See
[installer/README.md](installer/README.md) for everything it does and its options.

### Any other distribution: build it yourself

```bash
git clone https://github.com/pctrade/end4pC-cpp-beta.git ~/.config/end4pC
cd ~/.config/end4pC
meson setup build
ninja -C build
```

This produces, in `build/`:

| Binary | What it is |
|---|---|
| `end4pC` | The shell (bar, popups, wallpaper, widgets). It is also its own command line: `end4pC "settings"`, `end4pC "set barStyle notch"`... |
| `end4pC-settings` | The Settings window |
| `end4pC-tools` | The Tools window |
| `end4pC-lock` | The lock screen |

Then connect Hyprland to it. Add this block to your `~/.config/hypr/hyprland.lua`:

```lua
-- end4pC shell: layer rules, keybinds and autostart
do
    local ok, err = pcall(dofile, os.getenv("HOME") .. "/.config/end4pC/hyprland/end4pC.lua")
    if not ok then print("end4pC: " .. tostring(err)) end
end
```

[`hyprland/end4pC.lua`](hyprland/end4pC.lua) holds the layer rules (blur, animations), the window rule of the Settings app, the key
binds, and it **starts the shell with the session**. Read it before using it: some binds replace the ones you may already have
(`SUPER+L`, `SUPER+P`).

If something only applies to your machine, put it in `hyprland/local.lua`: it is loaded when it exists and is not in git.

Reload Hyprland (`hyprctl reload`) or log out and in.

### Update

```bash
end4pC update              # shows what is new and asks before applying it
end4pC update --check      # only says whether there is something new
end4pC update --rollback   # goes back to the version that ran before the last update
end4pC --version
```

It follows the `stable` branch of the repository (`git fetch` + fast-forward, then `ninja`), and it only touches the shell's own code and
assets. Your settings (`~/.config/end4pC-user`), your dotfiles and your compositor configs are never changed. A checkout with changes of
its own is not updated: it tells you instead of overwriting them. Restart the shell afterwards (`SUPER + R`).

### Lock screen

The lock screen checks your password through PAM. It uses `/etc/pam.d/end4pC-lock` if it exists, otherwise `hyprlock`, otherwise
`login`, so on most systems it works without setup.

---

## Launch

After the block above is in your Hyprland config, **the shell starts by itself** when Hyprland starts.

To start it by hand (for example to try it now):

```bash
~/.config/end4pC/build/end4pC
```

To restart it after rebuilding, kill the old one first:

```bash
pkill -x end4pC; ~/.config/end4pC/build/end4pC &
```

Open the Settings app from the bar's gear button, with `SUPER + Escape`, or:

```bash
~/.config/end4pC/build/end4pC "settings"
```

---

## Keybinds

| Keys | Action |
|---|---|
| `SUPER` (tap) | Launcher |
| `SUPER + W` | Wallpaper selector |
| `SUPER + P` | Power menu |
| `SUPER + L` | Lock screen |
| `SUPER + R` | Restart the shell (also the restart button of the control center, or `end4pC "restart"`); it works even if the shell is stuck |
| `SUPER + M` | Media player |
| `SUPER + Escape` | Settings |
| `SUPER + SHIFT + S` | Screenshot (region, with annotations) |
| `ALT + Tab` / `ALT + SHIFT + Tab` | Window switcher (let go of Alt to pick) |
| Brightness keys | Brightness (with the on-screen bar) |

More are listed, commented out, at the end of `hyprland/end4pC.lua`.

---

## Configuration

Your settings live in `~/.local/share/end4pC/` (or `$XDG_DATA_HOME/end4pC/`), **not** in the project folder: `config.json`,
your own themes in `themes/` and your own launcher actions in `actions/`. The folder is created the first time the shell runs; without a
`config.json` the shell uses its defaults. (Older versions kept it inside the project or in `~/.config/end4pC-user`: it is copied over once, nothing is deleted.)
You normally change it from Settings, but you can also use the command line:

```bash
end4pC "set barPosition top"
end4pC "set barStyle split"
end4pC "set fakeRounding 1"
end4pC "config"            # print the current values
```

Wallpapers are read from `~/Pictures/Wallpapers` by default (change it in Settings -> Desktop).

The Hyprland page of Settings writes its options to `~/.config/hypr/hyprland/shellOverrides/` (created if missing), and the monitor and
idle options to `~/.config/hypr/monitors.lua` and `hypridle.conf`.

---

## Languages

Settings -> General -> Language picks the language of the shell, the Settings window and the lock screen (default: the system language).
The texts live in `assets/i18n/<lang>.json`, one flat file per language: `"English text": "translated text"`. Every language file exists
already as a template (the value is still the English text, which means "not translated yet" and shows English). To translate, edit the
value; keep the key, and keep spaces at the end of a text and the `·` / `•` marks. Your own fixes go in
`~/.local/share/end4pC/i18n/<lang>.json` and win over the project's file. After adding texts to the code run
`python3 tools/i18n-extract.py --write` (it keeps what is already translated).

---

## Troubleshooting

- **Nothing appears**: run `~/.config/end4pC/build/end4pC` in a terminal and read the output. Make sure no other bar or notification
  daemon is running.
- **No blur behind the bar**: check that the layer rules from `hyprland/end4pC.lua` loaded (`hyprctl layers`, then `hyprctl reload`).
- **A button does nothing**: the tool it calls is probably not installed (see the table above).
- **Binds do not work**: another bind may use the same keys; Hyprland runs every bind on a key.

---

## Credits

- Inspired by [end-4's dots-hyprland](https://github.com/end-4/dots-hyprland) (illogical-impulse) and its Material look.
- Palettes from the wallpaper: [matugen](https://github.com/InioX/matugen).
- Icons: [Solar Icon Set](https://github.com/480-Design/Solar-Icon-Set) (CC BY 4.0). Glyphs: Material Symbols. Shapes: Material 3 expressive shapes.
- Dotfiles of the installer: based on end-4's dots-hyprland (GPL-3.0). More in [THIRD_PARTY.md](THIRD_PARTY.md).

## License

end4pC is free software under the [GNU General Public License v3.0](LICENSE). Copyright (C) 2026 pctrade.

The icons, fonts and configs it includes keep their own licenses: see [THIRD_PARTY.md](THIRD_PARTY.md).
