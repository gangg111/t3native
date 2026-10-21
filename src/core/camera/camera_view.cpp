// remake/core/camera/camera_view.gd
// The active camera as `g_camera` holds it (camera.md#camera-state): pitch and yaw in 4096
// units, the eye position in world units (y down), and the projection distance H in pixels
// of the 368 x 480 display (0x800A919C).
#include "core/camera/camera_view.h"

namespace t3 {

CameraView::CameraView(gd::Int p, gd::Int w, gd::Int px, gd::Int py, gd::Int pz, gd::Int projection) {
    pitch = p;
    yaw = w;
    x = px;
    y = py;
    z = pz;
    h = projection;
}

// A copy (views kept for interpolation must not change under the holder).
gd::Ref<CameraView> CameraView::copy() {
    gd::Ref<CameraView> c = gd::make<CameraView>(pitch, yaw, x, y, z, h);
    c->roll = roll;
    c->cut = cut;
    return c;
}

bool CameraView::equals(const gd::Ref<CameraView>& other) {
    return pitch == other->pitch && yaw == other->yaw && roll == other->roll && x == other->x && y == other->y
        && z == other->z && h == other->h;
}

std::string CameraView::_to_string() {
    return "(pitch " + gd::str(pitch) + ", yaw " + gd::str(yaw) + ", " + gd::str(x) + ", " + gd::str(y) + ", "
        + gd::str(z) + ", H " + gd::str(h) + ")";
}

}  // namespace t3
