"""tools/make_builder.py: the checks that keep game data out of the builder folder, and the
placeholder icon. The full layout is checked by building it (tests/builder)."""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import make_builder  # noqa: E402


def test_converter_marker_is_forbidden():
    # source: Tekken3Remake tools/remake_import/common.py OUTPUT_MARKER = ".converter" (odczyt 2026-10-22)
    assert ".converter" in make_builder.FORBIDDEN
    assert "manifest.json" in make_builder.FORBIDDEN


def test_game_data_stops_the_build(tmp_path):
    (tmp_path / "remake/x").mkdir(parents=True)
    (tmp_path / "remake/x/.converter").write_text("written by convert.py\n")
    with pytest.raises(SystemExit):
        make_builder.check_no_game_data(tmp_path)


def test_clean_folder_passes(tmp_path):
    (tmp_path / "app").mkdir()
    (tmp_path / "app/a.py").write_text("x = 1\n")
    make_builder.check_no_game_data(tmp_path)


def test_placeholder_icon_is_plain(tmp_path):
    from PIL import Image
    make_builder.placeholder_icon(tmp_path / "i.png")
    image = Image.open(tmp_path / "i.png")
    # source: mipmap-xxxhdpi = 192 px (Android density buckets); one colour, so nothing of the game's logo
    assert image.size == (192, 192)
    assert len(image.getcolors()) == 1
