"""tools/gen_accessors.py: name -> value accessor tables replacing GDScript `obj.get(name)`.

Input the tool expects: a .gd source, the C++ header of its translation, the class name. Expected
outputs below were written by hand from the tiny sources in each test.
"""
import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "gen_accessors.py"
sys.path.insert(0, str(TOOL.parent))
import gen_accessors  # noqa: E402

GD = """class_name Rec
const K := 1
var health := 0
var move: MoveRow:
\tget:
\t\treturn pose_move
var pose_move: MoveRow
class Inner:
\tvar hidden := 1
func f() -> void:
\tvar local := 2
"""
HEADER = "class Rec { gd::Int health = 0; gd::Ref<MoveRow> pose_move; gd::Ref<MoveRow> get_move() const; };"


def test_class_vars():
    # source: read off GD by hand: class-level vars only, in order
    assert gen_accessors.class_vars(GD) == ["health", "move", "pose_move"]


def test_accessor_uses_getter_for_properties():
    # source: HEADER declares get_move(), nothing else
    assert gen_accessors.accessor("move", HEADER) == "o.get_move()"
    assert gen_accessors.accessor("health", HEADER) == "o.health"


def test_generated_rows(tmp_path):
    gd = tmp_path / "rec.gd"
    h = tmp_path / "rec.h"
    out = tmp_path / "rec_fields.cpp"
    gd.write_text(GD, encoding="utf-8")
    h.write_text(HEADER, encoding="utf-8")
    r = subprocess.run([sys.executable, str(TOOL), "--gd", str(gd), "--header", str(h), "--cls", "Rec",
                        "--include", "core/rec.h", "--func", "rec_fields", "--out", str(out)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    text = out.read_text(encoding="utf-8")
    # source: hand-written expected rows for the three vars
    assert '{"health", [](const t3::Rec& o) { return field_value(o.health); }},' in text
    assert '{"move", [](const t3::Rec& o) { return field_value(o.get_move()); }},' in text
    assert '{"pose_move", [](const t3::Rec& o) { return field_value(o.pose_move); }},' in text
    assert "const FieldTable<t3::Rec>& rec_fields()" in text


def test_missing_name_fails(tmp_path):
    gd = tmp_path / "rec.gd"
    h = tmp_path / "rec.h"
    gd.write_text(GD, encoding="utf-8")
    h.write_text("class Rec { gd::Int health = 0; };", encoding="utf-8")
    r = subprocess.run([sys.executable, str(TOOL), "--gd", str(gd), "--header", str(h), "--cls", "Rec",
                        "--include", "x.h", "--func", "f", "--out", str(tmp_path / "o.cpp")],
                       capture_output=True, text=True)
    # source: move and pose_move are absent from this header
    assert r.returncode == 1
    assert "not in the header: move, pose_move" in r.stdout
