"""builder/t3builder: the window's image finder and progress lines, and the pipeline's icon."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "builder"))
from t3builder import gui, pipeline  # noqa: E402


def touch(path: Path, size: int = 1) -> Path:
    path.write_bytes(b"\x00" * size)
    return path


def test_cue_wins_over_tracks(tmp_path):
    # source: the converter reads a cue sheet's tracks itself (disc_image.py), so the .cue is the image
    touch(tmp_path / "Tekken 3 (Track 1).bin", 5000)
    cue = touch(tmp_path / "Tekken 3.cue")
    assert gui.find_image(tmp_path) == cue


def test_largest_track_without_a_cue(tmp_path):
    big = touch(tmp_path / "a.bin", 9000)
    touch(tmp_path / "b.bin", 10)
    assert gui.find_image(tmp_path) == big


def test_no_image(tmp_path):
    touch(tmp_path / "readme.txt")
    assert gui.find_image(tmp_path) is None
    assert gui.find_image(tmp_path / "missing") is None


def test_a_file_is_taken_as_is(tmp_path):
    cue = touch(tmp_path / "t3.cue")
    assert gui.find_image(cue) == cue
    assert gui.find_image(touch(tmp_path / "x.txt")) is None


def test_chd_is_refused_with_its_own_note(tmp_path):
    # source: disc_image._chd needs MAME's chdman, which the builder does not carry (decyzja usera 2026-10-22)
    touch(tmp_path / "Tekken 3.chd", 5000)
    assert gui.find_image(tmp_path) is None
    assert gui.find_image(tmp_path / "Tekken 3.chd") is None
    assert gui.missing_image_note(tmp_path) == gui.CHD_NOTE
    assert gui.missing_image_note(tmp_path / "Tekken 3.chd") == gui.CHD_NOTE
    assert gui.missing_image_note(tmp_path / "none") != gui.CHD_NOTE


def test_progress_lines():
    # source: pipeline.progress writes "@@ <step> <total> <text>"
    assert gui.parse_progress("@@ 2 5 Czcionki gry\n") == (2, 5, "Czcionki gry")
    assert gui.parse_progress("music track 3: xa stream 2\n") is None


def test_icon_entries():
    # source: aapt2 link keeps res/mipmap-xxxhdpi-v4/ic_launcher.png (the template's resource path)
    assert pipeline.is_icon("res/mipmap-xxxhdpi-v4/ic_launcher.png")
    assert not pipeline.is_icon("assets/imported/screens/title.png")


def test_icon_crops_the_logo(tmp_path):
    from PIL import Image
    screens = tmp_path / "screens"
    screens.mkdir()
    title = Image.new("RGBA", (368, 480), (0, 0, 0, 255))
    title.paste((200, 30, 30, 255), (100, 100, 300, 200))     # a 200 x 100 "logo"
    title.save(screens / "title.png")
    import io
    icon = Image.open(io.BytesIO(pipeline.make_icon(tmp_path)))
    # source: policzone ręcznie: the 200 x 100 logo centred on a 200 x 200 square, scaled to 192
    assert icon.size == (192, 192)
    assert icon.getpixel((96, 96))[0] > 150          # the logo in the middle
    assert icon.getpixel((96, 10)) == (0, 0, 0)      # black above it (the 50 px band above, scaled)
