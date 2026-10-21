"""font_sheets_from_disc: the sheet geometry and the prefix check. Input: glyph sheets as RGBA
arrays (rows of 16 cells, cell height h), codes from 0x20."""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import font_sheets_from_disc as tool  # noqa: E402


def test_rows():
    # source: counted by hand: 0x20-0x5F is 64 codes = 4 rows of 16, 0x20-0x7F is 96 = 6 rows
    assert tool.rows(0x20, 0x5F) == 4
    assert tool.rows(0x20, 0x7F) == 6
    # source: by hand: 0x20-0x63 (up to "c") is 68 codes, 5 rows (the last one partly used)
    assert tool.rows(0x20, 0x63) == 5


def test_same_prefix():
    h = 2
    old = np.zeros((4 * h, 8, 4), np.uint8)
    old[1, 3] = (1, 2, 3, 255)
    new = np.zeros((6 * h, 8, 4), np.uint8)
    new[:4 * h] = old
    new[5 * h, 0] = (9, 9, 9, 255)                 # a new glyph below: allowed
    assert tool.same_prefix(old, new, h, 4)
    changed = new.copy()
    changed[1, 3] = (0, 0, 0, 0)                   # an old glyph changed: refused
    assert not tool.same_prefix(old, changed, h, 4)
    assert not tool.same_prefix(old, np.zeros((6 * h, 9, 4), np.uint8), h, 4)   # other width
