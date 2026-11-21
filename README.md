# TitanSouls-nx

An unofficial port of **Titan Souls** to the **Nintendo Switch**, based on the Android version. It runs natively on the console (it is not an emulator) and launches from its own icon on the HOME menu.

- 60 fps at stock clocks, 720p in handheld mode.
- Switch buttons on screen and a controls screen showing a Switch.
- Achievements: they are saved, a banner shows when you earn one, and you can browse them from the title menu, in all 6 of the game's languages.
- Shorter loading times than on Android, especially when retrying a titan.

> **This repository does not include the game.** You need your own copy of Titan Souls for Android.

## What you need

**On the console**
- A Switch running custom firmware (**Atmosphère**).
- **[Sphaira](https://github.com/ITotalJustice/sphaira)** as the homebrew menu: it creates the game's icon on the HOME menu.

**From the game: your copy of Titan Souls for Android, version 1.03**
| File | Details |
|---|---|
| The APK | Package `com.devolver.titansouls`, version **1.03** (versionCode 31). The file name does not matter. |
| The OBB | `main.31.com.devolver.titansouls.obb` (178 MB), with exactly that name. |

Other versions do not work: the port is made for these binaries. To check that you have the right files, their SHA-256 hashes are:
```
APK  1d748a7dbe1e97cd8f6fd55dd4b750a19671ec5542ca363f348dd5c8cfa7cde5
OBB  8ae39ba0b6b7c45319058b89be30d2a2898f08d3b41f9f180fe4ed86d98d2c77
```

## Installation

1. Download `titansouls_nx.nro` from the latest version in [Releases](../../releases).
2. On the SD card, create the folder `switch/titansouls_nx/` and lay it out like this:
   ```
   sdmc:/switch/titansouls_nx/
   ├── titansouls_nx.nro
   ├── titansouls.apk                         (your APK, any name ending in .apk)
   └── obb/
       └── main.31.com.devolver.titansouls.obb
   ```
3. Put the SD card back in the Switch and open **Sphaira**.
4. Go to **Homebrew**, highlight **Titan Souls** and choose **Install Forwarder**. A new Titan Souls icon appears on the HOME menu.
5. Go back to the HOME menu and open that icon. The first time takes a few seconds (it installs the program and unpacks the libraries from the APK, with a progress bar). After that it starts right away.

From then on, always play from that icon. If you open the NRO from Sphaira's homebrew list instead, it only shows instructions.

### Updating
Replace `titansouls_nx.nro` with the new one and open the icon: the port updates itself. If the new version has a new icon, repeat step 4 to see it (if the HOME menu still shows the old one, delete the icon in System Settings → Data Management and install the forwarder again; your saves are on the SD card and are not touched).

## Controls

| Button | Action |
|---|---|
| Left stick | Move |
| **Y** | Shoot the arrow (hold to aim, release to fire; hold again to call it back) |
| **B** | Roll (hold to run) |
| **+** | Pause |
| D-pad | Navigate menus |
| **B** / **A** | Confirm / back, in menus |
| Hold **−** | Achievements list (from anywhere) |

The game shows Switch buttons on screen. If you prefer the original ones, change it in `config.ini`.

## Options (`config.ini`)

It is created in `sdmc:/switch/titansouls_nx/` the first time you open the game. Edit it with any text editor; changes apply from the next launch.

| Section / option | Default | What it does |
|---|---|---|
| `[game] language` | `auto` | Language: `auto` (the console's) or `en`, `fr`, `de`, `it`, `es`, `pt`. |
| `[controls] swap_a_b` | `false` | Swaps A and B. |
| `[controls] button_icons` | `switch` | On-screen buttons: `switch` or `original`. |
| `[display] resolution` | `auto` | `720`, `1080` or `auto` (1080 when docked). |
| `[audio] stream_long_sounds` | `true` | Music and long sounds are not decoded while loading (shorter loads). |
| `[audio] sound_cache_mb` | `96` | Memory used to keep sound effects between loads (faster retries). `0` turns it off. |
| `[performance] boost_cpu_when_loading` | `false` | Raises the CPU clock while loading. Off: everything at stock clocks. |

## Achievements

All 27 of the game's achievements. They are saved in `achievements.txt`, in the game folder, with the date each one was earned. Browse them from **ACHIEVEMENTS** on the title menu or by holding **−** at any time.

## Troubleshooting

- **The NRO says the APK or the OBB is missing:** check the names and the folder from step 2, and that the version is 1.03 (the SHA-256 hashes above).
- **Black screen or it closes:** the log is written to `sdmc:/switch/titansouls_nx/debug.log`. Open an issue and attach it.
- **Saves:** they are stored in `sdmc:/switch/titansouls_nx/TitanSoulsSave/`. To back them up, copy that folder.

Tested on a Switch Lite. Docked mode should work but has not been tested.

## How it works

It does not emulate Android or recompile the game. A *so-loader* loads the original ARM binary (`libTestSuite.so`) and its audio library (FMOD Ex) inside a Switch program, and implements on top of the console what the game asks Android for: libc, graphics (EGL/GLES2 through Mesa), controls, files and audio. Details in [`docs/plan_titan_souls.md`](docs/plan_titan_souls.md).

### Building
You need WSL2 with Docker. `tools/build_port.bat` builds everything and leaves `out/titansouls_nx.nro` (see [`docs/tools.md`](docs/tools.md)).

## Credits

- The [android32](https://github.com/aks796/android32) runtime (aks796) and the DuckTales-NX template (Thorhax), built on the ideas of [max_nx](https://github.com/fgsfdsfgs/max_nx) (fgsfds and Andy Nguyen).
- libnx, devkitPro, Mesa and Atmosphère.

License of the port's code: MIT ([LICENSE](LICENSE)).

Titan Souls is © Acid Nerve / Devolver Digital. This is a fan project with no affiliation with them; it does not include or distribute anything from the game.
