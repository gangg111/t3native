### t3native

A native Android (C++ NDK) port of [n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake),
translated file for file from its GDScript `remake/core` and proven bit-exact by the same golden
traces the remake is tested with (the original game's code run in a MIPS + GTE CPU harness).

Derived from Tekken3Remake, licensed under the PolyForm Noncommercial License 1.0.0
(see `LICENSE.md`, copied from the source project). Noncommercial use only. Tekken is a trademark
of Bandai Namco Entertainment; this project is not affiliated with it.

No game data is included. Assets come from your own disc through the remake's converter
(`tools/remake_import/convert.py`) into `remake/imported/`; traces from
`tools/research/export_traces.py` and `flow_trace.py` into `work/traces/`.

### How to build the APK from your own discs

Po polsku: [docs/PORADNIK.md](docs/PORADNIK.md).

![Tekken 3 Builder](docs/builder.png)

You need a Windows PC, your own Tekken 3 disc and an Android phone (Android 8.0 or newer, 64-bit).
The builder holds no game data and downloads nothing: the APK is made from your dumps on your PC.

**1. Dump your disc.** The Japanese PlayStation disc is required; the USA disc is optional and
adds the English texts and move lists.

| Disc | Redump name | Serial | |
|---|---|---|---|
| Japanese | Tekken 3 (Japan) (Rev 1) | SLPS-01300 | recommended |
| Japanese | Tekken 3 (Japan) | SLPS-01300 | first release, also works |
| USA | Tekken 3 (USA) | SLUS-00402 | optional |

Other releases (for example the European PAL disc) are not read.

- Dump the disc as **BIN/CUE** (raw 2352-byte sectors), with any CD drive and a ripping program,
  for example ImgBurn (mode "Create image file from disc") or redumper. Keep the `.cue` and its
  `.bin` file(s) together in one folder per disc.
- Do **not** dump to `.iso`: an ISO keeps only the plain data sectors, so the music and the movies
  (XA sectors) are lost.
- A `.chd` image is not read directly: extract it first with MAME's chdman:
  `chdman extractcd -i "Tekken 3.chd" -o "Tekken 3.cue"`. ECM-compressed images (`.bin.ecm`) are read as they are.

**2. Optional: the arcade set.** MAME's ROM set `tekken3` (Tekken 3, World ver. E1, 1997) as
`tekken3.zip`, dumped from your own arcade board, adds the arcade's stages and character models
(VERSION: ARCADE in the game's options). Older MAME sets whose program chips are named
`tet1vere.2e` / `tet1vere.2j` are accepted too.

**3. Build.** Download `Tekken3Builder.exe` from the
[latest release](https://github.com/gangg111/t3native/releases/latest) and run it (one file,
nothing to install). Choose the folder of the Japanese disc, optionally the USA disc folder and
`tekken3.zip`, and the folder for the APK. The window checks every choice and names the release it
found, or says what is wrong. Click BUILD APK: the first build takes a few minutes and writes
`Tekken3.apk` (about 500 MB). EN / PL in the corner switches the language.

**4. Install.** Copy `Tekken3.apk` to the phone and open it; Android asks to allow installing apps
from that app (file manager, browser): allow it once. A Bluetooth or USB pad works, and so do the
touch controls on the screen.

**Updates and the signing key.** The first build makes a signing key in the `key` folder next to
the .exe. Keep it: every later build is signed with the same key, so a new APK installs over the
old one and the saves stay. Without it the phone refuses the update and the game has to be
uninstalled (the saves are lost).

**Keep the APK to yourself.** It holds data from your disc: for your own private use only.

| Problem | Fix |
|---|---|
| "not a supported Tekken 3 disc" | another release or a damaged dump: dump again, check the table above |
| "the USA release: put it in the USA field" | the USA disc was chosen in the Japanese field |
| no music or movies in the game | the disc was dumped as `.iso`: dump it again as BIN/CUE |
| "CHD images are not supported" | extract with `chdman extractcd` (step 1) |
| the phone refuses the update | the APK was built with another key: use the same `key` folder, or uninstall first |
| the build fails | the reason is at the end of the window's log |

The builder's sources: `builder/`, `tools/make_builder.py`; its own readme: `builder/README.txt`.
NETWORK MODE in the APK finds games through the router and over Wi-Fi Direct (no router) at once.

### Layout

| Path | Source |
|---|---|
| `src/gd/` | GDScript value model (int64, shared packed arrays, Array, Dictionary), JSON, gzip |
| `src/core/` | `remake/core/` |
| `src/content/` | `remake/content/` |
| `src/platform/` | `remake/platform/` |
| `tests/` | `remake/tests/` (trace suites) |

### Build and test

Host (MinGW g++ 15, CMake, Ninja):

```bash
cmake -S . -B build/host -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build/host
build/host/t3tests --imported <remake/imported> --traces <work/traces> [filter]
```

Android arm64-v8a (NDK r26):

```bash
cmake -S . -B build/arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
cmake --build build/arm64
```
