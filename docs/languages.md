[← end4pC docs](README.md)

# Languages

Settings > General > Language picks the language of the shell, the Settings window and the lock screen (default: the system language).

Texts live in `assets/i18n/<lang>.json`, one flat file per language: `"English text": "translated text"`. Every file exists as a template (a value equal to the English text means "not translated yet" and shows English). To translate, edit the value; keep the key, the spaces at the end of a text and the `·` / `•` marks. Your own fixes go in `~/.local/share/end4pC/i18n/<lang>.json` and win over the project's file.

After adding texts to the code run `python3 tools/i18n-extract.py --write`; it keeps what is already translated.

Available: `ar be ca cs de en es fr gl-ES he hu it ja ko nl nn pl pt-BR ru sv tr uk-UA vi zh-Hans zh-Hant`.
