#pragma once
// remake/presentation/camera/camera_rig.gd
// Shows the simulation's camera (CameraView) with a Godot camera, for any window shape
// (remake-plan.md#aspect-ratios).
//
// The game's view matrix is `Rx(-pitch)·Ry(-yaw)` (CameraBuildView) looking along +z with y
// down, and the projection distance H is in pixels of the 368 x 480 display, whose rows are
// scaled by 0x1BD2/4096 for the 4:3 picture: the 4:3 frame spans +-184/H horizontally and
// +-138/H vertically in view-plane units. That frame is always fully visible. A wider window
// extends it sideways (up to 21:9); a taller one extends it upwards as far as the stage's
// backdrop reaches (its coverage data) and downwards over the endless floor for the rest, so
// the 4:3 frame sits higher in a tall window. What cannot be extended is framed.
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/math3d.h"
#include "gfx3d/nodes.h"

namespace t3 {

class CameraView;

class CameraRig : public Node3D {
public:
    static constexpr double HALF_WIDTH = 184.0;            // half the 368-pixel display width
    static constexpr double HALF_HEIGHT = 138.0;           // 240 display lines / 1.739
    static constexpr double MAX_ASPECT = 21.0 / 9.0;
    static constexpr double NEAR = 0.05;
    static constexpr double FAR = 400.0;

    gd::Ref<Camera3D> camera;
    gd::PI32 coverage;                                     // per direction: highest covered elevation (degrees)
    // The part of the window that shows the scene (normalised); the rest is framed.
    gfx::Rect2 visible_rect{0, 0, 1, 1};
    // Where the original 4:3 frame lies in the window (normalised): the HUD's frame.
    gfx::Rect2 frame_rect{0, 0, 1, 1};
    // The 4:3 frame's half extent in view-plane units (+-184/H, +-138/H): the arcade sky's screen.
    gfx::Vec2 frame_tan{static_cast<float>(HALF_WIDTH / 500.0), static_cast<float>(HALF_HEIGHT / 500.0)};
    gd::Ref<CameraView> _previous;
    gd::Ref<CameraView> _current;

    // Creates `camera`, then runs _ready (Godot runs it when the rig enters the tree; nothing here
    // depends on the tree).
    CameraRig();
    void _ready();
    // A new simulation step's camera; `snap` drops interpolation (cuts).
    void set_view(const gd::Ref<CameraView>& view, bool snap = false);
    // Places the camera between the last two steps (`weight` 0 -> previous, 1 -> current).
    void place(double weight, gfx::Vec2 window);
    // Camera-to-world basis in Godot space: S·M^T·S with M = Rx(-pitch)·Ry(-yaw), S = diag(1, -1, -1).
    static Basis view_basis(double pitch, double yaw);
    void _project(double h, double pitch, double yaw, gfx::Vec2 window);
    // The coverage direction the camera faces: coverage direction k points along the game's
    // (cos θ, -sin θ) in x and z with θ = k·360°/count (tools/remake_import/stage.py), so θ comes
    // from the view's forward vector (Godot z is the game's -z).
    static gd::Int coverage_index(double yaw, gd::Int count);
    // θ of the direction the camera faces, 0 to 2π.
    static double coverage_angle(double yaw);
    // The covered elevation (degrees) at direction position f (θ·count/2π), linear between the
    // stage's directions.
    double _coverage_at(double f);
    // How far above the view centre (in view-plane units) the backdrop still covers, at least the
    // 4:3 frame: the lowest covered elevation across the view's width (+-atan(side_tan) around the
    // faced direction), so no corner shows past the backdrop and turning the camera changes it
    // smoothly. Pitch is positive looking down; the elevations come from the stage data.
    double _covered_tan(double pitch, double yaw, double side_tan, double frame_tan);
};

}  // namespace t3
