#pragma once
// remake/presentation/scene/fight_view.gd
// Draws a fight: the stage with its light rig, the fighters (costume models, hand variants,
// eyes; Tekken Force's third record too), the flipbook effects and effect objects, Tekken
// Ball's ball and court, Tekken Force's items, and the fight camera, interpolated between
// simulation steps.
//
// The tile-map stages' backdrop is a TiledPanorama drawn into a SubViewport in the remake: here
// a gfx::RenderTarget the app draws it into every frame (draw_backdrop) before the 3D scene.
#include <memory>
#include <string>

#include "content/character_model.h"
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/render_target.h"
#include "gfx3d/nodes.h"
#include "presentation/fighter/arcade_wind.h"
#include "presentation/fighter/blinker.h"

namespace t3 {

class StageData;
class EffectData;
class FightTables;
class GameRam;
class FightSimulation;
class CameraView;
class StageView;
class StageLighting;
class EffectsView;
class CameraRig;
class FighterView;
class TiledPanorama;
class BallView;
class ForceItemsView;

class FightView : public Node3D {
public:
    static constexpr gd::Int CLEAR_STAGE = 11;
    static inline const gfx::Color CLEAR_STAGE_COLOUR = gfx::Color::c8(72, 104, 200);
    static constexpr double BACKDROP_DEPTH = 0.95;       // of the far plane: behind everything else
    // A camera that moves this far (game units) or turns this much (4096 units) between two
    // steps is a cut: the rig does not interpolate across it.
    static constexpr gd::Int CUT_DISTANCE = 1500;
    static constexpr gd::Int CUT_ANGLE = 0x200;
    // The spotlight floors (stages.md#floor, FUN_80049AE0): K per kind and the brightness limits.
    static inline const gd::Arr<gd::Int> SPOT_K{0, 253755392, 800000000, 512000000, 414720000};
    static constexpr gd::Int SPOT_LIMIT = 254;
    static constexpr gd::Int SPOT_LIMIT_MIDPOINT = 192;
    static constexpr gd::Int SPOT_LIMIT_DARK = 128;      // kind 2 on stages 13 and 19 with True Ogre
    static inline const gd::Arr<gd::Int> DARK_STAGES{13, 19};
    static constexpr gd::Int FORCE_CAMERA_LEVEL = 4;     // Tekken Force's level whose floor is lit from the camera
    static constexpr gd::Int FORCE_CAMERA_Z = -2000;

    gd::Ref<StageView> stage_view;
    gd::Ref<StageLighting> lighting;
    gd::Ref<EffectsView> effects;
    gd::Ref<CameraRig> rig;
    gd::Arr<gd::Ref<FighterView>> fighters;
    gfx::Color clear_colour = gfx::Color::black();
    gd::Ref<TiledPanorama> tiled;                         // the tile-map stages' screen-space backdrop
    std::shared_ptr<gfx::RenderTarget> _backdrop_viewport;   // SubViewport: made at the window's size by draw_backdrop
    gd::Ref<MeshInstance3D> _backdrop;
    gd::Ref<StandardMaterial3D> _backdrop_material;       // native: the material whose texture is the viewport's
    gd::Ref<BallView> ball;                               // Tekken Ball's ball and court
    gd::Ref<ForceItemsView> items;                        // Tekken Force's items
    gd::Ref<CameraView> _last_view;
    bool _overhead = false;                               // the overhead KO camera: fighters drawn closer to it
    gd::Int _floor_kind = 0;                              // the stage's floor kind (True Ogre turns 0 into 2)
    gd::Int _stage_number = 0;
    gd::Int _ambient_level = 0;                           // the light record's ambient word (StageData.ambient_level)
    bool _true_ogre = false;
    ArcadeWind _wind;                                     // the helicopter stage's wind on the arcade models' attachments
    RandomNumberGenerator _wind_rng;                      // the wind's direction, seeded by the match
    gd::Dict<std::string, ShaderValue> _fighter_lighting;   // StageLighting.fighter_parameters of the fight's light record
    gd::PI32 _costume_slots;
    // The fighters' mesh arrays, for this fight (FighterView.array_cache).
    gd::Dict<gd::Ref<CharacterModel::Surface>, MeshArrays> _array_cache;
    gfx::Vec2 _viewport_size{1, 1};                       // the SubViewport's size (the window's)

    // Creates stage_view, lighting, effects and rig (declaration order).
    FightView();
    // `light_stage`: the stage whose light record lights the fight, when not its own (Tekken Force's
    // Doctor B. level).
    void setup(const gd::Ref<StageData>& stage, const gd::Ref<EffectData>& effect_data,
               const gd::Arr<gd::Ref<CharacterModel>>& models, const gd::PI32& costume_slots, const gd::PI32& char_ids,
               const gd::Ref<StageData>& light_stage = nullptr);
    gd::Ref<FighterView> _new_fighter(const gd::Ref<CharacterModel>& model);
    // A record's model swapped in during the fight (Tekken Force's enemies and boss, the Ogre scene):
    // only its view is built again; it shows from the next step.
    void replace_fighter(gd::Int i, const gd::Ref<CharacterModel>& model, gd::Int costume_slot);
    // Tekken Ball's and Tekken Force's parts: the tile-map backdrop (drawn into a viewport and
    // shown on a quad just before the camera's far plane, behind everything), the ball and the items.
    void setup_modes(const gd::Ref<StageData>& stage, const gd::Ref<FightTables>& tables, const gd::Ref<GameRam>& volley);
    // The backdrop quad over the whole view at BACKDROP_DEPTH of the far plane.
    void _place_backdrop(gfx::Vec2 window);
    // After a simulation step.
    void apply(const gd::Ref<FightSimulation>& sim);
    // FloorDrawGrid: where the floor's distance shading starts, the camera's horizontal distance to
    // the fighters' midpoint (Tekken Force: to the line z = 0).
    static double _floor_distance(const gd::Ref<FightSimulation>& sim);
    // FloorSetup's kind (Tekken Force's camera-lit level forces 4; True Ogre turns a plain floor into
    // 2) and FUN_80049AE0's light points of this step.
    void _spotlight(const gd::Ref<FightSimulation>& sim);
    // Every rendered frame: the poses and camera between the last two steps.
    void interpolate(double weight, gfx::Vec2 window);
    bool _is_cut(const gd::Ref<CameraView>& view);

    // Native: the SubViewport's own update (UPDATE_ALWAYS), which the app runs every frame before
    // the 3D scene: the tile-map backdrop drawn with `canvas` into _backdrop_viewport at the
    // window's size (`interpolate` sets it). Leaves the window's framebuffer bound.
    void draw_backdrop(gfx::Canvas& canvas);
    // Native: the GL context was lost; the viewport is made again on the next draw_backdrop.
    void graphics_lost();
};

}  // namespace t3
