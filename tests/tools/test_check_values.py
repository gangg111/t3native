"""tools/check_values.py: integer constants and member defaults keep their values in C++.

Input the tool expects: a remake-style tree (`<remake>/core/x.gd`) and a src tree with
`<src>/core/x.h`. Expected values below are worked out by hand from the sources in each test.
"""
import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "check_values.py"
sys.path.insert(0, str(TOOL.parent))
import check_values  # noqa: E402

GD = """class_name Thing
const REACH := 0x215
const LIMIT: int = -3
var count := 7
var name := "x"
class Inner:
\tconst HIDDEN := 9

func f() -> void:
\tvar local := 5
"""


def test_gd_values_class_level_ints_only():
    # source: read off GD by hand (strings, inner classes and locals excluded)
    assert check_values.gd_values(GD) == {"REACH": 0x215, "LIMIT": -3, "count": 7}


def test_cpp_value_literals():
    # source: hand-written C++ initialisers
    assert check_values.cpp_value("static constexpr gd::Int REACH = 0x215;", "REACH") == 0x215
    assert check_values.cpp_value("static constexpr gd::Int MASK = 0xFFFFFFFFLL;", "MASK") == 0xFFFFFFFF
    assert check_values.cpp_value("gd::Int count = 7;", "count") == 7
    assert check_values.cpp_value("gd::Int count{-2};", "count") == -2
    assert check_values.cpp_value("static constexpr gd::Int N = A * 2;", "N") == "expr"
    assert check_values.cpp_value("gd::Int other = 1;", "count") is None


def _run(tmp_path, header: str):
    remake = tmp_path / "remake"
    src = tmp_path / "src"
    (remake / "core").mkdir(parents=True)
    (src / "core").mkdir(parents=True)
    (remake / "core" / "thing.gd").write_text(GD, encoding="utf-8")
    (src / "core" / "thing.h").write_text(header, encoding="utf-8")
    return subprocess.run([sys.executable, str(TOOL), "--remake", str(remake), "--src", str(src)],
                          capture_output=True, text=True)


def test_typo_reported(tmp_path):
    r = _run(tmp_path, "static constexpr gd::Int REACH = 0x251;\nstatic constexpr gd::Int LIMIT = -3;\ngd::Int count = 7;\n")
    # source: 0x215 = 533 vs the typo 0x251 = 593, computed by hand
    assert r.returncode == 1
    assert "REACH: GDScript 533 != C++ 593" in r.stdout
    assert "2 values equal, 1 differ or missing" in r.stdout


def test_equal_values_pass(tmp_path):
    r = _run(tmp_path, "static constexpr gd::Int REACH = 0x215;\nstatic constexpr gd::Int LIMIT = -3;\ngd::Int count = 7;\n")
    # source: all three literals copied unchanged
    assert r.returncode == 0, r.stdout
    assert "3 values equal, 0 differ or missing" in r.stdout
