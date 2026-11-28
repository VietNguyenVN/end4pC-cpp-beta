[← end4pC docs](README.md)

# Configuration

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
