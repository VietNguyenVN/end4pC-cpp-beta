# Third-party material

end4pC is released under the GNU General Public License v3.0 (see [LICENSE](LICENSE)). It includes or builds on the following; each part
keeps its own license.

| What | Where | License |
|---|---|---|
| [Solar Icon Set](https://github.com/480-Design/Solar-Icon-Set) by 480 Design (the SVG icons) | `assets/icons/` | CC BY 4.0 |
| [Material Symbols](https://github.com/google/material-design-icons) by Google (a subset of the glyphs, as two static fonts) | `assets/fonts/` (license text next to them) | Apache-2.0 |
| [Google Sans Flex](https://fonts.google.com/specimen/Google+Sans+Flex) by Google (the font the installer sets up) | `installer/fonts/` (its license is in the folder) | SIL Open Font License 1.1 |
| [end-4's dots-hyprland](https://github.com/end-4/dots-hyprland) (illogical-impulse): the Hyprland, kitty, fish, btop, geany, GTK and KDE configs the installer copies are based on it | `installer/dots/`, `hyprland/` | GPL-3.0 |
| The Bibata cursor recipe, the same one dots-hyprland uses | `installer/packages/local/` | GPL-3.0 |
| [matugen](https://github.com/InioX/matugen): makes the palettes from the wallpaper. It is installed as a package, not included | (not bundled) | GPL-2.0 |

The shell itself talks to the compositor and the desktop only through open protocols (wlr-layer-shell, ext-session-lock, ext-background-effect,
cursor-shape, D-Bus: notifications, StatusNotifierItem, MPRIS, UPower, polkit) and links against cairo, pango, librsvg, gdk-pixbuf, sd-bus,
xkbcommon and PAM.
