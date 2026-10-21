"""Reference digests for the C++ PNG decoder: for every PNG under a folder, the SHA-256 of its
pixels as RGBA8 rows (top to bottom), decoded by Pillow (an independent decoder).

    python tools/png_digests.py <folder> <out.tsv>

Output: one line per file, `<path relative to folder, forward slashes>\t<width>\t<height>\t<sha256>`.
"""
import hashlib
import sys
from pathlib import Path

from PIL import Image


def digest(path: Path) -> tuple[int, int, str]:
    with Image.open(path) as im:
        rgba = im.convert("RGBA")
        return rgba.width, rgba.height, hashlib.sha256(rgba.tobytes()).hexdigest()


def main() -> int:
    root = Path(sys.argv[1])
    lines = []
    for p in sorted(root.rglob("*.png")):
        w, h, d = digest(p)
        lines.append(f"{p.relative_to(root).as_posix()}\t{w}\t{h}\t{d}")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(lines)} PNG files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
