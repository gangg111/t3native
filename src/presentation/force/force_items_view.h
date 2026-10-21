#pragma once
// remake/presentation/force/force_items_view.gd
// Tekken Force's items (force.ovl FUN_800B4374, `items` in tools/research/force_sim.py): per placed item
// two camera-facing quads above its projected point (y 0x420, under the floor), both SZ / 4-sized
// by 0x15E00 wide, the picture 0x2BC00 high (opaque) and its shadow as high as wide (texture
// shaded by 0x20, half and half) behind it. The game sorts them into the scene's ordering table
// at the point's depth; here they stand in the scene at that depth, so fighters in front of the
// item hide it (in front of the floor under the picture's bottom, which the game draws behind
// everything).
#include <vector>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/texture.h"
#include "gfx3d/nodes.h"

namespace t3 {

class StageData;
class FightSimulation;

class ForceItemsView : public Node3D {
public:
    static constexpr gd::Int WIDTH = 0x15E00;
    static constexpr gd::Int HEIGHT = 0x2BC00;
    static inline const gfx::Color SHADOW_SHADE{0.25f, 0.25f, 0.25f, 0.5f};
    static constexpr double SHADOW_BEHIND = 0.01;   // Godot units: the shadow's quad just behind the picture's

    gd::Arr<gd::Ref<MeshInstance3D>> _meshes;       // per item: picture, shadow
    // Per item and step (previous, current): the picture's screen box in frame-buffer units (left, top,
    // width, height) and the point's depth SZ; width 0 when not placed.
    gd::Arr<std::vector<float>> _boxes;             // Array[PackedFloat32Array] (shared rows)

    void setup(const gd::Ref<StageData>& stage);
    static gd::Ref<StandardMaterial3D> _material(const gfx::TextureRef& texture);
    // After a simulation step: each item's box.
    void apply(const gd::Ref<FightSimulation>& sim);
    // The picture's box (left, top, width, height in frame-buffer units, and the depth SZ) over an
    // item's projected point (screen x, y, SZ): its bottom a picture's height above the point.
    static std::vector<float> picture_box(const gd::PI32& s);
    // Every rendered frame: the quads between the last two steps, in the camera's view.
    void show_between(double weight, const gd::Ref<Camera3D>& camera, const gfx::Rect2& frame);
    // The view depth at which the ray through a window point meets the floor (y 0), or a large depth
    // when it does not.
    static double _floor_depth(const gd::Ref<Camera3D>& camera, gfx::Vec2 at);
    static void _quad(const gd::Ref<MeshInstance3D>& quad, const gd::Ref<Camera3D>& camera, const gfx::Rect2& frame,
                      const gfx::Rect2& box, double depth);
};

}  // namespace t3
