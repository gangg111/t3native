### t3native

A native Android (C++ NDK) port of [n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake),
translated file for file from its GDScript `remake/core` and proven bit-exact by the same golden
traces the remake is tested with (the original game's code run in a MIPS + GTE CPU harness).

[![I Ported Tekken 3 to Android: No Emulator, 120 FPS](https://img.youtube.com/vi/MYBpfyi-2vo/maxresdefault.jpg)](https://www.youtube.com/watch?v=MYBpfyi-2vo)

Derived from Tekken3Remake, licensed under the PolyForm Noncommercial License 1.0.0
(see `LICENSE.md`, copied from the source project). Noncommercial use only. Tekken is a trademark
of Bandai Namco Entertainment; this project is not affiliated with it.

No game data is included. Assets come from your own disc through the remake's converter
(`tools/remake_import/convert.py`) into `remake/imported/`; traces from
`tools/research/export_traces.py` and `flow_trace.py` into `work/traces/`.

### How to build the APK from your own discs

Po polsku: [docs/PORADNIK.md](docs/PORADNIK.md).

![Tekken 3 Builder](docs/builder.png)

1. **Dump the PlayStation disc** as BIN/CUE (not ISO, it loses the music and movies), for
   example with ImgBurn: "Create image file from disc". Required: Tekken 3 (Japan), SLPS-01300.
   Optional: Tekken 3 (USA), SLUS-00402, for the English texts.
2. **Dump the arcade.** Read the ROM chips of your Tekken 3 arcade board (Namco System 12)
   with an EPROM programmer and save them as MAME's set
   `tekken3.zip`. It adds the arcade's stages and character models.
3. **Build.** Run `Tekken3Builder.exe` from the
   [latest release](https://github.com/gangg111/t3native/releases/latest), choose the dumps and
   click BUILD APK.
4. **Install** `Tekken3.apk` on the phone (Android 8.0 or newer). Keep the `key` folder next to
   the builder, so the next APK installs as an update.

The APK holds data from your dumps: for your own private use only.

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
