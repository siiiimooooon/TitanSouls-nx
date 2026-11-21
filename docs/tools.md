# Tools (`tools/`)

| File | What it does |
|---|---|
| `build_port.bat` / `build_port.sh` | Builds the port in WSL (copies `port/` to `/srv/nx/ts_nx`, uses the 32-bit libnx and Mesa) and leaves `out/titansouls_nx.nro` + `out/build_last.log`. |
| `install_wsl.bat` | Installs WSL2 (once, needs administrator rights). |
| `switch_ftp.py` + `switch_ftp.json` | Client for Sphaira's FTP server (push/pull/rm/ping, atomic uploads checked with SHA-256). The .json holds the console's IP (not in git). Folder: `/switch/titansouls_nx` (another one with the `TS_FTP_SUB` variable). |
| `switch_album.py` | Downloads the console's screenshots over FTP (`album_sd:`). |
| `apk_info.py` | APK facts without Android tools: package, versionCode, activity, dex classes and the in-memory size of each `.so`. |
| `obb_tool.py` | Lists and extracts files from the OBB (the game's own format). Extracted files go to `game/` (ignored). |
| `disasm_ts.py` | Annotated disassembly of `libTestSuite.so` (with strings), to find the game's functions. |
| `symbolize_ts.py` | Adds function names to the sampler's `[prof]` lines in `debug.log`. |
| `glyphs_proto.py`, `switchpad_proto.py` | Previews of the button icons and of the controls screen. |
| `overlay_proto*.py` | Previews of the achievements list (the latest, `overlay_proto3.py`, with the menus' typeface). |
