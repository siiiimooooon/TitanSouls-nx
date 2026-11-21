# TitanSouls-nx — Titan Souls (Android) on Nintendo Switch via so-loader

On start: read this file + STATE.md, nothing else. Anything more, through the `lector` subagent.

## Project rules
- Base: the android32 runtime (submodule `port/runtime`) and the DuckTales-NX template. Adapt what exists before writing a loader of our own.
- Measure on hardware as early as possible. Stock clocks: CPU 1020 MHz, GPU 460.8 MHz, never overclock. Handheld 720p. Goal: 60 fps when possible, never below a locked 30.
- Check against a reference that runs well (the Android version on a phone, or the PC version). Criterion: "faithful, not exact".
- Legal: never commit or distribute the APK, the OBB, the `.so` files, assets, dumps or builds containing game data. The port loads the user's own files from the SD card. Whatever the port needs from the game (fonts, ornaments, texts) is read from the OBB at run time. The one deliberate exception: the launcher icon (`port/launcher/icon.jpg`), art based on the game's.
- Everything in the repository is in English (code, comments, docs, tool messages). The game's own translations (the achievements list in its 6 languages) stay as they are.
- Tokens: docs, `ref/` repositories and long logs are read by the `lector` subagent (Haiku). The main agent reads only what it is about to edit.
- C strings with `\n`: edit them with the Edit tool, not with Python in a bash heredoc (it writes real line breaks).

## Layout
- `port/` — the port (code in `port/source/`, runtime in `port/runtime/`, launcher in `port/launcher/`).
- `docs/` — index in `docs/README.md`.
- `tools/` — scripts (build, FTP, OBB, previews); see `docs/tools.md`.
- `ref/` — cloned reference repositories (ignored by git).
- `game/`, `out/`, `logs/` — game data, builds and logs (ignored by git).
