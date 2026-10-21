// remake/presentation/scene/enbu_view.gd
// Draws the attract demonstration's performance (see the header).
#include "presentation/scene/enbu_view.h"

#include "content/character_model.h"
#include "content/effect_data.h"
#include "content/stage_data.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_view.h"
#include "core/fighter/fighter_skeleton.h"
#include "core/fighter/fighter_state.h"
#include "core/scripted/enbu_performance.h"
#include "core/scripted/scripted_fighter.h"
#include "presentation/camera/camera_rig.h"
#include "presentation/effects/effects_view.h"
#include "presentation/fighter/fighter_view.h"
#include "presentation/stage/stage_lighting.h"
#include "presentation/stage/stage_view.h"

namespace t3 {

EnbuView::EnbuView() {
    stage_view = gd::make<StageView>();
    lighting = gd::make<StageLighting>();
    effects = gd::make<EffectsView>();
    rig = gd::make<CameraRig>();
}

void EnbuView::setup(const gd::Ref<StageData>& stage, const gd::Ref<EffectData>& effect_data) {
    stage_view->setup(stage);
    add_child(stage_view);
    lighting->setup(stage, rig->camera);
    stage_view->apply_environment(lighting->environment);
    add_child(lighting);
    // StageLighting.fighter_parameters as the Dictionary FighterView.set_lighting takes.
    StageLighting::FighterParameters p = StageLighting::fighter_parameters(stage);
    _fighter_lighting.set("base_colour", p.base_colour);
    _fighter_lighting.set("back_colour", p.back_colour);
    _fighter_lighting.set("light_colour", p.light_colour);
    _fighter_lighting.set("fill_colour", p.fill_colour);
    effects->setup(effect_data);
    add_child(effects);
    rig->coverage = stage_view->coverage();
    add_child(rig);
}

// Binds a demonstration's models and builds, hidden, the view of every costume each fighter
// wears in it.
void EnbuView::load_demo(const gd::Ref<EnbuData::Demo>& demo, const gd::Dict<gd::Int, gd::Ref<CharacterModel>>& costume_models) {
    models = costume_models;
    for (const gd::Dict<gd::Int, gd::Ref<FighterView>>& per_fighter : views) {
        for (const auto& kv : per_fighter) {
            // queue_free()
            remove_child(kv.second.get());
        }
        per_fighter.clear();
    }
    _shown = gd::Arr<gd::Ref<FighterView>>{nullptr, nullptr};
    effects->clear();
    gd::Int n = views.size();
    for (gd::Int fighter = 0; fighter < n; ++fighter) {
        for (gd::Int slot : demo->costume_slots(fighter)) {
            _view(fighter, slot);
        }
    }
}

// After a simulation step of the performance.
void EnbuView::apply(const gd::Ref<EnbuPerformance>& perf) {
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<ScriptedFighter> sf = perf->fighters[i];
        gd::Ref<FighterView> view = _view(i, sf->model_costume);
        if (_shown[i] != nullptr && _shown[i] != view) {
            _shown[i]->visible = false;
        }
        bool appearing = _shown[i] != view || !view->visible;
        _shown[i] = view;
        view->visible = sf->drawn && perf->drawn;
        if (view->visible) {
            view->set_joints(sf->skeleton->joints, appearing);
            view->set_hidden_joints(sf->stick_shown ? 0 : (1LL << FighterView::STICK_JOINT));
            view->set_hands(sf->fighter->hands);
        }
    }
    effects->step(perf->events);
    stage_view->set_backdrop_turn(perf->backdrop->angle);
    stage_view->step();
    if (perf->camera_drawn != nullptr) {
        rig->set_view(perf->camera_drawn, perf->camera_drawn->cut);
    }
}

// Every rendered frame: the pose and camera between the last two steps.
void EnbuView::interpolate(double weight, gfx::Vec2 window) {
    for (const gd::Ref<FighterView>& view : _shown) {
        if (view != nullptr && view->visible) {
            view->show_between(weight);
        }
    }
    rig->place(weight, window);
    stage_view->place_sky(rig->camera, rig->frame_tan);
}

gd::Ref<FighterView> EnbuView::_view(gd::Int fighter, gd::Int slot) {
    gd::Dict<gd::Int, gd::Ref<FighterView>> per_fighter = views[fighter];
    if (!per_fighter.has(slot)) {
        gd::Ref<FighterView> view = gd::make<FighterView>();
        view->setup(models.at(slot));
        view->set_lighting(_fighter_lighting);
        view->visible = false;
        add_child(view);
        per_fighter.set(slot, view);
    }
    return per_fighter.at(slot);
}

}  // namespace t3
