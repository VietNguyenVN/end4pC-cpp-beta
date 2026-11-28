<div align="center" markdown="1">

# end4pC++

**A light, native desktop shell for Wayland, written in C++.**
Bar · launcher · notifications · control center · wallpaper · lock screen · widgets · its own Settings app.
No Qt, no QML, no GTK widgets: just `cairo` + `pango` in one small process.

![Hyprland](https://img.shields.io/badge/Hyprland-0.55%2B-58E1FF?style=flat-square)
![niri](https://img.shields.io/badge/niri-supported-9ece6a?style=flat-square)
![sway](https://img.shields.io/badge/SwayFX-supported-7aa2f7?style=flat-square)
![C++](https://img.shields.io/badge/C%2B%2B-meson%20%2B%20ninja-f7768e?style=flat-square)
![License](https://img.shields.io/badge/license-GPL--3.0-bb9af7?style=flat-square)
![Status](https://img.shields.io/badge/status-pre--alpha1-e0af68?style=flat-square)

[Install](#install) · [CLI reference](#command-line-reference) · [Keybinds](#keybinds) · [Configuration](#configuration) · [Languages](#languages) · [Troubleshooting](#troubleshooting)

</div>

<div align="center" markdown="1">

<img src="screenshots/1.png" alt="end4pC on Hyprland: bar, media player, weather, calendar and resources" width="100%">

</div>

---

It started as a C++ port of my Quickshell config and keeps its Material 3 look.

> [!WARNING]
> **Pre-alpha.** The project changes quickly and things may break. Tested on Arch Linux with Hyprland 0.57.1 (Lua config); niri and sway (SwayFX) are supported too.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Install](#install)
- [Launch](#launch)
- [Command line reference](#command-line-reference)
- [Keybinds](#keybinds)
- [Configuration](#configuration)
- [Languages](#languages)
- [Troubleshooting](#troubleshooting)
- [Credits and license](#credits)

---

## Features

| | |
|---|---|
| **Bar** | Top, bottom, left or right. Styles: Panel, Float, Island, Hug, Notch, Split hug. Workspaces, media, clock (8 looks), system resources, quick actions, tray, network, volume, battery, Bluetooth, keyboard layout, updates, notifications, launcher, power, weather, removable drives, AI usage (Claude Code and Codex, read from the files they leave on your machine), dividers. Every widget has Plain, Framed and M3 looks. The Dock can live inside the bar ("Dock to panel": click to launch or cycle windows, middle click for a new window, right click to pin, drag to reorder). |
| **Panels** | Launcher (apps, files with `fd`, clipboard, emoji, calculator, QR; in the clipboard, Space previews an entry, text with scroll or a picture, without closing the launcher, with Back and Copy buttons), power menu, control center, notification popups and panel, media card with lyrics, Alt+Tab switcher with live previews. |
| **Wallpaper** | Selector with local folders, favourites, Wallhaven / Unsplash / Pexels (a wallpaper you already downloaded is reused, not downloaded again); transitions; light/dark and generated palettes (matugen) plus ready-made themes. The right-click menu of the desktop has a carousel of wallpapers (its last item opens the selector), wallpaper options, and the switches of the widgets. |
| **Capture** | Screenshot and screen recording with annotations, audio and fps options. OCR of an area (`tesseract`), Google Lens search, QR reading and QR generation. |
| **Tools** | Translator, notes, a freeform board, image converter, video editor, unit and currency converter (about 160 currencies), timers, stopwatch and reminders that ring with the window closed, a key bind editor, an autostart manager (all the tabs can be hidden and reordered from the gear of the window; text fields work with Fcitx5 and other Wayland input methods), an Android phone page (adb and scrcpy), and Score: football matches with the chances of a statistical model, picks and a slip. Score is off until you turn it on in the gear of the Tools window, and it is for entertainment only. |
| **Play3r** | The music player, a program of its own (`end4pC-play3r`, or `end4pC play3r [song or folder]`). ffmpeg decodes, PipeWire plays, TagLib reads the tags and the cover; no toolkit. A folder with thousands of songs is in the queue at once (the tags are read in the background, from the rows on screen onwards). Cover on a Material shape, a wavy progress bar, synced lyrics (the same sources as the player of the bar), search, saved playlists, the queue of the last time, and MPRIS, so the bar, the media keys and `playerctl` see and control it. |
| **Fil3s** | The file manager, a program of its own (`end4pC-fil3s`, or `end4pC fil3s [folder]`), light on purpose: folders are read in a thread and thumbnails are decoded by workers, so a folder with thousands of files opens at once. Material 3 look, grid and list views, breadcrumbs, places you edit yourself (*Add to Places* in the menu of a folder to add, *Remove from Places* in the menu of a row, and a long press on a row to pick it up and move it), drives, instant search (just type), a Trash that follows the freedesktop spec, copy / cut / paste / rename, lasso, Shift and Ctrl selection, and the keys of any file manager. The right-click menu converts images (PNG, JPG, WebP, AVIF, GIF, BMP, TIFF, ICO, PDF), video (MP4, WebM, MKV, GIF, or just the audio) and audio (MP3, FLAC, Opus, OGG, M4A, WAV) with one click, and makes ZIP, 7z, TAR.GZ / XZ / ZST / BZ2 and RAR archives or extracts them; a bar shows the progress and the new file appears and is selected when it is done. |
| **T3xt** | The text editor, a program of its own (`end4pC-t3xt`, or `end4pC t3xt [file]`): tabs, a sidebar with the open files and the folder (toggle with the button or Ctrl+B), line numbers, find and replace, undo and redo, auto-indent, comment toggle, and syntax highlighting for C, C++, Java, JavaScript, TypeScript, Go, Rust, C#, Kotlin, Swift, Dart, QML, PHP, GLSL, KDL, Python, shell, Lua, JSON, HTML / XML, CSS / SCSS, Markdown, YAML, INI / TOML / conf, SQL, Makefile, CMake, Dockerfile and diffs. The code is coloured with the palette of the shell: the primary colour is the keywords and the other kinds are the same colour turned round the hue circle, so it follows the wallpaper and the theme. No terminal, no plugins. |
| **Task Manager** | Its own window (`SUPER + SHIFT + T`, `CTRL + SHIFT + Esc`, or the button in the resources popup). Processes: apps and processes with CPU, memory and disk, search, End task and Force kill with a confirmation. Performance: CPU (every core), memory, disks, network and GPU (Intel and AMD; NVIDIA is untested) with live charts. Battery and Devices tabs from `upower`. |
| **Desktop widgets** | Clock (many faces), weather, to-do, calendar, player, resources, user card (with the uptime), image, text, audio visualizer. In edit mode they snap to their neighbours and to an optional grid, show the gap between them, can be tidied into a column and resized from the corners. |
| **Accessibility** | Screen magnifier, bigger cursor, draggable on-screen keyboard that follows the focused monitor. |
| **System** | Lock screen (`ext-session-lock` + PAM; you choose which monitors show the full lock screen), polkit authentication agent, removable drives popup (`udisks2`), game overlay (with a volume mixer and a GPU row), per-monitor hot corners, volume limit and step (above 100% when you allow it), do-not-disturb. |
| **Settings app** | Almost everything is configurable from the GUI (also your display name and the hostname, the placement of the player and notification popups, and the odds provider of Score); it saves to `~/.local/share/end4pC/config.json`. |

> [!NOTE]
> The shell is also its own command line: every panel, widget and setting can be driven from a terminal or a key bind. See the [command line reference](#command-line-reference).


### Play3r: launching it (and for users who already have the dots)

- **Application menu / launcher:** look for **Play3r**. A fresh install, and `end4pC update`, add its entry (`~/.local/share/applications/end4pC-play3r.desktop`) by themselves when the player was built.
- **Terminal or key bind:** `end4pC play3r` (toggle), `end4pC play3r ~/Music/album` (play a song or folder).
- **Already had the dots before Play3r?** Install the three libraries once (`sudo pacman -S ffmpeg taglib pipewire`), then `end4pC update`. To only add the menu entry, or if you build by hand: `ninja -C build && scripts/install-desktop-entries.sh`.
- Play3r opens as a normal window (tiled, or floating if you float it); it has no window rule of its own.
- The bar shows the song and controls it through MPRIS; if the bar is set to follow another player (the `preferredPlayer` setting), change it to `play3r` to see Play3r there.
- Keys: type to search the queue, Up/Down/Enter to pick, Esc to clear the search, Ctrl+Q or Ctrl+W to close.

### Fil3s: launching it

- **Application menu / launcher:** look for **Fil3s**; `end4pC update` and the installer add its entry (`scripts/install-desktop-entries.sh` does the same by hand, for Play3r too). It needs no new libraries: it is built with the ones the rest of the shell already uses.
- **Terminal or key bind:** `end4pC fil3s` (your home folder), `end4pC fil3s ~/Pictures`. Bind it from Tools > Keybinds (Shell action: Fil3s; Play3r is there too).
- **Make it your default file manager (optional, nothing changes unless you run it):** `xdg-mime default end4pC-fil3s.desktop inode/directory`.
- **Right-click conversions and archives** use programs you may already have: `magick` (ImageMagick) for images, `ffmpeg` and `ffprobe` for video and audio, `zip`, `7z` (7zip), `tar` with `xz` / `zstd` / `bzip2`, `bsdtar` (libarchive) to extract; RAR can only be *made* with the non-free `rar` program (its entry shows when it is installed) but is extracted with `bsdtar` or `unrar`. An entry whose program is missing is not shown, or says which program is missing.
- Safety: nothing runs through a shell with a file name in it; names that start with `-` are never options; ImageMagick gets the format fixed from the extension (SVG is not accepted) and limits; ffmpeg is limited to local files; moving never overwrites; a folder is only extracted into a new folder of its own or without replacing files; thumbnails are made only for plain files of the common formats with a bounded size. Opening a file hands it to `xdg-open`.
- **Places:** they are yours: right-click a folder > *Add to Places* (or right-click an empty part of the folder you are in), right-click a place > *Remove from Places*, hold the left button on a place for half a second to pick it up and drag it to another position. The list is saved in `~/.local/share/end4pC/fil3s-places.txt` (one folder per line); *Reset Places* in the menu of the blank part of the sidebar brings the usual ones back.
- Keys: type to search, arrows move, Shift extends, Ctrl toggles (Ctrl+Space, Ctrl+arrows), Ctrl+A all, Enter opens, Backspace or Alt+Up goes up, Alt+Left / Right back and forward, F2 renames, Delete trashes (Shift+Delete deletes for good), Ctrl+C / X / V, Ctrl+H hidden files, Ctrl+1 / 2 grid and list, Ctrl+Shift+N new folder, F5 refresh, Ctrl+Q closes.

### T3xt: launching it

- **Application menu / launcher:** look for **T3xt**; `end4pC update` and the installer add its entry (it needs no new libraries). From a terminal or a key bind: `end4pC t3xt`, `end4pC t3xt ~/notes.md`; in Tools > Keybinds it is a Shell action. To open text files with it by default: `xdg-mime default end4pC-t3xt.desktop text/plain` (optional).
- **Keys:** Ctrl+S save (Ctrl+Shift+S save as), Ctrl+O open, Ctrl+N new, Ctrl+W close the tab, Ctrl+Tab next tab, Ctrl+B sidebar, Ctrl+F find (F3 / Shift+F3 next / previous), Ctrl+H replace, Ctrl+Z / Ctrl+Y undo / redo, Ctrl+/ comment, Ctrl+D duplicate the line, Alt+Up / Alt+Down move the line, Tab / Shift+Tab indent (a selection of lines too), Ctrl++ / Ctrl+- / Ctrl+0 and Ctrl+wheel for the size of the text, Shift+wheel sideways. Triple click selects a line, double click a word; click the numbers to select lines.
- **Files:** UTF-8 only (a file that is not valid UTF-8 opens read-only, so saving can never damage it); the line endings (LF / CRLF) are kept; saving writes a copy beside the file and renames it, so a failure never leaves half a file. Open and Save As use `zenity`; copy and paste use `wl-clipboard` (`wl-copy`, `wl-paste`).
- Accents and dead keys work (they go through the compose table of your locale). There is no input method support yet (Fcitx / IBus).

---

## Requirements

### To build

On Arch (package names):

```text
meson ninja gcc pkgconf
wayland wayland-protocols wlr-protocols
cairo pango gdk-pixbuf2 librsvg
libxkbcommon pam systemd-libs
ffmpeg taglib pipewire        # Play3r only
```

`ffmpeg`, `taglib` and `pipewire` are only for Play3r: without them everything else builds and Play3r is skipped (the build says so).

The build looks for the protocol XML files in `/usr/share/wayland-protocols` and `/usr/share/wlr-protocols`. It needs `wayland-protocols` 1.37 or newer: the overview and the window switcher use `ext-image-copy-capture`.

### To run

- `hyprland` **0.55 or newer** (the rules and binds in this repo are Lua), or niri, or sway/SwayFX.
- Helper tools. Most are optional: a missing one only disables the feature that uses it.

| Feature | Packages |
|---|---|
| Palette from the wallpaper | `matugen`, `python` |
| AI usage in the bar | `python` (reads `~/.claude` and `~/.codex`; nothing is sent anywhere) |
| Screenshots, recording | `grim`, `slurp`, `wf-recorder`, `wl-clipboard`, `swappy` (optional editor), `jq` |
| OCR, QR | `tesseract`, `zxing-cpp` or `zbar` (reading QR codes) |
| Audio, microphone | `wireplumber` (`wpctl`), `pipewire-pulse` (`pactl`) |
| Fil3s conversions and archives | `imagemagick`, `ffmpeg`, `zip`, `7zip`, `libarchive` (bsdtar), optionally `rar` |
| Media player, Play3r | `playerctl`, `cava` (optional visualizer); Play3r also uses `zenity` (folder chooser) and `python` (lyrics) |
| Wi-Fi, Bluetooth, battery | `networkmanager`, `bluez-utils`, `upower` |
| Power profile | `power-profiles-daemon` |
| Brightness | `ddcutil` (external monitors) or `brightnessctl` |
| Online wallpaper keys | `libsecret` (`secret-tool`) and a keyring such as `gnome-keyring` |
| Clipboard history | `cliphist` |
| File choosers, thumbnails | `zenity`, `imagemagick` |
| File search | `fd` |
| Removable drives | `udisks2` |
| Weather | `curl` (Open-Meteo, no key; the city is found by your IP unless you set one) |
| Night light | `hyprsunset` or `wlsunset` |
| Updates counter | `pacman-contrib` (`checkupdates`), `paru` or `yay`, `flatpak` |
| Score (Tools) | `python` (standard library only), `wl-clipboard` (to copy a booking code) |

> [!IMPORTANT]
> Stop any other bar, notification daemon or wallpaper tool first (waybar, mako, swaync, swww, Quickshell...). end4pC owns the notification service, the tray and the wallpaper. Do not run another polkit agent (polkit-kde, polkit-gnome, hyprpolkitagent...) at the same time: only one can be registered per session.

---

## Install

The shell has to live in **`~/.config/end4pC`**: the Hyprland, niri and sway files of this repository point there.

### Arch Linux: the installer

```bash
git clone https://github.com/pctrade/end4pC.git ~/.config/end4pC
~/.config/end4pC/installer/install.sh --dry-run    # shows what it would do, changes nothing
~/.config/end4pC/installer/install.sh              # asks before it does anything
```

It installs the packages, sets up the compositors you pick (Hyprland, niri, sway) with their configs and dotfiles, and builds the shell. A config folder you already have is moved to `~/.local/share/end4pC-installer/backup-<date>/`, never deleted. Details and options: [installer/README.md](installer/README.md).

### Any other distribution: build it yourself

```bash
git clone https://github.com/pctrade/end4pC.git ~/.config/end4pC
cd ~/.config/end4pC
meson setup build
ninja -C build
```

Binaries produced in `build/`:

| Binary | What it is |
|---|---|
| `end4pC` | The shell (bar, popups, wallpaper, widgets) **and** its own command line |
| `end4pC-settings` | The Settings window |
| `end4pC-tools` | The Tools window |
| `end4pC-play3r` | The music player |
| `end4pC-fil3s` | The file manager |
| `end4pC-t3xt` | The text editor |
| `end4pC-lock` | The lock screen |

<details>
<summary><b>Connect Hyprland</b></summary>

Add this block to your `~/.config/hypr/hyprland.lua`:

```lua
-- end4pC shell: layer rules, keybinds and autostart
do
    local ok, err = pcall(dofile, os.getenv("HOME") .. "/.config/end4pC/hyprland/end4pC.lua")
    if not ok then print("end4pC: " .. tostring(err)) end
end
```

[`hyprland/end4pC.lua`](hyprland/end4pC.lua) holds the layer rules (blur, animations), the window rule of the Settings app, loads the key binds ([`hyprland/binds.lua`](hyprland/binds.lua)) and **starts the shell with the session**. Read it before using it: some binds replace ones you may already have (`SUPER+L`, `SUPER+P`).

Machine-specific rules go in `hyprland/local.lua`: it is loaded when it exists and is not in git. Then `hyprctl reload`, or log out and in.

</details>

<details>
<summary><b>Connect niri or sway</b></summary>

See [niri/README.md](niri/README.md) and [sway/README.md](sway/README.md).

</details>

### Update

```bash
end4pC update              # shows what is new and asks before applying it
end4pC update --check      # only says whether there is something new
end4pC update -y           # applies it without asking
end4pC update --rollback   # goes back to the version that ran before the last update
end4pC --version
```

It follows the `stable` branch (`git fetch` + fast-forward, then `ninja`) and only touches the shell's own code and assets. Your settings, dotfiles and compositor configs are never changed. Shell files you edited yourself are set aside instead of overwritten. Restart the shell afterwards (`SUPER + R`).

### Lock screen

The lock screen checks your password through PAM. It uses `/etc/pam.d/end4pC-lock` if it exists, otherwise `hyprlock`, otherwise `login`, so on most systems it works without setup.

---

## Launch

Once the block above is in your Hyprland config, **the shell starts with the session**. By hand:

```bash
end4pC run                          # detached; does nothing if it is already running
~/.config/end4pC/build/end4pC       # foreground, prints its log (useful when debugging)
end4pC restart                      # or SUPER + R, works even if the shell is stuck
```

Open Settings from the bar's gear, with `SUPER + Escape`, or `end4pC settings`.

---

## Command line reference

`end4pC` with no arguments starts the shell. With arguments it **talks to the running shell** over a socket and prints the reply, so everything here works from a terminal, a key bind or a script.

> [!IMPORTANT]
> **Put the whole command in quotes** when it has more than one word: `end4pC "volume up"`, `end4pC "brightness set 70"`, `end4pC "set dock 1"`. Without the quotes only the first word reaches the shell (`end4pC volume up` is read as `end4pC volume`). Commands of one word, like `end4pC settings` or `end4pC ping`, need no quotes. Every example below with arguments is meant to be written this way.

```text
end4pC                          start the shell (foreground)
end4pC run                      start it detached (no-op if already running)
end4pC <command> [args]         send a command to the running shell
end4pC --version                installed version
end4pC help                     the built-in summary
```

### Shell and settings

| Command | Does |
|---|---|
| `ping` | Prints `pong` when the shell runs |
| `restart` / `quit` | Restart or stop the shell |
| `config` | Print the settings in use |
| `set <key> <value>` | Change any key of `config.json` and apply it live |
| `applytheme [image]` | Regenerate the palette from the wallpaper (no path: the one in use) |
| `wallpaper <image>` | Change the wallpaper |
| `collage [args]` | The wallpaper collage |
| `shapes` | List the Material 3 shapes accepted by the `*Shape` settings |
| `accentpick` | Pick an accent color from the screen |
| `update [--check\|-y\|--rollback]` | Update the shell (see [Update](#update)) |

```bash
end4pC set barPosition bottom
end4pC set barStyle notch
end4pC set selectorShape cookie9
end4pC set dock 1
```

### Panels and popups

All take `toggle` (the default), `open` or `close` unless noted.

| Command | Panel |
|---|---|
| `launcher [toggle\|open\|close\|clipboard\|emoji\|query <text>]` | Launcher; `clipboard` and `emoji` open it in that mode |
| `selector` | Wallpaper selector |
| `control [toggle\|open\|close\|wifi\|bt]` | Control center (`wifi` / `bt` jump to that page) |
| `media` | Media player card |
| `panel` | Notification panel |
| `power` | Power menu |
| `switcher [open\|next\|prev\|commit\|close]` | Alt+Tab window switcher |
| `clockpanel`, `weatherpanel`, `aipanel`, `respanel`, `batterypanel` `[toggle\|open\|close\|tab N]` | Popups of the bar's widgets |
| `audiomenu [open\|close\|tab output\|input\|apps]` | Volume popup |
| `desktopmenu [open [x y]\|close]` | The desktop's right-click menu |
| `drives` | Removable drives popup (only while a drive is plugged in) |
| `game [open\|close]` | Game overlay |
| `settings` | Settings window |
| `tools` | Tools window |
| `play3r [song or folder]` | The music player: opens it (closes it when open); with a path it plays it |
| `fil3s [folder]` | The file manager: a new window, on that folder if given |
| `t3xt [file]` | The text editor: a new window, with that file open if given |
| `tasks` | Task Manager window |
| `osd volume \| osd brightness` | Show the on-screen display |

### Desktop widgets

| Command | Does |
|---|---|
| `weather\|player\|calendar\|resources\|image\|clock\|todo\|usercard\|text\|visualizer [toggle\|show\|hide]` | Show or hide one widget |
| `widget <name> on\|off [W H LEFT TOP]` | Show or hide it, with that size and position |
| `widgets lock\|unlock` | Lock or unlock the edit mode (drag and resize) |
| `usercard image [path]` | The user card's picture (no path: back to the wallpaper) |

### Bar, dock, tray and hardware

| Command | Does |
|---|---|
| `dock [on\|off\|toggle]` | The dock |
| `tray` | List the tray items |
| `tray match` | Which window each tray item maps to |
| `tray click <n>` | Click tray item `n` |
| `tray open` / `tray close` | Expand or collapse the tray |
| `brightness [get\|up\|down\|set N]` | Screen brightness |
| `volume [get\|up\|down\|set N\|mute\|unmute\|toggle]` | Volume of the default speakers (`up` and `down` use the step of the Settings; the answer is the volume it ends at) |
| `dnd [on\|off\|toggle]` | Do not disturb |
| `updates [check]` | Check pending updates now |
| `quick <n>` | Run quick action `n` of the bar |
| `identify [NAME=N ...]` | Flash a number on each screen (and assign names) |
| `lock` | Lock the screen |

### Capture, OCR and QR

| Command | Does |
|---|---|
| `screenshot [region\|screen\|window\|edit]` | Take a screenshot (default: `region`, with annotations) |
| `screenshot ocr` | Select an area and copy its text (needs `tesseract`) |
| `screenshot lens` | Select an area and search it with Google Lens |
| `screenshot qr` | Select an area and read the QR code in it |
| `qr <text>` | Show the QR code of a text or link |
| `recording` | Prints `recording` or `idle` |

### Accessibility

| Command | Does |
|---|---|
| `osk [toggle\|open\|close]` | On-screen keyboard; it opens on the focused monitor |
| `magnifier [get\|in\|out\|reset\|set N]` | Screen magnifier (Hyprland cursor zoom) |

### Timers, stopwatch, reminders

They keep running and ring with every window closed.

```bash
end4pC "timer add 25:00 pomodoro"     # 5m, 1h30m, 90 (seconds) or MM:SS
end4pC "timer pause|resume|reset|remove|clear|list"
end4pC "stopwatch start|pause|reset|lap|get"
end4pC "reminder add 08:30 weekdays stand up"   # once | daily | weekdays | YYYY-MM-DD
end4pC "reminder remove|toggle|list"
```

### Introspection (for scripts)

| Command | Prints |
|---|---|
| `workspace` | The current workspace state |
| `music` | The current media player state |
| `notifs` | The pending notifications |
| `recording` | Whether a recording is running |
| `config` | The main settings in use |

### Debug

Fake states for testing the bar; they last until the next real event.

```text
network <kind>                       force the network icon
notifications <n>                    force the notification count
periph <pct> <state> <kind> [name]   fake a peripheral battery; `periph off` clears it
```

> [!TIP]
> Quote the command when it has arguments so the shell hands it over as one string: `end4pC "launcher query firefox"`.

---

## Keybinds

| Keys | Action |
|---|---|
| `SUPER` (tap) | Launcher |
| `SUPER + W` | Wallpaper selector |
| `SUPER + P` | Power menu |
| `SUPER + L` | Lock screen |
| `SUPER + R` | Restart the shell |
| `SUPER + M` | Media player |
| `SUPER + Escape` | Settings |
| `SUPER + /` | Tools |
| `SUPER + SHIFT + S` | Screenshot (region, with annotations) |
| `SUPER + =` / `SUPER + -` | Magnifier in / out (Hyprland) |
| `ALT + Tab` / `ALT + SHIFT + Tab` | Window switcher (let go of Alt to pick) |
| Brightness keys | Brightness, with the on-screen bar |

That is only a taste: there are about a hundred binds (windows, workspaces, volume, media, apps...) and **all of them are yours to change**.

Open **Tools > Keybinds** (`SUPER + /`): search them, turn any on or off, edit or delete them, add new ones. To add one you press the shortcut itself, mouse buttons and wheel included, and pick what it does: run a command, a shell action, or a window / workspace action. Two binds on the same keys are flagged. Every change is saved and Hyprland reloads at once; delete has an undo and restore brings the originals back.

The binds are one plain table: `hyprland/binds.default.tsv` ships with the shell and your copy is `~/.config/hypr/keybinds.tsv` (created the first time you change something; delete it to go back to the originals). Binds written by hand in Lua still work: put them in `~/.config/hypr/custom/keybinds.lua`.

---

## Configuration

Your settings live in `~/.local/share/end4pC/` (or `$XDG_DATA_HOME/end4pC/`), **not** in the project folder:

| Path | Holds |
|---|---|
| `config.json` | Every setting. Without it the shell uses its defaults |
| `themes/` | Your own themes |
| `actions/` | Your own launcher actions |
| `i18n/<lang>.json` | Your own translation fixes |

The folder is created the first time the shell runs. Older versions kept it inside the project or in `~/.config/end4pC-user`: it is copied over once and nothing is deleted.

You normally change it from Settings, but `set` does the same from a terminal:

```bash
end4pC "set barPosition top"
end4pC "set barStyle split"
end4pC "set fakeRounding 1"
end4pC config
```

Wallpapers are read from `~/Pictures/Wallpapers` by default (Settings > Desktop). The Hyprland page of Settings writes to `~/.config/hypr/hyprland/shellOverrides/`, and the monitor and idle options to `~/.config/hypr/monitors.lua` and `hypridle.conf`.

### Score (Tools)

A page of football matches (21 leagues from ESPN's public scoreboard) with the chances of a statistical model of my own, a detail view for each match, the algorithm's picks with a track record of how they did, and a slip with a suggested parlay. The numbers are estimates, **not advice, and the page is for entertainment only**. It is off until you turn it on in the gear of the Tools window.

To price the picks with the real odds of a betting site and to reserve a booking code (nothing is bet), set **Settings > Services > Score > Odds provider** to the Altenar integration name of that site (many betting sites run on the Altenar engine). Without it the picks use the model's own prices, marked with `*`. The slip lives only while the Tools window is open.

---

## Languages

Settings > General > Language picks the language of the shell, the Settings window and the lock screen (default: the system language).

Texts live in `assets/i18n/<lang>.json`, one flat file per language: `"English text": "translated text"`. Every file exists as a template (a value equal to the English text means "not translated yet" and shows English). To translate, edit the value; keep the key, the spaces at the end of a text and the `·` / `•` marks. Your own fixes go in `~/.local/share/end4pC/i18n/<lang>.json` and win over the project's file.

After adding texts to the code run `python3 tools/i18n-extract.py --write`; it keeps what is already translated.

Available: `ar be ca cs de en es fr gl-ES he hu it ja ko nl nn pl pt-BR ru sv tr uk-UA vi zh-Hans zh-Hant`.

---

## Troubleshooting

| Symptom | What to check |
|---|---|
| Nothing appears | Run `~/.config/end4pC/build/end4pC` in a terminal and read the output. Make sure no other bar or notification daemon is running |
| `git clone` says the folder exists | The shell is already there: use `end4pC update`, or `rm -rf ~/.config/end4pC` first (your settings are in `~/.local/share/end4pC`, not in that folder) |
| No blur behind the bar | Check that the layer rules from `hyprland/end4pC.lua` loaded: `hyprctl layers`, then `hyprctl reload` |
| A button does nothing | The tool it calls is probably not installed (see [Requirements](#to-run)) |
| Binds do not work | Another bind may use the same keys; Hyprland runs every bind on a key. Check Tools > Keybinds |
| The commands print nothing | The shell is not running: `end4pC ping` should answer `pong` |

---

## Credits

- Inspired by [end-4's dots-hyprland](https://github.com/end-4/dots-hyprland) (illogical-impulse) and its Material look.
- Palettes from the wallpaper: [matugen](https://github.com/InioX/matugen).
- Icons: [Solar Icon Set](https://github.com/480-Design/Solar-Icon-Set) (CC BY 4.0). Glyphs: Material Symbols. Shapes: Material 3 expressive shapes.
- Dotfiles of the installer: based on end-4's dots-hyprland (GPL-3.0). More in [THIRD_PARTY.md](THIRD_PARTY.md).

## License

end4pC is free software under the [GNU General Public License v3.0](LICENSE). Copyright (C) 2026 pctrade.

The icons, fonts and configs it includes keep their own licenses: see [THIRD_PARTY.md](THIRD_PARTY.md).
