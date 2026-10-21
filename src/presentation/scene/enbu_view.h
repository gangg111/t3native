#pragma once
// remake/presentation/scene/enbu_view.gd
// Draws the attract demonstration's performance: stage 4 with its light rig, the two fighters
// in their current costumes (a view per costume slot, shown only on the frames the game draws
// the fighter), the frame effects and the camera reel, interpolated between simulation steps.
//
// The original draws a flat grey floor in the demonstration; the stage's normal floor is used
// here (remake-plan.md#m1-the-attract-sequence-on-the-engine).
#include <string>

#include "content/enbu_data.h"
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/nodes.h"

namespace t3 {

class StageData;
class EffectData;
class CharacterModel;
class EnbuPerformance;
class StageView;
class StageLighting;
class EffectsView;
class CameraRig;
class FighterView;

class EnbuView : public Node3D {
public:
    gd::Ref<StageView> stage_view;
    gd::Ref<StageLighting> lighting;
    gd::Ref<EffectsView> effects;
    gd::Ref<CameraRig> rig;
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> models;                  // costume slot -> CharacterModel
    gd::Arr<gd::Dict<gd::Int, gd::Ref<FighterView>>> views{{}, {}};    // per fighter: costume slot -> FighterView
    gd::Arr<gd::Ref<FighterView>> _shown{nullptr, nullptr};
    gd::Dict<std::string, ShaderValue> _fighter_lighting;                // the stage's light rig for the fighters

    // Creates stage_view, lighting, effects and rig (declaration order).
    EnbuView();
    void setup(const gd::Ref<StageData>& stage, const gd::Ref<EffectData>& effect_data);
    // Binds a demonstration's models and builds, hidden, the view of every costume each fighter
    // wears in it, so that no mesh is built while the performance runs.
    void load_demo(const gd::Ref<EnbuData::Demo>& demo, const gd::Dict<gd::Int, gd::Ref<CharacterModel>>& costume_models);
    // After a simulation step of the performance.
    void apply(const gd::Ref<EnbuPerformance>& perf);
    // Every rendered frame: the pose and camera between the last two steps.
    void interpolate(double weight, gfx::Vec2 window);
    gd::Ref<FighterView> _view(gd::Int fighter, gd::Int slot);
};

}  // namespace t3
