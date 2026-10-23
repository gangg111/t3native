"""Makes the one-file builder (build/builder/Tekken 3 Builder.exe): everything a user needs to
make the APK from their own discs, with nothing to install. First the folder
(build/builder/Tekken3Builder), then the launcher (builder/launcher.cpp) with that folder appended
as an archive, which it unpacks to %LOCALAPPDATA% and runs in a clean environment.

    Tekken3Builder/
      Tekken 3 Builder.exe          the launcher without an archive: runs the folder it lies in
      app/                          t3builder (window, pipeline, signing) and kompilator.pyw
      runtime/python/               a CPython 3.12 copy with numpy, Pillow and xxhash
      runtime/ffmpeg/               ffmpeg.exe, ffprobe.exe
      remake/                       the remake's converter (tools/remake_import, tools/research),
                                    remake/content/texts_en.json and its licence
      t3native/tools/               build_apk, movies_from_disc, font_sheets_from_disc, arcade_overlay
      template/lan/, template/direct/  per APK (local Wi-Fi; Wi-Fi Direct from the wifi-direct
                                    branch's worktree, DIRECT_TREE): base.apk (manifest, resources,
                                    classes.dex, a placeholder icon) and lib/<abi>/libt3game.so, stripped

No game data goes in: the template's icon is a placeholder (the builder draws the real one from
the user's disc), and the folder is checked for the converter's output marker before it is done.
The key (key/, next to the .exe) and the conversions (work/) appear on the user's first build.

    py tools/make_builder.py [--out build/builder/Tekken3Builder] [--template-only]
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

import build_apk

ROOT = build_apk.ROOT
REMAKE = build_apk.REMAKE.parent                    # the remake's checkout (REMAKE is its remake/)
OUT = ROOT / "build/builder/Tekken3Builder"
TEMPLATE = ROOT / "build/builder/template"          # lan/ and direct/: the two APKs' templates
# The wifi-direct branch's worktree (its Android builds make the Wi-Fi Direct APK's template).
DIRECT_TREE = Path(os.environ.get("T3_DIRECT_TREE", ROOT.parent / "t3native-wd"))
PYTHON = Path(sys.base_prefix)                      # the CPython the venv was made from
SITE = Path(sys.prefix) / "Lib/site-packages"       # the venv's packages
PACKAGES = ("numpy", "numpy.libs", "PIL", "xxhash")
PYTHON_FILES = ("python.exe", "pythonw.exe", "python3.dll", f"python{sys.version_info[0]}{sys.version_info[1]}.dll",
                "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt")
LIB_SKIPPED = {"test", "idlelib", "site-packages", "ensurepip", "lib2to3", "turtledemo", "__pycache__"}
T3_TOOLS = ("build_apk.py", "movies_from_disc.py", "font_sheets_from_disc.py", "arcade_overlay.py")
REMAKE_DIRS = ("tools/remake_import", "tools/research")
SKIPPED = shutil.ignore_patterns("__pycache__", ".pytest_cache", "tests", "*.pyc")
LAUNCHER = "Tekken 3 Builder.exe"
ONE_FILE = ROOT / "build/builder" / LAUNCHER       # the one-file builder, next to the folder
# builder/ffmpeg_min.sh's install folder (MSYS2 home): FFmpeg 8.0.1, LGPL, shared.
FFMPEG = Path(os.environ.get("T3_FFMPEG_MIN", f"C:/msys64/home/{Path.home().name}/t3ff/out/bin"))
# MSYS2's MinGW-w64 g++: its static zlib goes into the launcher.
GPP = Path("C:/msys64/mingw64/bin/g++.exe")
PACK_MAGIC = b"T3BPAK01"
PACK_LEVEL = 9
USER_DIRS = {"key", "work", "APK"}
USER_FILES = {"settings.json", "crash.log"}
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


def template(out: Path, tree: Path = ROOT) -> None:
    """base.apk with a placeholder icon and classes.dex, and the stripped libraries, of the checkout
    `tree` (its manifest, Java and CMake builds)."""
    if out.exists():
        shutil.rmtree(out)
    (out / "lib").mkdir(parents=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # The resources of this checkout for every variant (the same files): a worktree lacks the
        # launcher icon, which stays out of git; it is a placeholder here anyway.
        res = tmp / "res"
        shutil.copytree(ROOT / "android/res", res)
        for icon in res.glob("mipmap-*/ic_launcher.png"):
            placeholder_icon(icon)
        base = build_apk.base_apk(tmp, res, tree)
        dex = build_apk.dex(tree / "android/java", tmp)
        with zipfile.ZipFile(base, "a") as z:
            z.write(dex, "classes.dex", zipfile.ZIP_DEFLATED)
        shutil.copy(base, out / "base.apk")
        for abi in ("arm64-v8a", "x86_64"):
            (out / "lib" / abi).mkdir()
            shutil.copy(build_apk.stripped(abi, tmp, tree), out / "lib" / abi / "libt3game.so")


def variant_trees() -> dict[str, Path]:
    """The checkouts the two APKs are made from: this one (local Wi-Fi) and a worktree of the
    wifi-direct branch with its own Android builds (`git worktree add ../t3native-wd wifi-direct`,
    then cmake as in README.md)."""
    direct = DIRECT_TREE
    branch = subprocess.run(["git", "-C", str(direct), "branch", "--show-current"], capture_output=True,
                            text=True).stdout.strip()
    if branch != "wifi-direct":
        raise SystemExit(f"{direct} is not a checkout of the wifi-direct branch ({branch or 'none'})")
    for abi in ("arm64-v8a", "x86_64"):
        lib = direct / f"build/android-{abi}/libt3game.so"
        if not lib.is_file():
            raise SystemExit(f"no {lib}: build the wifi-direct worktree first")
    return {"lan": ROOT, "direct": direct}


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


def launcher(dest: Path) -> None:
    """builder/launcher.cpp, static (zlib included), as `dest`: with no archive appended it runs
    the folder it lies in."""
    env = dict(os.environ, PATH=str(GPP.parent) + os.pathsep + os.environ.get("PATH", ""))
    subprocess.run([str(GPP), "-O2", "-s", "-municode", "-mwindows", "-static", str(ROOT / "builder/launcher.cpp"),
                    "-o", str(dest), "-lz", "-lshell32", "-lole32", "-luuid"], check=True, env=env)


def packed_files(folder: Path) -> list[Path]:
    """The builder folder's files that go into the one-file builder: not the user's own (key,
    conversions, APKs, settings) nor the folder's launcher."""
    files = []
    for path in sorted(folder.rglob("*")):
        rel = path.relative_to(folder)
        if path.is_file() and rel.parts[0] not in USER_DIRS and rel.name not in USER_FILES and rel.name != LAUNCHER:
            files.append(path)
    return files


