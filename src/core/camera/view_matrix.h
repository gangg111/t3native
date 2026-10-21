#pragma once
// remake/core/camera/view_matrix.gd
// The camera's view matrix as `CameraBuildView` (0x80045F40) builds it from `g_camera`
// (camera.md#camera-state), and the fighters' copy of it (`FUN_80036254`). Rotation row-major
// 4.12 (row 1 scaled by 0x1BD2 / 4096 for the display aspect), translation in world units.
#include "gd/gd.h"

namespace t3 {

class CameraView;
class FightTables;
class JointFrame;

class ViewMatrix {
public:
    static constexpr gd::Int ASPECT = 0x1BD2;
    static constexpr gd::Int SCREEN_OFFSET_X = 192;        // GTE OFX of display mode 0 (FUN_800B0D08)
    static constexpr gd::Int SCREEN_OFFSET_Y = 240;
    static constexpr gd::Int OVERHEAD_DEPTH = 0xA3A;       // FUN_80036254: fighters moved towards the overhead KO camera

    gd::PI32 rot;                       // Fx.identity(), set in the constructor
    gd::PI32 t{0, 0, 0};
    gd::Int h = 500;                    // the GTE projection plane of the frame

    ViewMatrix();

    // CameraBuildView: R = Rx(-pitch) * Ry(-yaw) * Rz(-roll), row 1 scaled, t = -R * eye. The
    // fight camera's roll is always 0, which makes Rz the identity.
    static gd::Ref<ViewMatrix> build(const gd::Ref<CameraView>& view, const gd::Ref<FightTables>& tables);

    // FUN_80036254: the matrix the fighters are drawn with. The overhead KO camera
    // (0x800B08D4) draws them 0xA3A units closer to the eye.
    gd::Ref<ViewMatrix> fighter_view(bool overhead);

    // FighterDrawParts -> FUN_80036CFC: the screen position (x, y) and depth of a joint's origin;
    // its view-space translation is FUN_8003B970's (ApplyMatrixLV plus the view translation).
    gd::PI32 screen_of(const gd::Ref<JointFrame>& joint);
};

}  // namespace t3
