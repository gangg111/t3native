#pragma once
// remake/core/camera/camera_view.gd
// The active camera as `g_camera` holds it (camera.md#camera-state): pitch and yaw in 4096
// units, the eye position in world units (y down), and the projection distance H in pixels
// of the 368 x 480 display (0x800A919C).
#include <string>

#include "gd/gd.h"

namespace t3 {

class CameraView {
public:
    gd::Int pitch = 0;
    gd::Int yaw = 0;
    gd::Int roll = 0;                      // +8: Tekken Force's last level rolls the camera
    gd::Int x = 0;
    gd::Int y = 0;
    gd::Int z = 0;
    gd::Int h = 0;
    // The first view of a new shot (a reel stream starts): presentation must not interpolate
    // into it. Not part of the game state (`equals` ignores it).
    bool cut = false;

    explicit CameraView(gd::Int p = 0, gd::Int w = 0, gd::Int px = 0, gd::Int py = 0, gd::Int pz = 0,
                        gd::Int projection = 0);

    // A copy (views kept for interpolation must not change under the holder).
    gd::Ref<CameraView> copy();

    bool equals(const gd::Ref<CameraView>& other);

    std::string _to_string();
};

}  // namespace t3
