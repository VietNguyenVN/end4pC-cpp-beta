[← end4pC docs](README.md)

# T3xt: the text editor

The text editor, a program of its own (`end4pC-t3xt`, or `end4pC t3xt [file]`): tabs, a sidebar with the open files and the folder (toggle with the button or Ctrl+B), line numbers, find and replace, undo and redo, auto-indent, comment toggle, and syntax highlighting for C, C++, Java, JavaScript, TypeScript, Go, Rust, C#, Kotlin, Swift, Dart, QML, PHP, GLSL, KDL, Python, shell, Lua, JSON, HTML / XML, CSS / SCSS, Markdown, YAML, INI / TOML / conf, SQL, Makefile, CMake, Dockerfile and diffs. The code is coloured with the palette of the shell: the primary colour is the keywords and the other kinds are the same colour turned round the hue circle, so it follows the wallpaper and the theme. No terminal, no plugins.

## Launching it

- **Application menu / launcher:** look for **T3xt**; `end4pC update` and the installer add its entry (it needs no new libraries). From a terminal or a key bind: `end4pC t3xt`, `end4pC t3xt ~/notes.md`; in Tools > Keybinds it is a Shell action. To open text files with it by default: `xdg-mime default end4pC-t3xt.desktop text/plain` (optional).
- **Keys:** Ctrl+S save (Ctrl+Shift+S save as), Ctrl+O open, Ctrl+N new, Ctrl+W close the tab, Ctrl+Tab next tab, Ctrl+B sidebar, Ctrl+F find (F3 / Shift+F3 next / previous), Ctrl+H replace, Ctrl+Z / Ctrl+Y undo / redo, Ctrl+/ comment, Ctrl+D duplicate the line, Alt+Up / Alt+Down move the line, Tab / Shift+Tab indent (a selection of lines too), Ctrl++ / Ctrl+- / Ctrl+0 and Ctrl+wheel for the size of the text, Shift+wheel sideways. Triple click selects a line, double click a word; click the numbers to select lines.
- **Session:** like Geany, when you close T3xt the files that were open are remembered (each at the line where the cursor was, and the tab that was in front) and open again the next time. Unnamed documents are not kept. T3xt is one window: launching it again (from the launcher, `end4pC t3xt file`, or a file manager) hands the files to the window that is already open. To turn the session off put `restore=0` in `~/.local/share/end4pC/t3xt.txt`.
- **Files:** UTF-8 only (a file that is not valid UTF-8 opens read-only, so saving can never damage it); the line endings (LF / CRLF) are kept; saving writes a copy beside the file and renames it, so a failure never leaves half a file. Open and Save As use `zenity`; copy and paste use `wl-clipboard` (`wl-copy`, `wl-paste`).
- Accents and dead keys work (they go through the compose table of your locale). There is no input method support yet (Fcitx / IBus).
