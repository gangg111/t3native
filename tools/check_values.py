"""Integer constants and integer member defaults must keep their values in the C++ translation.

For every `const NAME := <int>` / `const NAME: int = <int>` and every class-level
`var name := <int>` / `var name: int = <int>` of remake/<path>.gd whose value is a plain integer
literal (decimal or hex, optional minus sign), finds `NAME = <literal>` / `name = <literal>` /
`name{<literal>}` in src/<path>.h (+ .cpp) and compares the numbers.

    python tools/check_values.py [--remake DIR] [--src DIR]

Prints one line per mismatch or missing literal; exit code 1 when any value differs.
Names whose C++ initialiser is not a literal (an expression) are reported as `unchecked`.
"""
import argparse
import re
import sys
from pathlib import Path

INT = r"-?(?:0x[0-9A-Fa-f]+|\d+)"
GD_CONST = re.compile(rf"^const\s+([A-Za-z_]\w*)\s*(?::\s*int\s*)?:?=\s*({INT})\s*(?:#.*)?$")
GD_VAR = re.compile(rf"^(?:static\s+)?var\s+([A-Za-z_]\w*)\s*(?::\s*int\s*)?:?=\s*({INT})\s*(?:#.*)?$")


def gd_values(text: str) -> dict[str, int]:
    out = {}
    for line in text.splitlines():
        if line[:1] in ("\t", " "):
            continue            # inner classes and function bodies: only the class level
        m = GD_CONST.match(line.strip()) or GD_VAR.match(line.strip())
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def cpp_value(text: str, name: str):
    """The integer literal C++ initialises `name` with, None when absent, 'expr' when not a literal."""
    m = re.search(rf"\b{re.escape(name)}\s*(?:=|\{{)\s*([^;,}}\n]+)", text)
    if not m:
        return None
    init = m.group(1).strip().rstrip(")")
    init = re.sub(r"(?<=[0-9A-Fa-f])(?:LL|ll|ULL|U|L)\b", "", init)
    init = re.sub(r"^(?:gd::Int|static_cast<gd::Int>)\((.*)\)?$", r"\1", init)
    if re.fullmatch(INT, init):
        return int(init, 0)
    return "expr"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--remake", type=Path, default=Path(__file__).resolve().parents[2] / "Tekken3Remake/remake")
    p.add_argument("--src", type=Path, default=Path(__file__).resolve().parents[1] / "src")
    a = p.parse_args()
    differ = 0
    checked = 0
    for h in sorted(a.src.rglob("*.h")):
        gd = a.remake / h.relative_to(a.src).with_suffix(".gd")
        if not gd.exists():
            continue
        text = h.read_text(encoding="utf-8")
        cpp = h.with_suffix(".cpp")
        if cpp.exists():
            text += "\n" + cpp.read_text(encoding="utf-8")
        for name, want in gd_values(gd.read_text(encoding="utf-8")).items():
            got = cpp_value(text, name)
            rel = gd.relative_to(a.remake)
            if got is None:
                print(f"{rel}: {name} = {want}: no initialiser in C++")
                differ += 1
            elif got == "expr":
                print(f"{rel}: {name} = {want}: unchecked (C++ initialiser is an expression)")
            elif got != want:
                print(f"{rel}: {name}: GDScript {want} != C++ {got}")
                differ += 1
            else:
                checked += 1
    print(f"{checked} values equal, {differ} differ or missing")
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