def pack(folder: Path, exe: Path) -> str:
    """The one-file builder: the launcher with the folder appended as an archive (raw deflate per
    file, an index, a 40-byte trailer: magic, index offset, index size, the archive's id). Returns
    the id: the first 16 hex digits of the SHA-256 of everything before the trailer."""
    launcher(exe)
    digest = hashlib.sha256()
    index = []
    with open(exe, "ab") as out:
        base = out.tell()
        for path in packed_files(folder):
            data = path.read_bytes()
            packer = zlib.compressobj(PACK_LEVEL, zlib.DEFLATED, -15)
            packed = packer.compress(data) + packer.flush()
            method = 8
            if len(packed) >= len(data):
                packed, method = data, 0
            offset = out.tell()
            out.write(packed)
            digest.update(packed)
            name = path.relative_to(folder).as_posix().encode()
            index.append(struct.pack("<H", len(name)) + name +
                         struct.pack("<QQQIB", offset, len(packed), len(data), zlib.crc32(data), method))
        table = struct.pack("<I", len(index)) + b"".join(index)
        digest.update(table)
        index_offset = out.tell()
        out.write(table)
        archive_id = digest.hexdigest()[:16]
        out.write(PACK_MAGIC + struct.pack("<QQ", index_offset, len(table)) + archive_id.encode())
    print(f"{exe.name}: {len(index)} files from {base} B on, id {archive_id}")
    return archive_id


def check_no_game_data(out: Path) -> None:
    found = [p for p in out.rglob("*") if p.name in FORBIDDEN]
    if found:
        raise SystemExit(f"game data in the builder folder: {found[:3]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--template-only", action="store_true")
    args = ap.parse_args()

    if TEMPLATE.exists():
        shutil.rmtree(TEMPLATE)
    for variant, tree in variant_trees().items():
        template(TEMPLATE / variant, tree)
    if args.template_only:
        return 0
    out = args.out
    keep = {name: out / name for name in ("key", "work") if (out / name).exists()}
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
    shutil.copy2(ROOT / "builder/README.txt", out / "README.txt")
    python_runtime(out / "runtime/python")
    ffmpeg = ffmpeg_folder()
    (out / "runtime/ffmpeg").mkdir(parents=True)
    for path in ffmpeg.iterdir():
        if path.suffix in (".exe", ".dll") and path.name != "ffplay.exe":
            shutil.copy2(path, out / "runtime/ffmpeg" / path.name)
    shutil.copytree(ffmpeg / "licenses", out / "runtime/ffmpeg/licenses")
    launcher(out / LAUNCHER)
    check_no_game_data(out)
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"{out}: {size / 1e6:.0f} MB")
    pack(out, ONE_FILE)
    print(f"{ONE_FILE}: {ONE_FILE.stat().st_size / 1e6:.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
