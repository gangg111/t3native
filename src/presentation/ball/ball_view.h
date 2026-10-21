#pragma once
// remake/presentation/ball/ball_view.gd
// Tekken Ball's ball and court (volley.ovl FUN_800B3198 and FUN_800B3DA4, `ball_draw` and
// `court_draw` in tools/research/ball_sim.py): the game's 42-vertex sphere (16 triangles and 32
// quads) in the ball type's colours, lit, or in a flat colour for the flash kinds; squashed
// along an axis on impact, casting the stage light's shadow as the fighters do; the court's
// centre band and side lines, placed between the fighters along z.
//
// Render kinds 0 and 10 go into the 2D ordering table: over the whole scene. Kind 2's streaks
// are ModeHudView's.
//
// gfx3d's StandardMaterial3D has no `vertex_color_is_srgb`, `roughness`, `metallic_specular` or
// `render_priority`: the face colours are given in linear light instead (the same colours as
// Godot's sRGB flag), the specular settings and the overlay's priority are not carried (TODO:
// gfx3d material properties).
#include <array>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/math3d.h"
#include "gfx3d/nodes.h"

namespace t3 {

class GameRam;
class FightSimulation;

class BallView : public Node3D {
public:
    static constexpr gd::Int VERTICES = 0x800B64A8LL;     // 42 SVECTORs
    static constexpr gd::Int NORMALS = 0x800B65F8LL;      // per vertex: SVECTOR
    static constexpr gd::Int TRIANGLES = 0x800B6748LL;    // 16 x 4 vertex indices
    static constexpr gd::Int QUADS = 0x800B6788LL;        // 32 x 4
    static constexpr gd::Int COLOURS = 0x800B6828LL;      // per type (64 bytes): 8 triangle colours, 8 quad colours
    static constexpr gd::Int STYLES = 0x800B6460LL;       // per type (24 bytes): +0xC specular power, +0xD... back colour
    static constexpr gd::Int VERTEX_COUNT = 42;
    static inline const gd::Arr<gd::Int> FACE_COUNTS{16, 32};   // triangles, quads
    static inline const gd::Arr<gd::Int> FLAT_KINDS{0, 1, 2, 5, 8};
    static inline const gd::Arr<gd::Int> OVERLAY_KINDS{0, 10};
    static constexpr gd::Int OVERLAY_PRIORITY = 100;
    static constexpr gd::Int COURT_START = -0x1F40;
    static constexpr gd::Int COURT_STEP = 0x600;
    static constexpr gd::Int COURT_ROWS = 9;
    static constexpr gd::Int BAND_HALF = 0x64;
    static constexpr double LINE_WIDTH = 24.0;

    gd::Ref<GameRam> _ram;
    gd::Ref<MeshInstance3D> _ball;
    gd::Ref<MeshInstance3D> _court;
    gd::Arr<gd::Ref<StandardMaterial3D>> _materials;
    gd::Arr<gd::Ref<StandardMaterial3D>> _overlay_materials;   // kind 10: the same, over the scene
    gd::Ref<StandardMaterial3D> _flat;
    gd::Ref<StandardMaterial3D> _flat_overlay;
    gd::Arr<gd::Ref<ArrayMesh>> _meshes;                      // per ball type
    Transform3D _previous;
    Transform3D _current;
    std::array<float, 2> _court_z{0, 0};                      // PackedFloat32Array([0, 0])
    gd::Int _type = -1;
    bool _snap = true;                                        // the next step's pose is not interpolated into (the first, a return)

    // Creates _ball, _court and _flat (declaration order).
    BallView();
    void setup(const gd::Ref<GameRam>& ram);
    static gd::Ref<StandardMaterial3D> _over_scene(const gd::Ref<StandardMaterial3D>& m);
    // A game SVECTOR (three s16) in RAM, in the game's frame.
    static Vector3 svector(const gd::Ref<GameRam>& ram, gd::Int at);
    // Face `i` of a pass (0: the triangles, 1: the quads): its vertex indices, and its colour in a
    // ball type (the colours cycle by face index & 7).
    static gd::PI32 face_corners(const gd::Ref<GameRam>& ram, gd::Int pass_, gd::Int i);
    static gfx::Color face_colour(const gd::Ref<GameRam>& ram, gd::Int t, gd::Int pass_, gd::Int i);
    // The sphere of a ball type: each face in its colour (colours cycle by face index & 7), with
    // the game's vertex normals.
    gd::Ref<ArrayMesh> _sphere(gd::Int t);
    // The court: the centre band (x +-100) and the side lines (x +- the neutral zone) in nine rows
    // 0x600 apart from z = -8000, brightest in the middle (ball_init's shades), added to the floor.
    gd::Ref<ArrayMesh> _court_mesh(gd::Int zone);
    // After a simulation step.
    void apply(const gd::Ref<FightSimulation>& sim);
    // libgte RotMatrix (4096 units): R = Rx · Ry · Rz in the game's y-down frame.
    static Basis rot_matrix(const gd::PI32& angles);
    // rot_matrix turned into Godot's frame.
    static Basis _psx_rotation(const gd::PI32& angles);
    // Every rendered frame.
    void show_between(double weight);
};

}  // namespace t3
