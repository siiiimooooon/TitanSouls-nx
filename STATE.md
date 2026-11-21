# STATE — TitanSouls-nx

Updated 2026-11-20. **Playable on Switch Lite**: graphics, sound, controls, OBB, saves; 60 fps while playing (the only drops are loads).

## Base
- Titan Souls Android v1.03 (`com.devolver.titansouls`, versionCode 31), via so-loader. Analysis and file list: `docs/plan_titan_souls.md`.
- APK + OBB `main.31` in `game/` (ignored). armeabi-v7a only.
- Runtime `port/runtime` = aks796/android32 @ 4b5e470 (submodule, `core.autocrlf=false` so the `.sh` files keep LF). DuckTales-NX template. Imports 261/261.
- Build: `tools/build_port.bat` → `out/titansouls_nx.nro` (0 warnings). WSL2 `Ubuntu-24.04` + Docker, user `nx`; 32-bit deps in `/srv/nx/mc3_nx/`; build in `/srv/nx/ts_nx`. From Git Bash: `MSYS_NO_PATHCONV=1` before `wsl`.
- Upload: `python -I tools/switch_ftp.py push out/sd titansouls_nx.nro` (copy the NRO to `out/sd/` first). Screenshots: `tools/switch_album.py`.

## Done
- Boot: kuser helpers in `.data` (`ts_loader.c`), fmodex imports, TVActivity JNI (`ts_java.c`), our own regex, no per-thread OBB layer.
- Loads: long sounds as streams (`ts_fmod.c`): 6.4 → 4.9 s. Cache of decoded effects (`[audio] sound_cache_mb`, 96 MB): retrying the big area 5.5 → 0.95 s, the 57-sound area 2.0 → 0.47 s.
- Switch icons with crossed letters A↔B and X↔Y (`ts_glyphs.c`: the game takes BUTTON_Y as shoot and BUTTON_B as roll). Controls screen with a Switch (`ts_switchpad.c`). D-pad as a hat axis: navigates menus without moving the character.
- Achievements (`ts_achievements.c`, `ts_menu.c`, `ts_overlay.c`): 27, detected through `printf("UNLOCK: %s")`, saved in `achievements.txt` (the old `logros.txt` is read if it is the only one). ACHIEVEMENTS option on the title menu (text translated as gametext.xml is read). List and banner in Andrios at 2× (the game's FreeType), ornaments from `title.png` and the B icon; language from `printf("LANGUAGE: %s")`. Holding − opens it anywhere.
- `ts_setup_screen.c`: install screen without text. Note: an update is drawn by the build that was already installed.

## Release
- v1.0.0 (build 202611190127) published. v1.0.1: the repository in English and `achievements.txt` (migrates `logros.txt`); notes in `out/release/release_notes_1.0.1.md`. Installation guide in the README; MIT license at the root.
- Launcher icon: art based on the game's (`port/launcher/icon.jpg`), a deliberate decision.

## Next
1. A ~1 s stall after some loads (one thread alone; not the sound): look at it with the sampler.
2. Achievements in the pause menu (GameMenu hard-codes its selection to 0..2: risky).
3. Hints on how to unlock each achievement; statistics (deaths, time) if they are in the save.
4. A full playthrough (HOME/sleep, ending) and testing docked.
