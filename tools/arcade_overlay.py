"""The arcade version's data next to the PlayStation's, for the VERSION option (CONSOLE / ARCADE).

The remake's converter takes the arcade ROM set with `--arcade` and then REPLACES the PlayStation
character models (convert.py: the choice is made at conversion time). The port switches at run
time, so it needs both: convert once more with `--arcade` into another folder, then this tool
copies from that conversion into the PlayStation one

- the stage scenes: `stages/<letter>/arcade/` (scene, atlas, sky, floor, props, arcade.json) and
  `stages/arcade_floor_fade.json`, `stages/arcade_sine.json` (arcade.py);
- the arcade character models, as `characters/<costume>/arcade/` (arcade_character.py's
  MODEL_FILES and eye atlases: model.json, mesh.bin.gz, texture.png, texture_eye<n>.png), for the
  costumes whose model.json says "source": "arcade"; `characters/arcade_attachments.json`;
- the manifest's group "arcade" (Settings offers the arcade backdrops only with it).

    python tools/arcade_overlay.py --into <imported> --arcade <imported from the --arcade run>

Game data: the folders stay outside git.
"""
import argparse
import json
import logging
import shutil
import sys
from pathlib import Path

log = logging.getLogger("arcade_overlay")

STAGE_FILES = ("arcade_floor_fade.json", "arcade_sine.json")
MODEL_FILES = ("model.json", "mesh.bin.gz", "texture.png")
EYE_GLOB = "texture_eye*.png"
ATTACHMENTS = "arcade_attachments.json"


def _replace_tree(src: Path, dst: Path) -> int:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    return sum(1 for p in dst.rglob("*") if p.is_file())


def overlay(into: Path, arcade: Path) -> dict:
    """Copies the arcade data from `arcade` into `into`; returns what was copied."""
    done = {"stages": 0, "stage_files": 0, "models": 0, "model_files": 0}
    for scene in sorted((arcade / "stages").glob("*/arcade")):
        target_stage = into / "stages" / scene.parent.name
        if not target_stage.is_dir():
            continue
        done["stage_files"] += _replace_tree(scene, target_stage / "arcade")
        done["stages"] += 1
    for name in STAGE_FILES:
        if (arcade / "stages" / name).is_file():
            shutil.copy2(arcade / "stages" / name, into / "stages" / name)
            done["stage_files"] += 1
    for model in sorted((arcade / "characters").glob("*/model.json")):
        costume = model.parent
        if json.loads(model.read_text(encoding="utf-8")).get("source") != "arcade":
            continue
        target = into / "characters" / costume.name / "arcade"
        if not target.parent.is_dir():
            continue
        if target.exists():
            shutil.rmtree(target)
        target.mkdir()
        for path in [costume / n for n in MODEL_FILES] + sorted(costume.glob(EYE_GLOB)):
            shutil.copy2(path, target / path.name)
            done["model_files"] += 1
        done["models"] += 1
    if (arcade / "characters" / ATTACHMENTS).is_file():
        shutil.copy2(arcade / "characters" / ATTACHMENTS, into / "characters" / ATTACHMENTS)
    manifest_path = into / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest.setdefault("groups", {})["arcade"] = done["stages"] > 0
    manifest_path.write_text(json.dumps(manifest, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return done


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--into", type=Path, required=True, help="the PlayStation conversion (remake/imported)")
    parser.add_argument("--arcade", type=Path, required=True, help="the conversion made with --arcade")
    args = parser.parse_args()
    done = overlay(args.into, args.arcade)
    log.info("arcade: %d stage scenes (%d files), %d character models (%d files)",
             done["stages"], done["stage_files"], done["models"], done["model_files"])
    return 0 if done["stages"] else 1


if __name__ == "__main__":
    sys.exit(main())
