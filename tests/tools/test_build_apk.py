"""tools/build_apk.py: which files go into the APK, under which names, stored or deflated.

Input the tool expects: the remake's `imported/` folder (game data plus Godot's `.import` files and
hidden converter stamps) and `content/texts_en.json`. The tree below is a hand-made miniature; the
expected lists are written out by hand.
"""
import sys
import zipfile
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "build_apk.py"
sys.path.insert(0, str(TOOL.parent))
import build_apk  # noqa: E402


def _tree(tmp_path):
    imported = tmp_path / "imported"
    for name in ["tables/flow.json", "screens/title.png", "screens/title.png.import",
                 "movies/m01.ogv", "movies/m01.large.mpg", "movies/m01.small.mpg", ".converter",
                 "music/a.ogg"]:
        p = imported / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b"x")
    texts = tmp_path / "content/texts_en.json"
    texts.parent.mkdir(parents=True)
    texts.write_text("{}")
    return imported, texts


def test_assets_with_movies(tmp_path):
    imported, texts = _tree(tmp_path)
    names = [n for n, _ in build_apk.asset_entries(imported, texts, True)]
    # source: the tree above by hand, sorted by path, minus .import, the hidden stamp, the Theora
    # movie (the native player reads the .mpg; changed 2026-10-21 by the user's movie decision) and
    # the large MPEG-1 variant (the remake's Android export keeps the small movies)
    assert names == ["assets/imported/movies/m01.small.mpg", "assets/imported/music/a.ogg",
                     "assets/imported/screens/title.png", "assets/imported/tables/flow.json",
                     "assets/content/texts_en.json"]


def test_assets_without_movies(tmp_path):
    imported, texts = _tree(tmp_path)
    names = [n for n, _ in build_apk.asset_entries(imported, texts, False)]
    # source: as above without the .ogv
    assert "assets/imported/movies/m01.ogv" not in names
    assert len(names) == 4
    # source: --no-movies leaves out every movie, the .mpg too
    assert not any(n.endswith(".mpg") for n in names)


def test_assets_without_music(tmp_path):
    imported, texts = _tree(tmp_path)
    names = [n for n, _ in build_apk.asset_entries(imported, texts, False, False)]
    # source: the tree above without movies and without music/a.ogg
    assert names == ["assets/imported/screens/title.png", "assets/imported/tables/flow.json", "assets/content/texts_en.json"]


def test_java_sources(tmp_path):
    for name in ["com/x/B.java", "com/x/A.java", "com/x/notes.txt"]:
        p = tmp_path / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("")
    # source: the tree above by hand: only .java files, sorted by path
    assert [p.relative_to(tmp_path).as_posix() for p in build_apk.java_sources(tmp_path)] == ["com/x/A.java", "com/x/B.java"]


def test_compression():
    # source: the manifest's extractNativeLibs=false needs stored .so; media are already compressed
    assert build_apk.compression("lib/x86_64/libt3game.so") == zipfile.ZIP_STORED
    assert build_apk.compression("assets/imported/music/a.ogg") == zipfile.ZIP_STORED
    assert build_apk.compression("assets/imported/tables/flow.json") == zipfile.ZIP_DEFLATED
    # source: MPEG-1 is already compressed (pl_mpeg reads the stored bytes)
    assert build_apk.compression("assets/imported/movies/m01.small.mpg") == zipfile.ZIP_STORED
