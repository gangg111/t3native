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

    python -m t3builder.pipeline --jp CUE [--usa CUE] [--arcade ZIP] --out FOLDER [--variant lan|direct]
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
from t3builder.texts import set_language, t

log = logging.getLogger("t3builder")

APP_ROOT = Path(__file__).resolve().parents[1]          # the folder holding the t3builder package
# The user's side of the builder: the folder of the one-file builder's .exe (its launcher sets
# T3B_HOME; the unpacked builder itself lies elsewhere): the key, the settings, the APKs.
HOME = Path(os.environ.get("T3B_HOME", APP_ROOT.parent))
ABIS = ("arm64-v8a", "x86_64")
MOVIE_VARIANT = "small"                                   # the Android variant (build_apk skips .large)
ICON_SIZE = 192                                           # mipmap-xxxhdpi
# The APKs the builder makes: NETWORK MODE over the local Wi-Fi, or over Wi-Fi Direct (the
# wifi-direct branch: its own package, so it installs beside the other). template/<variant>.
VARIANTS = ("lan", "direct")
APK_NAMES = {"lan": "Tekken3.apk", "direct": "Tekken3.apk"}   # one name for both variants (the user's choice)
KEY_FILE = "key/signing_key.json"                       # next to the builder (HOME)
PROGRESS = "@@"                                           # stdout lines the window reads as progress
DONE = "done"                                             # the last one: "@@ done <minutes> <apk>"


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
                          Path(os.environ.get("T3B_WORK", root / "work")), HOME / KEY_FILE)
        repo = APP_ROOT.parent                              # builder/ in the t3native checkout
        return Layout(Path(os.environ.get("T3_REMAKE_ROOT", "D:/Tekken 3/Tekken3Remake")), repo / "tools",
                      repo / "build/builder/template", None, repo / "build/builder/work",
                      repo / "build/builder/work/signing_key.json")


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
        raise RuntimeError(t("e_convert"))


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


def assemble(layout: Layout, imported: Path, out: Path, key: apk_sign.Key, icon: bytes, variant: str = "lan") -> None:
    template = layout.template / variant
    import build_apk  # noqa: PLC0415  (t3native/tools: the assets' names and compression)
    with tempfile.TemporaryDirectory() as tmp:
        unsigned = Path(tmp) / "unsigned.apk"
        z = apk_sign.AlignedZip(unsigned)
        with zipfile.ZipFile(template / "base.apk") as base:
            for info in base.infolist():
                if info.filename.startswith("META-INF/"):
                    continue
                data = icon if is_icon(info.filename) else base.read(info)
                z.write_bytes(info.filename, data, info.compress_type)
        for abi in ABIS:
            z.write_file(f"lib/{abi}/libt3game.so", template / f"lib/{abi}/libt3game.so", zipfile.ZIP_STORED)
        entries = build_apk.asset_entries(imported, layout.remake / "remake/content/texts_en.json", True)
        for name, path in entries:
            z.write_file(name, path, build_apk.compression(name))
        z.close()
        bad = apk_sign.check_alignment(unsigned)
        if bad:
            raise RuntimeError(t("e_align", entries=bad[:3]))
        apk_sign.sign(unsigned, out, key)
    apk_sign.verify(out)
    log.info(t("apk_done", files=len(entries), size=f"{out.stat().st_size / 1e6:.1f}"))


def build(jp: Path, usa: Path | None, arcade: Path | None, out_dir: Path, layout: Layout | None = None,
          variant: str = "lan") -> Path:
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
        raise RuntimeError(t("e_ffmpeg"))

    step += 1
    progress(step, total, t("s_disc"))
    mpeg_movies(media, movies_from_disc)
    run_converter(convert, [*images, "--out", str(imported), "--ffmpeg", ffmpeg])

    if arcade:
        step += 1
        progress(step, total, t("s_arcade"))
        arcade_out = layout.work / "imported_arcade"
        run_converter(convert, ["--image", str(jp), "--arcade", str(arcade), "--no-music", "--no-movies",
                                "--out", str(arcade_out)])
        done = arcade_overlay.overlay(imported, arcade_out)
        if not done["stages"]:
            raise RuntimeError(t("e_arcade"))
        log.info(t("arcade_done", stages=done["stages"], models=done["models"]))

    step += 1
    progress(step, total, t("s_fonts"))
    if font_sheets_from_disc.main(["--disc", str(jp), "--out", str(imported)]):
        raise RuntimeError(t("e_fonts"))

    step += 1
    progress(step, total, t("s_key"))
    key, made = apk_sign.load_or_create(layout.key)
    log.info(t("key_new" if made else "key_kept", path=layout.key))

    step += 1
    progress(step, total, t("s_apk"))
    out_dir.mkdir(parents=True, exist_ok=True)
    apk = out_dir / APK_NAMES[variant]
    assemble(layout, imported, apk, key, make_icon(imported), variant)
    print(f"{PROGRESS} {DONE} {(time.time() - start) / 60:.1f} {apk}", flush=True)
    return apk


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jp", type=Path, required=True, help="the Japanese disc image (.cue, .ecm, .bin, .iso)")
    ap.add_argument("--usa", type=Path, help="the USA disc image: the English texts")
    ap.add_argument("--arcade", type=Path, help="MAME's tekken3 ROM set (.zip)")
    ap.add_argument("--out", type=Path, required=True, help="the folder the APK is written to")
    ap.add_argument("--lang", default="en", help="the language of the progress and the errors (en, pl)")
    ap.add_argument("--variant", default="lan", choices=VARIANTS, help="NETWORK MODE over the local Wi-Fi or Wi-Fi Direct")
    args = ap.parse_args(argv)
    set_language(args.lang)
    try:
        build(args.jp, args.usa, args.arcade, args.out, variant=args.variant)
    except Exception as e:  # noqa: BLE001  (the window shows the reason; the trace goes to the log)
        log.exception(t("error", error=e))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
