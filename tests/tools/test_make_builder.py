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


def read_archive(exe: Path) -> tuple[str, dict[str, bytes]]:
    """The archive at the end of `exe`, read by the format builder/launcher.cpp reads (written
    here again, apart from make_builder.pack): 40-byte trailer, index, raw deflate or stored data."""
    import struct
    import zlib
    data = exe.read_bytes()
    trailer = data[-40:]
    assert trailer[:8] == b"T3BPAK01"
    index_offset, index_size = struct.unpack_from("<QQ", trailer, 8)
    index = data[index_offset:index_offset + index_size]
    count = struct.unpack_from("<I", index)[0]
    p, files = 4, {}
    for _ in range(count):
        n = struct.unpack_from("<H", index, p)[0]
        name = index[p + 2:p + 2 + n].decode()
        offset, packed, size, crc, method = struct.unpack_from("<QQQIB", index, p + 2 + n)
        p += 2 + n + 29
        raw = data[offset:offset + packed]
        content = zlib.decompress(raw, -15) if method == 8 else raw
        assert len(content) == size and zlib.crc32(content) == crc
        files[name] = content
    return trailer[24:].decode(), files


def test_pack_round_trip_and_user_files_left_out(tmp_path, monkeypatch):
    folder = tmp_path / "Tekken3Builder"
    (folder / "app/t3builder").mkdir(parents=True)
    (folder / "app/t3builder/x.py").write_bytes(b"print('x')\n" * 50)     # bytes: no CRLF translation
    (folder / "runtime").mkdir()
    (folder / "runtime/random.bin").write_bytes(bytes(range(256)) * 3)
    for user in ("klucz/klucz.json", "praca/imported/a.png", "APK/Tekken3.apk", "ustawienia.json", "crash.log"):
        (folder / user).parent.mkdir(parents=True, exist_ok=True)
        (folder / user).write_text("user's")
    (folder / make_builder.LAUNCHER).write_bytes(b"MZ folder launcher")
    monkeypatch.setattr(make_builder, "launcher", lambda dest: dest.write_bytes(b"MZ stub"))
    exe = tmp_path / "one.exe"
    archive_id = make_builder.pack(folder, exe)
    assert exe.read_bytes().startswith(b"MZ stub")
    read_id, files = read_archive(exe)
    assert read_id == archive_id and len(archive_id) == 16
    # source: the folder written above, minus the user's own files and the folder's launcher
    assert sorted(files) == ["app/t3builder/x.py", "runtime/random.bin"]
    assert files["app/t3builder/x.py"] == b"print('x')\n" * 50
    assert files["runtime/random.bin"] == bytes(range(256)) * 3


def test_placeholder_icon_is_plain(tmp_path):
    from PIL import Image
    make_builder.placeholder_icon(tmp_path / "i.png")
    image = Image.open(tmp_path / "i.png")
    # source: mipmap-xxxhdpi = 192 px (Android density buckets); one colour, so nothing of the game's logo
    assert image.size == (192, 192)
    assert len(image.getcolors()) == 1
