#pragma once
// remake/presentation/stage/tiled_panorama.gd
// The screen-space backdrops of the tile-map stages, drawn behind the 3D scene (a canvas layer
// the environment shows as its background):
//
// - Tekken Ball (volley.ovl FUN_800B5F0C, `background_draw` in tools/research/ball_sim.py): 32-pixel
//   tiles scrolling with the camera's view-space position and yaw, the map's top edge on the
//   projected horizon, a sky-coloured tile above it;
// - Tekken Force (force.ovl FUN_800B4DF8, `background_draw` in tools/research/force_sim.py): 64-pixel
//   tiles scrolling with the camera's view-space x, climbing every third column on the fourth
//   level (with black gates at the map's first column), a black tile over the ground there and
//   only that tile in the Doctor B. level.
//
// Positions are the game's 384 x 480 frame-buffer units (the 368 shown columns start at x = 8),
// laid into the scene's 4:3 frame and continued sideways over a wider window.
//
// A Control in the remake, drawn into a SubViewport (FightView.setup_modes): here a 2D drawable,
// `draw(canvas, size)`, which the app renders into a texture behind the 3D scene.
#include <array>

#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/texture.h"
#include "gfx3d/math3d.h"

namespace t3 {

class StageData;
class FightTables;
class FightSimulation;
class ViewMatrix;

class TiledPanorama {
public:
    static inline const gfx::Color BALL_SKY = gfx::Color::c8(0x21, 0x3A, 0x94);
    static constexpr gd::Int BALL_SCROLL_DIV = 0x60;
    static constexpr gd::Int BALL_REACH = 0x1A00;        // the horizon point ahead of the camera
    static constexpr gd::Int BALL_HORIZON_Y = 0x100;
    static constexpr gd::Int BALL_FIRST = 2;             // map columns left of the screen: 2, 16 at the result, 10 later
    static constexpr gd::Int FORCE_SCROLL_DIV = 0x14;
    static constexpr gd::Int FORCE_HORIZON_Z = 0x9C4;
    static constexpr gd::Int FORCE_SLOPE = 0xD8;         // the fourth level's climb (4096 units)
    static constexpr gd::Int FORCE_SLOPE_LEVEL = 3;
    static inline const gfx::Rect2 FORCE_GROUND{0, 0x14, 0x110, 0x64};
    static inline const gfx::Vec2 FORCE_GATE{0x40, 0x20};
    static constexpr gd::Int SCREEN_TOP = 0x14;          // FUN_80029860's display rectangle
    static constexpr gd::Int SCREEN_HEIGHT = 0x1C0;

    // Control's own members.
    bool visible = true;
    gfx::Vec2 size;

    gfx::TextureRef picture;
    gd::Int tile = 32;
    gd::Int map_width = 1;                     // cells
    gd::Int map_height = 1;
    bool ball = true;
    gd::Int level = 0;                         // Tekken Force: the level (stage - 15)
    gfx::Rect2 frame_rect{0, 0, 1, 1};         // the 4:3 frame in the window (normalised)
    bool shown = true;
    gd::Ref<FightTables> _tables;
    gd::Int _yaw_ref = 0;
    gd::Int _last_kind = -1;
    std::array<float, 2> _scroll{0, 0};        // PackedFloat32Array: previous and current step
    std::array<float, 2> _top{0, 0};
    bool _started = false;                     // the first step starts at its own scroll, not at 0
    gd::Int _shift = 0;                        // extra map columns (Tekken Ball's result and replays)
    bool _tile_only = false;
    double _weight = 1.0;
    // The line past which the map covers the floor (StageView.set_floor_edge): the ground under the
    // map's bottom edge, across the view.
    Vector3 floor_edge{0, 0, 1.0e9f};

    void setup(const gd::Ref<StageData>& stage, const gd::Ref<FightTables>& tables);
    // After a simulation step: the scroll and the map's top edge of this frame.
    void step(const gd::Ref<FightSimulation>& sim);
    gd::Int _screen_y(const gd::Ref<ViewMatrix>& m, const gd::PI32& point);
    // Every rendered frame: between the last two steps.
    void show_between(double weight, const gfx::Rect2& frame);
    gfx::Vec2 _point(double x, double y) const;
    gfx::Rect2 _rect(double x, double y, double w, double h) const;
    // The window's left and right edges in frame-buffer units.
    gfx::Vec2 _extent() const;
    // The Control's draw: sets `size` (the window) and draws with `canvas` (_draw).
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw();
    void _draw_ball(double scroll, double top, gfx::Vec2 span);
    void _draw_force(double scroll, double top, gfx::Vec2 span);
    // Straight rows: screen x shows map pixel origin + x.
    void _draw_rows(double origin, double top, gfx::Vec2 span);
    void _draw_cell(gd::Int column, gd::Int row, double x, double y);

    gfx::Canvas* _canvas = nullptr;   // the canvas of the draw in progress (CanvasItem's draw_* target)
};

}  // namespace t3
