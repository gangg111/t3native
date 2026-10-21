"""tools/arcade_overlay.py on two small conversion trees made here.

Input: a PlayStation `imported` tree (stages a, b; costumes 00, 30) and an `--arcade` one (stage a
with an arcade scene, costume 00 an arcade model, costume 30 the PlayStation's, as
arcade_character.py keeps Mokujin's).
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import arcade_overlay  # noqa: E402


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def make_trees(tmp_path: Path) -> tuple[Path, Path]:
    ps, ar = tmp_path / "ps", tmp_path / "arcade"
    for root in (ps, ar):
        write(root / "stages/a/stage.json", "{}")
        write(root / "stages/b/stage.json", "{}")
        write(root / "characters/costume_00/move_list.bin", "moves")
        write(root / "characters/costume_30/model.json", json.dumps({"parts": []}))
    write(ps / "manifest.json", json.dumps({"groups": {"music": True}}))
    write(ps / "characters/costume_00/model.json", json.dumps({"parts": []}))
    write(ps / "characters/costume_00/texture_closed.png", "ps closed eyes")
    write(ar / "stages/a/arcade/arcade.json", "{\"scene_scale\": 10}")
    write(ar / "stages/a/arcade/scene.bin", "scene")
    write(ar / "stages/arcade_sine.json", "[]")
    write(ar / "stages/arcade_floor_fade.json", "[]")
    write(ar / "characters/costume_00/model.json", json.dumps({"source": "arcade"}))
    write(ar / "characters/costume_00/mesh.bin.gz", "arcade mesh")
    write(ar / "characters/costume_00/texture.png", "arcade texture")
    write(ar / "characters/costume_00/texture_eye1.png", "eye 1")
    write(ar / "characters/costume_00/texture_eye2.png", "eye 2")
    write(ar / "characters/arcade_attachments.json", "{}")
    return ps, ar


def test_overlay_puts_both_versions_side_by_side(tmp_path):
    ps, ar = make_trees(tmp_path)
    done = arcade_overlay.overlay(ps, ar)
    # source: by hand from make_trees: stage a's 2 files + the 2 shared files; costume 00's 3 model
    # files + 2 eye atlases; costume 30 is not "source": "arcade"
    assert done == {"stages": 1, "stage_files": 4, "models": 1, "model_files": 5}
    assert (ps / "stages/a/arcade/scene.bin").read_text() == "scene"
    assert not (ps / "stages/b/arcade").exists()
    assert (ps / "stages/arcade_sine.json").is_file()
    arcade_model = ps / "characters/costume_00/arcade"
    assert sorted(p.name for p in arcade_model.iterdir()) == [
        "mesh.bin.gz", "model.json", "texture.png", "texture_eye1.png", "texture_eye2.png"]
    # The PlayStation model stays as it was.
    assert json.loads((ps / "characters/costume_00/model.json").read_text()) == {"parts": []}
    assert (ps / "characters/costume_00/texture_closed.png").is_file()
    assert not (ps / "characters/costume_30/arcade").exists()
    assert (ps / "characters/arcade_attachments.json").is_file()
    manifest = json.loads((ps / "manifest.json").read_text())
    assert manifest["groups"] == {"music": True, "arcade": True}


def test_overlay_again_replaces_not_mixes(tmp_path):
    ps, ar = make_trees(tmp_path)
    arcade_overlay.overlay(ps, ar)
    (ar / "characters/costume_00/texture_eye2.png").unlink()
    write(ps / "stages/a/arcade/stale.bin", "old")
    arcade_overlay.overlay(ps, ar)
    # source: by hand: the second run copies only what the arcade tree has now
    assert not (ps / "characters/costume_00/arcade/texture_eye2.png").exists()
    assert not (ps / "stages/a/arcade/stale.bin").exists()
