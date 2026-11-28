[← end4pC docs](README.md)

# Play3r: the music player

The music player, a program of its own (`end4pC-play3r`, or `end4pC play3r [song or folder]`). ffmpeg decodes, PipeWire plays, TagLib reads the tags and the cover; no toolkit. A folder with thousands of songs is in the queue at once (the tags are read in the background, from the rows on screen onwards). Cover on a Material shape, a wavy progress bar, synced lyrics (the same sources as the player of the bar), search, saved playlists, the queue of the last time, and MPRIS, so the bar, the media keys and `playerctl` see and control it.

## Launching it

- **Application menu / launcher:** look for **Play3r**. A fresh install, and `end4pC update`, add its entry (`~/.local/share/applications/end4pC-play3r.desktop`) by themselves when the player was built.
- **Terminal or key bind:** `end4pC play3r` (toggle), `end4pC play3r ~/Music/album` (play a song or folder).
- **Already had the dots before Play3r?** Install the three libraries once (`sudo pacman -S ffmpeg taglib pipewire`), then `end4pC update`. To only add the menu entry, or if you build by hand: `ninja -C build && scripts/install-desktop-entries.sh`.
- Play3r opens as a normal window (tiled, or floating if you float it); it has no window rule of its own.
- The bar shows the song and controls it through MPRIS; if the bar is set to follow another player (the `preferredPlayer` setting), change it to `play3r` to see Play3r there.
- Keys: type to search the queue, Up/Down/Enter to pick, Esc to clear the search, Ctrl+Q or Ctrl+W to close.
