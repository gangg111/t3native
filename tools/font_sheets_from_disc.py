"""The text engine's glyph sheets with every character code the game draws, straight from the disc.

The remake's converter (tools/remake_import/hud.py) writes `hud/font_<f>_<colour>.png` for the
codes 0x20 to 0x5F only. The game's texts also use codes above that: ROUND TIME's last value is
`"  abc"`, three glyphs of font 0 that make the infinity sign, and with the sheets cut at 0x5F they
drew as black cells (in the remake too). This tool runs the converter's own font conversion with the
range up to 0x7F and replaces the sheets: rows of 16 cells from 0x20 as before, so the first four
rows are unchanged and hud.json's metrics still hold. Every written sheet is checked against the
converter's: the cells of 0x20 to 0x5F must be equal.

    py -3.12 tools/font_sheets_from_disc.py [--disc CUE] [--out DIR]
"""
from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path

WORKSPACE = Path(os.environ.get("T3_WORKSPACE", Path(__file__).resolve().parents[2]))
REMAKE = Path(os.environ.get("T3_REMAKE_ROOT", WORKSPACE / "Tekken3Remake"))
DISC = Path(os.environ.get("T3_DISC", WORKSPACE / "japan rev.1 dump/Tekken 3 (Japan) (Rev 1).cue"))
FIRST_CHAR = 0x20
LAST_CHAR = 0x7F
COLUMNS = 16


def rows(first: int, last: int, columns: int = COLUMNS) -> int:
    """The sheet's rows of `columns` cells for the codes first to last."""
    return (last - first + columns) // columns


def same_prefix(old, new, cell_h: int, old_rows: int) -> bool:
    """Whether `new` (a taller sheet) starts with `old`'s rows of cells unchanged."""
    return old.shape[1] == new.shape[1] and (new[:old_rows * cell_h] == old[:old_rows * cell_h]).all()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--disc", type=Path, default=DISC)
    ap.add_argument("--out", type=Path, default=REMAKE / "remake/imported")
    args = ap.parse_args(argv)
    sys.path.insert(0, str(REMAKE / "tools/remake_import"))
    import json  # noqa: PLC0415

    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415

    import common  # noqa: PLC0415  (the remake's converter)
    import hud  # noqa: PLC0415

    disc = common.Disc.open(args.disc).canonical()
    hud.LAST_CHAR = LAST_CHAR
    fonts = json.loads((args.out / "hud/hud.json").read_text())["fonts"]
    old_rows = rows(FIRST_CHAR, 0x5F)
    failed = 0
    written = 0
    with tempfile.TemporaryDirectory() as tmp:
        hud.convert(disc, common.Output(Path(tmp)))
        for sheet in sorted(Path(tmp, "hud").glob("font_*_*.png")):
            dest = args.out / "hud" / sheet.name
            f = sheet.stem.split("_")[1]
            new = np.asarray(Image.open(sheet).convert("RGBA"))
            old = np.asarray(Image.open(dest).convert("RGBA"))
            if new.shape[0] != rows(FIRST_CHAR, LAST_CHAR) * fonts[f]["height"] or \
                    not same_prefix(old, new, fonts[f]["height"], old_rows):
                failed += 1
                print(f"FAILED {sheet.name}: the codes 0x20-0x5F differ from the converter's sheet", file=sys.stderr)
                continue
            dest.write_bytes(sheet.read_bytes())
            written += 1
    print(f"{written} sheets written" + (f", {failed} failed" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
