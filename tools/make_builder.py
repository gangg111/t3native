"""Lays out the self-contained builder folder (build/builder/Tekken3Builder): everything a user needs
to make the APK from their own discs, with nothing to install.

    Tekken3Builder/
      Kompilator Tekken 3.exe       the launcher (builder/launcher.c): runs the window below
      app/                          t3builder (window, pipeline, signing) and kompilator.pyw
      runtime/python/               a CPython 3.12 copy with numpy, Pillow and xxhash
      runtime/ffmpeg/               ffmpeg.exe, ffprobe.exe
      remake/                       the remake's converter (tools/remake_import, tools/research),
                                    remake/content/texts_en.json and its licence
      t3native/tools/               build_apk, movies_from_disc, font_sheets_from_disc, arcade_overlay
      template/                     base.apk (manifest, resources, classes.dex, a placeholder icon)
                                    and lib/<abi>/libt3game.so, stripped

No game data goes in: the template's icon is a placeholder (the builder draws the real one from
the user's disc), and the folder is checked for the converter's output marker before it is done.
The key (klucz/) and the conversions (praca/) appear on the user's first build.

    py tools/make_builder.py [--out build/builder/Tekken3Builder] [--template-only]
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import build_apk

ROOT = build_apk.ROOT
REMAKE = build_apk.REMAKE.parent                    # the remake's checkout (REMAKE is its remake/)
OUT = ROOT / "build/builder/Tekken3Builder"
TEMPLATE = ROOT / "build/builder/template"
PYTHON = Path(sys.base_prefix)                      # the CPython the venv was made from
SITE = Path(sys.prefix) / "Lib/site-packages"       # the venv's packages
PACKAGES = ("numpy", "numpy.libs", "PIL", "xxhash")
PYTHON_FILES = ("python.exe", "pythonw.exe", "python3.dll", f"python{sys.version_info[0]}{sys.version_info[1]}.dll",
                "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt")
LIB_SKIPPED = {"test", "idlelib", "site-packages", "ensurepip", "lib2to3", "turtledemo", "__pycache__"}
T3_TOOLS = ("build_apk.py", "movies_from_disc.py", "font_sheets_from_disc.py", "arcade_overlay.py")
REMAKE_DIRS = ("tools/remake_import", "tools/research")
SKIPPED = shutil.ignore_patterns("__pycache__", ".pytest_cache", "tests", "*.pyc")
LAUNCHER = "Kompilator Tekken 3.exe"
# builder/ffmpeg_min.sh's install folder (MSYS2 home): FFmpeg 8.0.1, LGPL, shared.
FFMPEG = Path(os.environ.get("T3_FFMPEG_MIN", f"C:/msys64/home/{Path.home().name}/t3ff/out/bin"))
GPP = Path("C:/mingw64/bin/g++.exe")
WINDRES = Path("C:/mingw64/bin/windres.exe")
# Files whose presence means game data got in: the converter's marker and its outputs.
FORBIDDEN = (".converter", "manifest.json", "texture_index.json")


def ffmpeg_folder() -> Path:
    """The minimal LGPL FFmpeg built by builder/ffmpeg_min.sh (programs, DLLs, licences)."""
    if not (FFMPEG / "ffmpeg.exe").is_file():
        raise SystemExit(f"no {FFMPEG / 'ffmpeg.exe'}: build it with builder/ffmpeg_min.sh in MSYS2")
    return FFMPEG


def placeholder_icon(path: Path) -> None:
    """A plain icon for the template (192 px): the builder replaces it with one from the disc."""
    from PIL import Image  # noqa: PLC0415
    Image.new("RGB", (192, 192), (24, 24, 32)).save(path)


def template(out: Path) -> None:
    """base.apk with a placeholder icon and classes.dex, and the stripped libraries."""
    if out.exists():
        shutil.rmtree(out)
    (out / "lib").mkdir(parents=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        res = tmp / "res"
        shutil.copytree(ROOT / "android/res", res)
        for icon in res.glob("mipmap-*/ic_launcher.png"):
            placeholder_icon(icon)
        base = build_apk.base_apk(tmp, res)
        dex = build_apk.dex(ROOT / "android/java", tmp)
        with zipfile.ZipFile(base, "a") as z:
            z.write(dex, "classes.dex", zipfile.ZIP_DEFLATED)
        shutil.copy(base, out / "base.apk")
        for abi in build_apk.ABIS if hasattr(build_apk, "ABIS") else ("arm64-v8a", "x86_64"):
            (out / "lib" / abi).mkdir()
            shutil.copy(build_apk.stripped(abi, tmp), out / "lib" / abi / "libt3game.so")


def python_runtime(out: Path) -> None:
    out.mkdir(parents=True)
    for name in PYTHON_FILES:
        if (PYTHON / name).is_file():
            shutil.copy2(PYTHON / name, out / name)
    shutil.copytree(PYTHON / "DLLs", out / "DLLs", ignore=SKIPPED)
    shutil.copytree(PYTHON / "tcl", out / "tcl", ignore=SKIPPED)
    lib = out / "Lib"
    lib.mkdir()
    for item in (PYTHON / "Lib").iterdir():
        if item.name in LIB_SKIPPED:
            continue
        if item.is_dir():
            shutil.copytree(item, lib / item.name, ignore=SKIPPED)
        else:
            shutil.copy2(item, lib / item.name)
    for package in PACKAGES:
        shutil.copytree(SITE / package, lib / "site-packages" / package, ignore=SKIPPED)


def launcher(out: Path) -> None:
    source = ROOT / "builder/launcher.c"
    subprocess.run([str(GPP), "-O2", "-municode", "-mwindows", "-static", "-x", "c", str(source),
                    "-o", str(out / LAUNCHER)], check=True)


def check_no_game_data(out: Path) -> None:
    found = [p for p in out.rglob("*") if p.name in FORBIDDEN]
    if found:
        raise SystemExit(f"game data in the builder folder: {found[:3]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--template-only", action="store_true")
    args = ap.parse_args()

    template(TEMPLATE)
    if args.template_only:
        return 0
    out = args.out
    keep = {name: out / name for name in ("klucz", "praca") if (out / name).exists()}
    if keep:
        raise SystemExit(f"{out} holds a key or conversions ({', '.join(keep)}): move them away first")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copytree(TEMPLATE, out / "template")
    shutil.copytree(ROOT / "builder/t3builder", out / "app/t3builder", ignore=SKIPPED)
    shutil.copy2(ROOT / "builder/kompilator.pyw", out / "app/kompilator.pyw")
    for name in T3_TOOLS:
        (out / "t3native/tools").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "tools" / name, out / "t3native/tools" / name)
    for rel in REMAKE_DIRS:
        shutil.copytree(REMAKE / rel, out / "remake" / rel, ignore=SKIPPED)
    (out / "remake/remake/content").mkdir(parents=True)
    shutil.copy2(REMAKE / "remake/content/texts_en.json", out / "remake/remake/content/texts_en.json")
    shutil.copy2(REMAKE / "LICENSE.md", out / "remake/LICENSE.md")
    shutil.copy2(ROOT / "LICENSE.md", out / "LICENSE.md")
    shutil.copy2(ROOT / "builder/CZYTAJ.txt", out / "CZYTAJ.txt")
    python_runtime(out / "runtime/python")
    ffmpeg = ffmpeg_folder()
    (out / "runtime/ffmpeg").mkdir(parents=True)
    for path in ffmpeg.iterdir():
        if path.suffix in (".exe", ".dll") and path.name != "ffplay.exe":
            shutil.copy2(path, out / "runtime/ffmpeg" / path.name)
    shutil.copytree(ffmpeg / "licenses", out / "runtime/ffmpeg/licenses")
    launcher(out)
    check_no_game_data(out)
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"{out}: {size / 1e6:.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
