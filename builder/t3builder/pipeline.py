"""The builder's whole run: the user's discs (and the arcade ROM set) to a signed APK.

1. The remake's converter (tools/remake_import/convert.py) from the Japanese disc, with the USA
   disc's English when given, into `<work>/imported`; its movies are encoded straight to MPEG-1
   for the native player (movies_from_disc.encode_movie) instead of Ogg Theora.
2. With the arcade set: a second conversion with `--arcade` into `<work>/imported_arcade` and its
   stages and models laid over the first (arcade_overlay.py).
3. The glyph sheets with every character code (font_sheets_from_disc.py).
4. The APK: the template (manifest, resources, classes.dex; the icon made from the disc's title
   picture), the prebuilt native libraries and the converted data as assets, aligned and signed
   with the builder's own key (apk_sign.py), kept in the builder's folder after the first run.

The template and the libraries come from the developer (tools/make_builder.py) and hold no game
data; everything taken from the discs stays on the user's computer.

    python -m t3builder.pipeline --jp CUE [--usa CUE] [--arcade ZIP] --out FOLDER
"""
from __future__ import annotations

import argparse
import io
import logging
import os
import shutil
import sys
import tempfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path

from t3builder import apk_sign

log = logging.getLogger("t3builder")

APP_ROOT = Path(__file__).resolve().parents[1]          # the folder holding the t3builder package
ABIS = ("arm64-v8a", "x86_64")
MOVIE_VARIANT = "small"                                   # the Android variant (build_apk skips .large)
ICON_SIZE = 192                                           # mipmap-xxxhdpi
APK_NAME = "Tekken3.apk"
PROGRESS = "@@"                                           # stdout lines the window reads as progress


@dataclass
class Layout:
    """Where the builder's parts are: next to each other in the distributed folder, or in the
    developer's checkouts."""
    remake: Path            # a checkout of the remake: tools/remake_import, tools/research, remake/content
    tools: Path             # t3native/tools
    template: Path          # base.apk and lib/<abi>/libt3game.so
    ffmpeg: Path | None     # the folder of ffmpeg.exe and ffprobe.exe (None: the PATH's)
    work: Path              # the conversions, kept between runs
    key: Path               # the signing key

    @staticmethod
    def detect() -> "Layout":
        root = APP_ROOT.parent
        if (root / "remake").is_dir():
            return Layout(root / "remake", root / "t3native/tools", root / "template", root / "runtime/ffmpeg",
                          root / "praca", root / "klucz/klucz.json")
        repo = APP_ROOT.parent                              # builder/ in the t3native checkout
        return Layout(Path(os.environ.get("T3_REMAKE_ROOT", "D:/Tekken 3/Tekken3Remake")), repo / "tools",
                      repo / "build/builder/template", None, repo / "build/builder/work",
                      repo / "build/builder/work/klucz.json")


def progress(step: int, total: int, text: str) -> None:
    print(f"{PROGRESS} {step} {total} {text}", flush=True)


def import_paths(layout: Layout) -> None:
    for path in (layout.remake / "tools/remake_import", layout.tools):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    if layout.ffmpeg is not None:
        os.environ["PATH"] = str(layout.ffmpeg) + os.pathsep + os.environ.get("PATH", "")


def mpeg_movies(media, movies_from_disc) -> None:
    """Makes the converter write each movie as the Android MPEG-1 variant; movies.json keeps the
    converter's own names (the native player reads `<name>.<variant>.mpg` beside each `.ogv`)."""
    def convert_movie(out, tool, name, raw, arrivals):
        def keep(variant: str, part: Path) -> None:
            out.write(f"movies/{name}.{variant}.mpg", part.read_bytes())
        duration, failures = movies_from_disc.encode_movie(media, tool, raw, arrivals,
                                                            {MOVIE_VARIANT: movies_from_disc.QUALITIES[MOVIE_VARIANT]}, keep)
        if failures:
            raise media.MediaError(f"movie {name}: {'; '.join(failures)}")
        log.info("movie %s: %d frames, %.1f s", name, len(arrivals), duration)
        return {"video": [f"{name}.{v}.ogv" for v in media.THEORA_QUALITIES]}, duration
    media.convert_movie = convert_movie


def run_converter(convert, argv: list[str]) -> None:
    saved = sys.argv
    sys.argv = ["convert.py", *argv]
    try:
        code = convert.main()
    finally:
        sys.argv = saved
    if code:
        raise RuntimeError("konwersja płyty nie powiodła się (szczegóły w logu powyżej)")


