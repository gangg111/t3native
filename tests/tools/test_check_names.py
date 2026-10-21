"""tools/check_names.py: which GDScript class-level names a C++ translation never mentions.

Input the tool expects: a remake-style tree (`<remake>/core/x.gd`) and a src tree with the
translation (`<src>/core/x.h`, optionally `.cpp`). Every expected value below was worked out
by hand from the tiny sources written in each test.
"""
import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "check_names.py"
sys.path.insert(0, str(TOOL.parent))
import check_names  # noqa: E402

GD = """class_name Thing
extends RefCounted
## doc
const LIMIT := 5
enum Kind { RED, GREEN = 4 }
var count := 0
static var shared := 1

class Inner:
\tvar depth := 2
\tfunc inner_step() -> void:
\t\tvar local_in_inner := 3

func _init() -> void:
\tvar local_a := 1

func step(x: int) -> int:
\tvar local_b := x
\treturn local_b
"""


def test_declared_takes_class_level_names_only():
    # source: read off GD by hand; locals (local_a, local_b, local_in_inner) and _init excluded
    assert check_names.declared(GD) == [
        "LIMIT", "Kind", "RED", "GREEN", "count", "shared", "Inner", "depth", "inner_step", "step",
    ]


def test_missing_names_reported(tmp_path):
    remake = tmp_path / "remake"
    src = tmp_path / "src"
    (remake / "core").mkdir(parents=True)
    (src / "core").mkdir(parents=True)
    (remake / "core" / "thing.gd").write_text(GD, encoding="utf-8")
    # The translation forgets GREEN and inner_step, and renames shared.
    (src / "core" / "thing.h").write_text(
        "class Thing { static constexpr int LIMIT = 5; enum Kind { RED }; int count; static int shared_v;\n"
        " struct Inner { int depth; }; int step(int x); };\n",
        encoding="utf-8",
    )
    r = subprocess.run([sys.executable, str(TOOL), "--remake", str(remake), "--src", str(src)],
                       capture_output=True, text=True)
    # source: hand diff of the declared list against the header's words
    assert r.returncode == 1
    assert "GREEN, shared, inner_step" in r.stdout
    assert "1 files checked, 1 with missing names" in r.stdout


def test_complete_translation_passes(tmp_path):
    remake = tmp_path / "remake"
    src = tmp_path / "src"
    (remake / "core").mkdir(parents=True)
    (src / "core").mkdir(parents=True)
    (remake / "core" / "thing.gd").write_text(GD, encoding="utf-8")
    (src / "core" / "thing.h").write_text(
        "LIMIT Kind RED GREEN count shared Inner depth inner_step step", encoding="utf-8")
    r = subprocess.run([sys.executable, str(TOOL), "--remake", str(remake), "--src", str(src)],
                       capture_output=True, text=True)
    # source: every declared name is present by construction
    assert r.returncode == 0, r.stdout
    assert "1 files checked, 0 with missing names" in r.stdout
