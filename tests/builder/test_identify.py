"""builder/t3builder/identify.py and the window's field checks: which release a chosen image is.

Input: the user's own dumps (skipped where missing) and small files made here."""
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "builder"))
from t3builder import gui, identify  # noqa: E402

JP = Path("D:/Tekken 3/japan rev.1 dump/Tekken 3 (Japan) (Rev 1).cue")
USA = Path("D:/Tekken 3/dump/Tekken 3 (USA).cue")
ARCADE = Path("D:/Tekken 3/_arcade/tekken3.zip")
HAVE_DISCS = JP.is_file() and USA.is_file()


@pytest.mark.skipif(not HAVE_DISCS, reason="the user's Japan Rev.1 and USA dumps are not on this computer")
def test_the_users_discs():
    # source: docs/research/disc-variants.md (Japan Rev.1 boots SLPS_013.00, USA TEKKEN3/SLUS_004.02)
    assert identify.disc(JP) == ("jp_rev1", "")
    assert identify.disc(USA) == ("usa", "")
    assert gui.check_jp(str(JP.parent)) == (True, "", "Tekken 3 (Japan) (Rev 1), SLPS-01300")
    assert gui.check_usa(str(USA.parent)) == (True, "", "Tekken 3 (USA), SLUS-00402")
    # Each in the other's field: the window names the right one.
    assert gui.check_jp(str(USA.parent)) == (False, "is_usa", "")
    assert gui.check_usa(str(JP.parent)) == (False, "is_japanese", "")


@pytest.mark.skipif(not ARCADE.is_file(), reason="the user's arcade set is not on this computer")
def test_the_users_arcade_set():
    # source: memory arcade-rom-tet1vere (the user's set: E1's program under the older tet1vere names)
    assert gui.check_arcade(str(ARCADE)) == (True, "", "MAME tekken3, World ver. E1")


def test_not_a_disc(tmp_path):
    (tmp_path / "x.bin").write_bytes(b"\x00" * 2352 * 20)
    assert gui.check_jp(str(tmp_path)) == (False, "unknown_disc", "")
    assert gui.check_jp(str(tmp_path / "empty")) == (False, "unknown_disc", "")


def test_not_the_arcade_set(tmp_path):
    with zipfile.ZipFile(tmp_path / "other.zip", "w") as z:
        z.writestr("tet2vere1.2e", b"\x00" * 16)
    assert gui.check_arcade(str(tmp_path / "other.zip")) == (False, "bad_arcade", "")
    (tmp_path / "broken.zip").write_bytes(b"not a zip")
    assert gui.check_arcade(str(tmp_path / "broken.zip")) == (False, "bad_arcade", "")
