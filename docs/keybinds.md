[← end4pC docs](README.md)

# Keybinds

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
