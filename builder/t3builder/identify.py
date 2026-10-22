"""Which release a disc image or an arcade set is, for the window: the converter's own checks
(common._identify reads the image's executable; arcade.open_set checks the ROM chips' CRCs and,
for the older MAME names, the program itself), so the window says exactly what the build will use.
"""
from __future__ import annotations

from pathlib import Path

from t3builder import pipeline

# The releases the converter reads (common.RELEASES), by their Redump names and serials.
DISC_LABELS = {
    "jp_rev1": "Tekken 3 (Japan) (Rev 1), SLPS-01300",
    "jp_orig": "Tekken 3 (Japan), SLPS-01300",
    "usa": "Tekken 3 (USA), SLUS-00402",
}
ARCADE_LABEL = "MAME tekken3, World ver. E1"
JAPANESE = ("jp_rev1", "jp_orig")


def _converter() -> None:
    pipeline.import_paths(pipeline.Layout.detect())


def disc(path: Path) -> tuple[str | None, str]:
    """(release key, "") of a disc image, or (None, the reason it is not one the converter reads)."""
    _converter()
    import common  # noqa: PLC0415  (the remake's converter)
    from disc_image import ImageError, open_image  # noqa: PLC0415
    try:
        image = open_image(path)
    except (ImageError, OSError, ValueError) as e:
        return None, str(e)
    try:
        release, _ = common._identify(image)
        return release.key, ""
    except (common.DiscError, ValueError, OSError) as e:
        return None, str(e)
    finally:
        image.close()


def arcade(path: Path) -> tuple[bool, str]:
    """(True, "") for MAME's tekken3 set (World ver. E1), else (False, the reason)."""
    _converter()
    import arcade as arcade_set  # noqa: PLC0415  (the remake's converter)
    try:
        arcade_set.open_set(path)
        return True, ""
    except arcade_set.ArcadeError as e:
        return False, str(e)