def make_icon(imported: Path) -> bytes:
    """The launcher icon from the disc's title picture: its logo on black, square."""
    from PIL import Image  # noqa: PLC0415
    title = Image.open(imported / "screens/title.png").convert("RGBA")
    # The picture is opaque on black: the logo is what is brighter than the black.
    bright = title.convert("RGB").point(lambda v: 255 if v > 40 else 0).convert("L")
    box = bright.getbbox() or (0, 0, *title.size)
    logo = title.crop(box)
    side = max(logo.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 255))
    square.alpha_composite(logo, ((side - logo.width) // 2, (side - logo.height) // 2))
    buffer = io.BytesIO()
    square.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS).convert("RGB").save(buffer, "PNG")
    return buffer.getvalue()


def is_icon(name: str) -> bool:
    return name.startswith("res/mipmap") and Path(name).stem == "ic_launcher" and name.endswith(".png")


def assemble(layout: Layout, imported: Path, out: Path, key: apk_sign.Key, icon: bytes) -> None:
    import build_apk  # noqa: PLC0415  (t3native/tools: the assets' names and compression)
    with tempfile.TemporaryDirectory() as tmp:
        unsigned = Path(tmp) / "unsigned.apk"
        z = apk_sign.AlignedZip(unsigned)
        with zipfile.ZipFile(layout.template / "base.apk") as base:
            for info in base.infolist():
                if info.filename.startswith("META-INF/"):
                    continue
                data = icon if is_icon(info.filename) else base.read(info)
                z.write_bytes(info.filename, data, info.compress_type)
        for abi in ABIS:
            z.write_file(f"lib/{abi}/libt3game.so", layout.template / f"lib/{abi}/libt3game.so", zipfile.ZIP_STORED)
        entries = build_apk.asset_entries(imported, layout.remake / "remake/content/texts_en.json", True)
        for name, path in entries:
            z.write_file(name, path, build_apk.compression(name))
        z.close()
        bad = apk_sign.check_alignment(unsigned)
        if bad:
            raise RuntimeError(f"niewyrównane wpisy APK: {bad[:3]}")
        apk_sign.sign(unsigned, out, key)
    apk_sign.verify(out)
    log.info("%d plików danych, APK %.1f MB", len(entries), out.stat().st_size / 1e6)


def build(jp: Path, usa: Path | None, arcade: Path | None, out_dir: Path, layout: Layout | None = None) -> Path:
    layout = layout or Layout.detect()
    import_paths(layout)
    import arcade_overlay  # noqa: PLC0415
    import convert  # noqa: PLC0415  (the remake's converter: puts tools/research on the path)
    import font_sheets_from_disc  # noqa: PLC0415
    import media  # noqa: PLC0415
    import movies_from_disc  # noqa: PLC0415

    total = 5 if arcade else 4
    step = 0
    start = time.time()
    images = ["--image", str(jp)] + (["--image", str(usa)] if usa else [])
    imported = layout.work / "imported"
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("brak FFmpeg w folderze kompilatora")

    step += 1
    progress(step, total, "Konwersja płyty PS1 (grafika, dźwięk, muzyka, filmy)")
    mpeg_movies(media, movies_from_disc)
    run_converter(convert, [*images, "--out", str(imported), "--ffmpeg", ffmpeg])

    if arcade:
        step += 1
        progress(step, total, "Konwersja automatu (sceny i modele postaci)")
        arcade_out = layout.work / "imported_arcade"
        run_converter(convert, ["--image", str(jp), "--arcade", str(arcade), "--no-music", "--no-movies",
                                "--out", str(arcade_out)])
        done = arcade_overlay.overlay(imported, arcade_out)
        if not done["stages"]:
            raise RuntimeError("zestaw automatu nie dał żadnej sceny")
        log.info("automat: %d scen, %d modeli postaci", done["stages"], done["models"])

    step += 1
    progress(step, total, "Czcionki gry")
    if font_sheets_from_disc.main(["--disc", str(jp), "--out", str(imported)]):
        raise RuntimeError("czcionki: arkusze różnią się od konwertera")

    step += 1
    progress(step, total, "Klucz podpisu")
    key, made = apk_sign.load_or_create(layout.key)
    log.info("%s klucz podpisu: %s", "nowy" if made else "zapisany", layout.key)

    step += 1
    progress(step, total, "Składanie i podpisywanie APK")
    out_dir.mkdir(parents=True, exist_ok=True)
    apk = out_dir / APK_NAME
    assemble(layout, imported, apk, key, make_icon(imported))
    progress(total, total, f"Gotowe w {(time.time() - start) / 60:.1f} min: {apk}")
    return apk


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jp", type=Path, required=True, help="the Japanese disc image (.cue, .chd, .bin, .iso)")
    ap.add_argument("--usa", type=Path, help="the USA disc image: the English texts")
    ap.add_argument("--arcade", type=Path, help="MAME's tekken3 ROM set (.zip)")
    ap.add_argument("--out", type=Path, required=True, help="the folder the APK is written to")
    args = ap.parse_args(argv)
    try:
        build(args.jp, args.usa, args.arcade, args.out)
    except Exception as e:  # noqa: BLE001  (the window shows the reason; the trace goes to the log)
        log.exception("BŁĄD: %s", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
