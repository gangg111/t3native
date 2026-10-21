"""tools/check_structure.py: per-function control-flow counts, GDScript against C++.

Input the tool expects: a .gd source and the .h/.cpp of its translation. Every expected count
below was counted by hand in the tiny sources of the test.
"""
import sys
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "check_structure.py"
sys.path.insert(0, str(TOOL.parent))
import check_structure as cs  # noqa: E402

GD = """class_name Thing
func step(x: int) -> int:
\tif x > 0:
\t\treturn 1
\telif x < -5:
\t\tx = 2 if x == -6 else 3   # a ternary
\tfor i in 3:
\t\tx += i
\treturn x

class Inner:
\tfunc go() -> void:
\t\twhile true:
\t\t\treturn
"""

CPP_OK = """
gd::Int Thing::step(gd::Int x) {
    if (x > 0) {
        return 1;
    } else if (x < -5) {
        x = x == -6 ? 2 : 3;   // "?" in a comment does not count
    }
    for (gd::Int i = 0; i < 3; ++i) x += i;
    return x;
}
void Thing::Inner::go() {
    while (true) {
        return;
    }
}
"""


def test_gd_counts():
    # source: counted by hand in GD: step has if+elif=2 branches, 1 for, 2 returns, 1 ternary
    g = cs.gd_functions(GD)
    assert g["step"] == {"branch": 2, "loop": 1, "ret": 2, "cond": 1, "match": 0}
    assert g["Inner.go"] == {"branch": 0, "loop": 1, "ret": 1, "cond": 0, "match": 0}


def test_cpp_counts():
    # source: counted by hand in CPP_OK (else if counts as one if; the comment's ? is stripped)
    c = cs.cpp_functions(CPP_OK)
    assert c["step"] == {"branch": 2, "loop": 1, "ret": 2, "cond": 1}
    assert c["Inner.go"] == {"branch": 0, "loop": 1, "ret": 1, "cond": 0}


def test_faithful_translation_has_no_issues():
    # source: CPP_OK was written as a line-by-line translation of GD
    assert cs.compare(cs.gd_functions(GD), cs.cpp_functions(CPP_OK)) == []


def test_dropped_branch_is_reported():
    dropped = CPP_OK.replace("} else if (x < -5) {\n        x = x == -6 ? 2 : 3;   // \"?\" in a comment does not count\n    }", "}")
    # source: removing the elif branch removes 1 branch and 1 ternary, counted by hand
    assert cs.compare(cs.gd_functions(GD), cs.cpp_functions(dropped)) == [
        "step: branch gd=2 cpp=1", "step: cond gd=1 cpp=0"]
