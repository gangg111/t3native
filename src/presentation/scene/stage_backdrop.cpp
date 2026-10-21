// remake/presentation/scene/stage_backdrop.gd
// A stage without fighters behind a screen: the ranking's turning view.
#include "presentation/scene/stage_backdrop.h"

#include "content/stage_data.h"
#include "core/camera/camera_view.h"
#include "presentation/camera/camera_rig.h"
#include "presentation/stage/stage_lighting.h"
#include "presentation/stage/stage_view.h"
#include "presentation/world_space.h"

namespace t3 {

StageBackdrop::StageBackdrop() {
    stage_view = gd::make<StageView>();
    lighting = gd::make<StageLighting>();
    rig = gd::make<CameraRig>();
}

void StageBackdrop::setup(const gd::Ref<StageData>& stage) {
    number = stage->number;
    stage_view->setup(stage);
    add_child(stage_view);
    lighting->setup(stage, rig->camera);
    stage_view->apply_environment(lighting->environment);
    add_child(lighting);
    rig->coverage = stage_view->coverage();
    add_child(rig);
    stage_view->follow(WorldSpace::point(0, 0, 0));
}

// After a step: the view of this frame (`cut`: no interpolation into it).
void StageBackdrop::show_view(const gd::Ref<CameraView>& view, bool cut) {
    rig->set_view(view, cut);
    stage_view->step();
}

void StageBackdrop::interpolate(float weight, gfx::Vec2 window) {
    rig->place(weight, window);
    stage_view->place_sky(rig->camera, rig->frame_tan);
}

}  // namespace t3
