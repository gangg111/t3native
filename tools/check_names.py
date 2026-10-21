"""Every name a GDScript file declares must appear in its C++ translation (RULES.md: names are
identical). Lists, per file, the members, constants, methods, enum values and inner classes of
remake/<path>.gd that the matching src/<path>.h (and .cpp, when present) never mention.

    python tools/check_names.py [--remake DIR] [--src DIR] [--cpp] [paths...]

--cpp also requires the .cpp to exist. Exit code 1 when anything is missing.
"""
import argparse
import re
import sys
from pathlib import Path

DECL = [
    re.compile(r"^\s*(?:static\s+)?var\s+([A-Za-z_][A-Za-z0-9_]*)"),
    re.compile(r"^\s*const\s+([A-Za-z_][A-Za-z0-9_]*)"),
    re.compile(r"^\s*(?:static\s+)?func\s+([A-Za-z_][A-Za-z0-9_]*)"),
    re.compile(r"^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)"),
]
ENUM = re.compile(r"^\s*enum\s*([A-Za-z_][A-Za-z0-9_]*)?\s*\{([^}]*)\}?")
SKIP = {"_init", "_to_string"}


def _indent(line: str) -> int:
    n = len(line) - len(line.lstrip("\t"))
    return n if n else (len(line) - len(line.lstrip(" "))) // 4


def declared(gd_text: str) -> list[str]:
    """Class-level names only: indent 0, or indent 1 inside an inner `class X:` block
    (deeper lines are function bodies and their locals)."""
    names = []
    lines = gd_text.splitlines()
    in_class = False
    i = 0
    while i < len(lines):
        line = lines[i].split("#", 1)[0]
        if not line.strip():
            i += 1
            continue
        depth = _indent(line)
        if depth == 0:
            in_class = line.lstrip().startswith("class ")
        elif not (depth == 1 and in_class):
            i += 1
            continue
        m = ENUM.match(line)
        if m:
            body = m.group(2)
            j = i
            while "}" not in lines[j] and j + 1 < len(lines):
                j += 1
                body += " " + lines[j].split("#", 1)[0]
            if m.group(1):
                names.append(m.group(1))
            for item in body.replace("}", "").split(","):
                item = item.strip().split("=")[0].strip()
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", item):
                    names.append(item)
            i = j + 1
            continue
        for rx in DECL:
            m = rx.match(line)
            if m and m.group(1) not in SKIP:
                names.append(m.group(1))
                break
        i += 1
    return names


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--remake", type=Path, default=Path(r"D:\Tekken 3\Tekken3Remake\remake"))
    p.add_argument("--src", type=Path, default=Path(__file__).resolve().parents[1] / "src")
    p.add_argument("--cpp", action="store_true")
    p.add_argument("paths", nargs="*")
    a = p.parse_args()
    bad = 0
    headers = sorted(a.src.rglob("*.h")) if not a.paths else [a.src / (x.removesuffix(".gd") + ".h") for x in a.paths]
    for h in headers:
        rel = h.relative_to(a.src).with_suffix(".gd")
        gd = a.remake / rel
        if not gd.exists():
            continue
        text = h.read_text(encoding="utf-8")
        cpp = h.with_suffix(".cpp")
        if cpp.exists():
            text += cpp.read_text(encoding="utf-8")
        elif a.cpp:
            print(f"{rel}: no .cpp")
            bad += 1
        words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text))
        missing = [n for n in dict.fromkeys(declared(gd.read_text(encoding="utf-8"))) if n not in words]
        if missing:
            bad += 1
            print(f"{rel}: {', '.join(missing)}")
    print(f"{len(headers)} files checked, {bad} with missing names")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
