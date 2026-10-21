#pragma once
// remake/presentation/scene/stage_backdrop.gd
// A stage without fighters behind a screen: the ranking's turning view (ranking.ovl
// FUN_800C3AAC: the camera orbits the stage's centre, FUN_80046458 makes the block's pitch, yaw
// and eye the camera).
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx3d/nodes.h"

namespace t3 {

class StageData;
class StageView;
class StageLighting;
class CameraRig;
class CameraView;

class StageBackdrop : public Node3D {
public:
    gd::Ref<StageView> stage_view;
    gd::Ref<StageLighting> lighting;
    gd::Ref<CameraRig> rig;
    gd::Int number = -1;

    StageBackdrop();
    void setup(const gd::Ref<StageData>& stage);
    // After a step: the view of this frame (`cut`: no interpolation into it).
    void show_view(const gd::Ref<CameraView>& view, bool cut);
    void interpolate(float weight, gfx::Vec2 window);
};

}  // namespace t3
