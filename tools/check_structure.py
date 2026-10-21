"""Structural review of a translation: per function, the GDScript and C++ control-flow skeletons
must have the same counts of branches, loops, returns and conditional expressions. A dropped or
added `if`, `elif`, `for`, `while`, `return` or ternary shows up as a count mismatch.

    python tools/check_structure.py [--remake DIR] [--src DIR] [paths...]

Counted per function:
  branch : GDScript statement `if`/`elif` lines;   C++ `if (` (an `else if` counts once)
  loop   : `for` / `while` statements
  ret    : `return` statements
  cond   : GDScript `x if c else y`;              C++ `?` (ternary)
Functions with `match` in GDScript are reported only when loops or returns differ (a match may
become a switch or an if-chain). Output: `path:func: kind gd=N cpp=M`. Exit 1 on any mismatch.
"""
import argparse
import re
import sys
from pathlib import Path

FUNC_GD = re.compile(r"^(\t*)(?:static\s+)?func\s+([A-Za-z_]\w*)\s*\(")
CLASS_GD = re.compile(r"^(\t*)class\s+([A-Za-z_]\w*)")


def _strip_gd(line: str) -> str:
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    return line.split("#", 1)[0]


def gd_functions(text: str) -> dict[str, dict[str, int]]:
    """function name (Inner.name for inner classes) -> counts."""
    out: dict[str, dict[str, int]] = {}
    lines = text.splitlines()
    i = 0
    cls_stack: list[tuple[int, str]] = []
    while i < len(lines):
        raw = lines[i]
        m_cls = CLASS_GD.match(raw)
        m = FUNC_GD.match(raw)
        if m_cls and not m:
            depth = len(m_cls.group(1))
            cls_stack = [c for c in cls_stack if c[0] < depth] + [(depth, m_cls.group(2))]
            i += 1
            continue
        if not m:
            i += 1
            continue
        depth = len(m.group(1))
        cls_stack = [c for c in cls_stack if c[0] < depth]
        name = ".".join([c[1] for c in cls_stack] + [m.group(2)])
        c = {"branch": 0, "loop": 0, "ret": 0, "cond": 0, "match": 0}
        j = i + 1
        while j < len(lines):
            body = lines[j]
            if body.strip() and (len(body) - len(body.lstrip("\t"))) <= depth:
                break
            s = _strip_gd(body).strip()
            if re.match(r"(if|elif)\b", s):
                c["branch"] += 1
                s = re.sub(r"^(if|elif)\b", "", s)
            if re.match(r"(for|while)\b", s):
                c["loop"] += 1
            if re.match(r"return\b", s):
                c["ret"] += 1
            if re.match(r"match\b", s):
                c["match"] += 1
            c["cond"] += len(re.findall(r"\bif\b[^:]*\belse\b", s)) if not re.match(r"else\s*:", s) else 0
            j += 1
        out[name] = c
        i = j
    return out


def _strip_cpp(text: str) -> str:
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
    text = re.sub(r"'(?:[^'\\\n]|\\.)'", "''", text)
    return text


def cpp_functions(text: str) -> dict[str, dict[str, int]]:
    """Function definitions `Ret Class::name(...) {` (any nesting `A::B::name`) -> counts, keyed
    by the qualified name without the outermost class (`Inner.name` for nested classes)."""
    text = _strip_cpp(text)
    out: dict[str, dict[str, int]] = {}
    for m in re.finditer(r"\b((?:[A-Za-z_]\w*::)+)([A-Za-z_]\w*)\s*\(", text):
        quals = [q for q in m.group(1).split("::") if q]
        # find the body: skip the parameter list, then require `{` (after const/noexcept/init list)
        k = m.end() - 1
        depth = 0
        while k < len(text):
            if text[k] == "(":
                depth += 1
            elif text[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        rest = text[k + 1:k + 400]
        mb = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?::[^{;]*)?\{", rest)
        if not mb:
            continue
        start = k + 1 + mb.end() - 1
        depth = 0
        e = start
        while e < len(text):
            if text[e] == "{":
                depth += 1
            elif text[e] == "}":
                depth -= 1
                if depth == 0:
                    break
            e += 1
        body = text[start:e]
        name = ".".join(quals[1:] + [m.group(2)]) if len(quals) > 1 else m.group(2)
        if quals and quals[0] in ("gd", "std", "Fx", "Gte", "Log", "t3"):
            continue
        out[name] = {
            "branch": len(re.findall(r"\bif\s*\(", body)) - len(re.findall(r"\bif\s+constexpr", body)),
            "loop": len(re.findall(r"\b(?:for|while)\s*\(", body)),
            "ret": len(re.findall(r"\breturn\b", body)),
            "cond": body.count("?"),
        }
    return out


def compare(gd: dict, cpp: dict) -> list[str]:
    issues = []
    for name, g in gd.items():
        c = cpp.get(name) or cpp.get(name.split(".")[-1])
        if c is None:
            continue    # inline in the header, or a constructor (_init): check_names covers presence
        kinds = ["loop", "ret"] if g["match"] else ["branch", "loop", "ret", "cond"]
        for k in kinds:
            if g[k] != c[k]:
                issues.append(f"{name}: {k} gd={g[k]} cpp={c[k]}")
    return issues


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--remake", type=Path, default=Path(r"D:\Tekken 3\Tekken3Remake\remake"))
    p.add_argument("--src", type=Path, default=Path(__file__).resolve().parents[1] / "src")
    p.add_argument("paths", nargs="*")
    a = p.parse_args()
    cpps = sorted(a.src.rglob("*.cpp")) if not a.paths else [a.src / (x.removesuffix(".gd") + ".cpp") for x in a.paths]
    bad = 0
    funcs = 0
    for cpp in cpps:
        gd = a.remake / cpp.relative_to(a.src).with_suffix(".gd")
        if not gd.exists() or not cpp.exists():
            continue
        text = cpp.read_text(encoding="utf-8")
        h = cpp.with_suffix(".h")
        if h.exists():
            text = h.read_text(encoding="utf-8") + "\n" + text
        g = gd_functions(gd.read_text(encoding="utf-8"))
        funcs += len(g)
        for issue in compare(g, cpp_functions(text)):
            print(f"{gd.relative_to(a.remake)}:{issue}")
            bad += 1
    print(f"{funcs} functions checked, {bad} mismatches")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
