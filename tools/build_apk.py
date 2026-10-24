"""Packs the Android build into an APK without Gradle.

aapt2 links the manifest and the resources into a base APK; this script then adds the activity's
Java class (android/java, compiled with javac and dexed with d8), the native libraries (stripped,
stored: the manifest has extractNativeLibs=false, so the loader maps them in place) and the game
data as assets, and zipalign and apksigner finish it with the debug key.

Input: build/android-<abi>/libt3game.so from the CMake builds and the remake's converted data
(remake/imported, remake/content/texts_en.json). The data stays private to the APK: never publish
the result.

    py tools/build_apk.py [--abi arm64-v8a --abi x86_64] [--no-movies] [--out build/t3native.apk]
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "AppData/Local/Android/Sdk"))
BUILD_TOOLS = SDK / "build-tools/36.0.0"
PLATFORM_JAR = SDK / "platforms/android-35/android.jar"
NDK = SDK / "ndk/26.1.10909125"
STRIP = NDK / "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
REMAKE = Path(os.environ.get("T3_REMAKE", "D:/Tekken 3/Tekken3Remake/remake"))
KEYSTORE = Path.home() / ".android/debug.keystore"
# Android Studio's JetBrains Runtime (JDK 21): the system Java is 8.
JDK = Path(os.environ.get("T3_JDK", "C:/Program Files/Android/Android Studio/jbr"))
MIN_API = 26

# Already compressed: stored as they are (deflate would only cost time at start).
STORED_SUFFIXES = {".png", ".ogg", ".mpg", ".so"}
# Godot's import bookkeeping, not game data; the Theora movies, which the native player does not
# read (it plays MPEG-1 encodes of the disc's movies, tools/movies_from_disc.py).
SKIPPED_SUFFIXES = {".import", ".ogv"}
MOVIE_SUFFIXES = {".mpg"}
# The remake's Android export keeps the small movies (export_presets.cfg excludes *.large.ogv).
SKIPPED_MOVIE_VARIANT = ".large.mpg"


def asset_entries(imported: Path, content_texts: Path, movies: bool, music: bool = True) -> list[tuple[str, Path]]:
    """(name in the APK, file) for every asset: imported/** and content/texts_en.json."""
    entries = []
    for path in sorted(imported.rglob("*")):
        if not path.is_file() or path.suffix in SKIPPED_SUFFIXES or path.name.startswith("."):
            continue
        if not music and path.relative_to(imported).parts[0] == "music":
            continue
        if path.suffix in MOVIE_SUFFIXES and (not movies or path.name.endswith(SKIPPED_MOVIE_VARIANT)):
            continue
        entries.append(("assets/imported/" + path.relative_to(imported).as_posix(), path))
    entries.append(("assets/content/texts_en.json", content_texts))
    return entries


def compression(name: str) -> int:
    return zipfile.ZIP_STORED if Path(name).suffix in STORED_SUFFIXES else zipfile.ZIP_DEFLATED


def java_sources(java_dir: Path) -> list[Path]:
    """The activity's Java sources, in a stable order."""
    return sorted(java_dir.rglob("*.java"))


def dex(java_dir: Path, tmp: Path) -> Path:
    """classes.dex of the Java sources (javac for Java 11 bytecode, then d8 for MIN_API)."""
    classes = tmp / "classes"
    classes.mkdir()
    run(JDK / "bin/javac.exe", "--release", "11", "-classpath", PLATFORM_JAR, "-d", classes, *java_sources(java_dir))
    out = tmp / "dex"
    out.mkdir()
    # d8.bat runs the `java` of JAVA_HOME (the system one is 8, too old for d8).
    env = dict(os.environ, JAVA_HOME=str(JDK), PATH=str(JDK / "bin") + os.pathsep + os.environ.get("PATH", ""))
    run(BUILD_TOOLS / "d8.bat", "--release", "--min-api", MIN_API, "--lib", PLATFORM_JAR, "--output", out,
        *sorted(classes.rglob("*.class")), env=env)
    return out / "classes.dex"


def run(*cmd, env: dict | None = None) -> None:
    print(">", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, env=env)


def base_apk(tmp: Path, res_dir: Path = ROOT / "android/res", tree: Path = ROOT, label: str | None = None) -> Path:
    """The manifest (of the checkout `tree`) and the resources linked by aapt2 into `tmp/base.apk`;
    `label`, when given, in place of every android:label of the manifest (the app's name)."""
    res = tmp / "res.zip"
    run(BUILD_TOOLS / "aapt2.exe", "compile", "-o", res, "--dir", res_dir)
    manifest = tree / "android/AndroidManifest.xml"
    if label is not None:
        text = re.sub(r'android:label="[^"]*"', f'android:label="{label}"', manifest.read_text(encoding="utf-8"))
        manifest = tmp / "AndroidManifest.xml"
        manifest.write_text(text, encoding="utf-8")
    base = tmp / "base.apk"
    run(BUILD_TOOLS / "aapt2.exe", "link", "-o", base, "-I", PLATFORM_JAR, "--manifest", manifest, res)
    return base


def stripped(abi: str, tmp: Path, tree: Path = ROOT) -> Path:
    """The CMake build's libt3game.so of `abi` (in the checkout `tree`) without its symbols."""
    out = tmp / f"libt3game-{abi}.so"
    run(STRIP, "--strip-unneeded", "-o", out, tree / f"build/android-{abi}/libt3game.so")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--abi", action="append", help="arm64-v8a, x86_64 (default: both)")
    ap.add_argument("--no-movies", action="store_true", help="leave the movies out (a smaller test APK)")
    ap.add_argument("--no-music", action="store_true", help="leave the music out (a smaller test APK)")
    ap.add_argument("--out", type=Path, default=ROOT / "build/t3native.apk")
    args = ap.parse_args()
    abis = args.abi or ["arm64-v8a", "x86_64"]

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        base = base_apk(tmp)
        classes_dex = dex(ROOT / "android/java", tmp)
        unaligned = tmp / "unaligned.apk"
        shutil.copy(base, unaligned)
        with zipfile.ZipFile(unaligned, "a") as z:
            z.write(classes_dex, "classes.dex", zipfile.ZIP_DEFLATED)
            for abi in abis:
                z.write(stripped(abi, tmp), f"lib/{abi}/libt3game.so", zipfile.ZIP_STORED)
            entries = asset_entries(REMAKE / "imported", REMAKE / "content/texts_en.json", not args.no_movies, not args.no_music)
            for name, path in entries:
                z.write(path, name, compression(name))
            print(f"{len(entries)} assets")

        aligned = tmp / "aligned.apk"
        # -P 16: 16 KB page alignment of the stored .so (Android 15+ devices with 16 KB pages).
        run(BUILD_TOOLS / "zipalign.exe", "-P", "16", "-f", "4", unaligned, aligned)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        run(BUILD_TOOLS / "apksigner.bat", "sign", "--ks", KEYSTORE, "--ks-pass", "pass:android",
            "--out", args.out, aligned)
        run(BUILD_TOOLS / "apksigner.bat", "verify", args.out)
    print(f"{args.out} {args.out.stat().st_size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
