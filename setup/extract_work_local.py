"""Local stand-in for tools/research/extract_work.py without the original SLPS-01300 disc.

The BNS record names come from docs/research/bns-original-index.md (the original EXE's
index, the same 303 records as Rev.1 and USA) instead of the original disc's EXE.
Writes work/jp_rev1 and work/usa exactly like extract_work.write_release.
"""
import logging
import re
import sys
from pathlib import Path

REPO = Path(r"D:\Tekken 3\Tekken3Remake")
sys.path.insert(0, str(REPO / "tools" / "research"))

from compare_bns import JAPAN, USA, read_records  # noqa: E402
from extract_work import read_exe, write_release  # noqa: E402

DISCS = {
    "jp_rev1": (Path(r"D:\Tekken 3\japan rev.1 dump\Tekken 3 (Japan) (Rev 1) (Track 1).bin"), JAPAN),
    "usa": (Path(r"D:\Tekken 3\dump\Tekken 3 (USA) (Track 1).bin"), USA),
}


def names_from_doc() -> list[str]:
    rows = re.findall(r"^\| *(\d+) *\| `([^`]+)` \|", (REPO / "docs/research/bns-original-index.md").read_text(), re.M)
    names = [name for _, name in sorted((int(i), n) for i, n in rows)]
    if [int(i) for i, _ in sorted((int(i), n) for i, n in rows)] != list(range(len(names))):
        raise SystemExit("index doc ids are not 0..n-1")
    return names


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    names = names_from_doc()
    for label, (image, profile) in DISCS.items():
        records, _ = read_records(image, profile)
        if len(records) != len(names):
            raise SystemExit(f"{label}: {len(records)} records, {len(names)} names")
        write_release(REPO / "work" / label, read_exe(image, profile), records, names)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
