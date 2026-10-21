"""tools/png_digests.py: SHA-256 of a PNG's RGBA8 pixels via Pillow.

Input the tool expects: a folder of PNG files. The expected digest below is computed by hashing a
pixel buffer written out by hand (not by decoding the PNG), so it does not depend on the tool.
"""
import hashlib
import subprocess
import sys
from pathlib import Path

from PIL import Image

TOOL = Path(__file__).resolve().parents[2] / "tools" / "png_digests.py"
sys.path.insert(0, str(TOOL.parent))
import png_digests  # noqa: E402


def test_digest_of_known_pixels(tmp_path):
    # 2x1 image: red opaque, then half-transparent blue (hand-written RGBA bytes)
    raw = bytes([255, 0, 0, 255, 0, 0, 255, 128])
    Image.frombytes("RGBA", (2, 1), raw).save(tmp_path / "a.png")
    # source: sha256 of the hand-written bytes above
    assert png_digests.digest(tmp_path / "a.png") == (2, 1, hashlib.sha256(raw).hexdigest())


def test_palette_png_expands_to_rgba(tmp_path):
    im = Image.new("P", (1, 1))
    im.putpalette([10, 20, 30] + [0] * 765)
    im.putpixel((0, 0), 0)
    im.save(tmp_path / "p.png")
    # source: palette entry 0 is (10, 20, 30), opaque, written by hand
    assert png_digests.digest(tmp_path / "p.png") == (1, 1, hashlib.sha256(bytes([10, 20, 30, 255])).hexdigest())


def test_listing(tmp_path):
    (tmp_path / "sub").mkdir()
    Image.frombytes("RGBA", (1, 1), bytes([1, 2, 3, 4])).save(tmp_path / "sub" / "x.png")
    out = tmp_path / "d.tsv"
    r = subprocess.run([sys.executable, str(TOOL), str(tmp_path), str(out)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    # source: one file, 1x1, digest of the 4 hand-written bytes
    assert out.read_text(encoding="utf-8") == f"sub/x.png\t1\t1\t{hashlib.sha256(bytes([1, 2, 3, 4])).hexdigest()}\n"
