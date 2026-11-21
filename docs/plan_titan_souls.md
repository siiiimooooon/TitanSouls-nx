# Titan Souls (Android) → Switch via so-loader

## The game
- A 2D indie game from 2015 (Acid Nerve / Devolver), on its own C++ engine. Ported to Android by Abstraction Games, package `com.devolver.titansouls`, v1.03 (versionCode 31). It asked for a Shield or a gamepad; the Shield has the same SoC as the Switch (Tegra X1).
- It was never released on Switch. It is no longer sold on Google Play: what counts is the copy of whoever bought it (it stays in their Library). APK mirror sites do not count as your own copy.

## What it contains
- APK (7.2 MB): only `lib/armeabi-v7a/` (32-bit) → the android32 route.
  - `libTestSuite.so` (3.8 MB): the game. NativeActivity (`ANativeActivity_onCreate`, `android_main`), GLES2/EGL, AAsset, AInputQueue/ALooper, pthread, bionic.
  - `libfmodex.so` (0.9 MB): FMOD Ex, the audio. It outputs through OpenSL ES.
  - `libAmazonGamesJni.so` (0.3 MB): Amazon GameCircle. Stubbed.
  - `classes.dex`: Java (TVActivity), answered by a fake JNI.
- OBB `main.31.com.devolver.titansouls.obb` (177.6 MB): **not a zip**, a container of its own (`u32 count`; per file `u32 namelen, name, u64 size, u64 offset`; data after the index). It holds all the game data; the game opens it with `fopen` from `/sdcard/Android/obb/<pkg>/`. `tools/obb_tool.py` lists and extracts it.

## The method
The APK's ARM `.so` files are loaded inside a Switch program, their imports are resolved against our own implementations (bionic, EGL/GLES2 from Mesa, OpenSL, input from libnx) and a few functions are patched. Nothing is recompiled.
- Runtime: [android32](https://github.com/aks796/android32) (submodule `port/runtime`). Template: DuckTales-NX (Thorhax, MIT).
- Format: a 64-bit launcher NRO installs the 32-bit program (NSO + NPDM) as an override for Sphaira's forwarder (`/atmosphere/contents/<tid>/exefs.nsp`). The program updates itself when the NRO carries a newer build.
- Toolchain: WSL2 + Docker (image `ghcr.io/vita2hos/devcontainer/vita2hos`), 32-bit libnx and Mesa. `tools/build_port.bat`.

## The port's files (`port/source/`)
| File | What it does |
|---|---|
| `ts_loader.c` | Loads fmodex → AmazonGamesJni → TestSuite; fixes the pointers to Linux kuser helpers in `.data` |
| `ts_activity.c` | NativeActivity host: callbacks, window, input queue, main loop |
| `ts_java.c` | Fake JNI: TVActivity statics, ClassLoader, OBB and save paths |
| `ts_input.c` | AInputQueue from the controller; D-pad also as a hat axis (menus); holding − opens the achievements |
| `ts_assets.c` | AAsset over the APK with miniz |
| `ts_regex.c`, `ts_libc.c` | Our own ERE engine (the shaders' `#include`s) and a few libc functions |
| `ts_imports_extra.c` | Import overlay: fopen/fread (counters, gametext.xml texts), printf (achievements, language) |
| `ts_fmod.c` | Long sounds as streams; cache of decoded effects between loads |
| `ts_gl.c`, `ts_glyphs.c`, `ts_switchpad.c` | GL wrapper; Switch button icons in the font; controls screen with a Switch |
| `ts_achievements.c`, `ts_menu.c`, `ts_overlay.c` | Achievements: detection, the title menu option, list and banner in the menus' typeface |
| `ts_setup_screen.c` | Install/update screen without text |
| `ts_prof.c`, `ts_sampler.c` | Counters and sampler for long frames (loads) |
| `dcr_config.c` | `config.ini` |

## Legal
The APK, the OBB, the `.so` files and the assets come from each user's own copy. They never go into the repository or a release. The port loads them from the SD card; what it uses from the game (the Andrios typeface, the menu ornaments) is read from the OBB at run time.

## Installation
On the SD card, `sdmc:/switch/titansouls_nx/`: the `.nro`, the APK (any `.apk` name) and `obb/main.31.com.devolver.titansouls.obb`. In Sphaira: Homebrew → Titan Souls → **Install Forwarder**, then open the new icon on the HOME menu. Log in `debug.log`, options in `config.ini`.
