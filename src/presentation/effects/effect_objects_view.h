#pragma once
// remake/presentation/effects/effect_objects_view.gd
// Draws the effect object pool (FightState.effects, effects.md#effect-objects) as the game does:
// each object a camera-facing quad of its model (the executable's small models), textured from the
// object page (VRAM (960, 256), 4-bit) through its CLUT (the fixed ones and the fade ramps of rows
// 509 and 510) at the frame of its phase's UV table, or its model's own 16 x 16 sprite. One
// MultiMesh draws the additive page, one the 50 % page. Tekken Ball's charged ball (UV table
// BALL_CHARACTER) shows the hitter's character flipbook instead: the game points its quad at the
// flipbook's VRAM upload; the remake draws the converted flipbook's frames in turn on the ball's
// model, additively.
#include "content/effect_data.h"
#include "core/effects/effect_objects.h"
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/texture.h"
#include "gfx3d/nodes.h"

namespace t3 {

class EffectPool;

class EffectObjectsView : public Node3D {
public:
    static constexpr gd::Int MAX_OBJECTS = EffectObjects::CAPACITY;
    // preload("res://presentation/effects/effect_object.gdshader") and effect_object_half.gdshader:
    // functions (one object each, loaded on first use).
    static const gd::Ref<Shader>& ADD_SHADER();
    static const gd::Ref<Shader>& HALF_SHADER();

    gd::Ref<MultiMeshInstance3D> additive;
    gd::Ref<MultiMeshInstance3D> half;
    gd::Ref<MeshInstance3D> ball_glow;
    gd::Arr<gfx::Color> quad_corners;                   // by EffectObjects.Model: corners 0 and 3 (corners)
    gd::Dict<gd::Int, gd::PI32> sprites;                // model -> (u, v, size)
    gd::Arr<gd::Arr<gd::PI32>> uv_frames;               // by EffectObjects.UvTable - 1: (u, v, size) per frame
    gd::Dict<gd::Int, gd::Int> clut_rows;               // CLUT id -> palette row
    gd::Arr<gd::Ref<EffectData::Flipbook>> costumes;    // the players' character flipbooks
    gd::PI32 ball_glow_frames;                          // per glow step: the flipbook frame (-1 none)
    gd::Ref<StandardMaterial3D> _ball_material;

    // Creates additive, half, ball_glow and _ball_material (declaration order).
    EffectObjectsView();
    void setup(const gd::Ref<EffectData>& data);
    void _prepare(const gd::Ref<MultiMeshInstance3D>& node, const gd::Ref<Shader>& shader, const gfx::TextureRef& page,
                  const gfx::TextureRef& palettes);
    // The players' character flipbooks (the charged ball's glow).
    void set_costumes(const gd::Arr<gd::Ref<EffectData::Flipbook>>& books);
    void clear();
    // The sprite of an object this frame: (u0, v0, frame size) on the object page, or empty.
    gd::PI32 sprite_of(const gd::Ref<EffectObjects::Obj>& o);
    gd::PI32 _table_frame(gd::Int table, gd::Int frame);
    // The pool after a simulation step.
    void show_pool(const gd::Ref<EffectPool>& pool);
    // A model's corners 0 and 3 as (x0, y0, x1, y1) in metres.
    gfx::Color corners(gd::Int model);
    // The charged ball's glow on the ball's quad, additively: step `uv` of the glow's UV table points
    // the quad at a frame of the hitter's flipbook upload (FUN_800743EC), converted as that frame of
    // the character flipbook (`ball_glow_frames`).
    void _show_ball_glow(const gd::Ref<EffectObjects::Obj>& o);
};

}  // namespace t3
