// remake/core/camera/view_matrix.gd
// The camera's view matrix as `CameraBuildView` (0x80045F40) builds it from `g_camera`
// (camera.md#camera-state), and the fighters' copy of it (`FUN_80036254`). Rotation row-major
// 4.12 (row 1 scaled by 0x1BD2 / 4096 for the display aspect), translation in world units.
#include "core/camera/view_matrix.h"

#include "content/fight_tables.h"
#include "core/camera/camera_view.h"
#include "core/fight/fight_math.h"
#include "core/fighter/joint_frame.h"
#include "core/math/fx.h"
#include "core/math/gte.h"

namespace t3 {

ViewMatrix::ViewMatrix() { rot = Fx::identity(); }

// CameraBuildView: R = Rx(-pitch) * Ry(-yaw) * Rz(-roll), row 1 scaled, t = -R * eye. The
// fight camera's roll is always 0, which makes Rz the identity.
gd::Ref<ViewMatrix> ViewMatrix::build(const gd::Ref<CameraView>& view, const gd::Ref<FightTables>& tables) {
    gd::Ref<ViewMatrix> m = gd::make<ViewMatrix>();
    gd::Int yaw = -view->yaw & 0xFFF;
    gd::Int sy = FightMath::sin12(yaw, tables);
    gd::Int cy = FightMath::cos12(yaw, tables);
    gd::PI32 ry{cy, 0, -sy, 0, Fx::ONE, 0, sy, 0, cy};
    if (view->roll != 0) {
        gd::Int roll = -view->roll & 0xFFF;
        gd::Int sz = FightMath::sin12(roll, tables);
        gd::Int cz = FightMath::cos12(roll, tables);
        ry = Fx::mul_matrix(ry, gd::PI32{cz, -sz, 0, sz, cz, 0, 0, 0, Fx::ONE});
    }
    gd::Int pitch = -view->pitch & 0xFFF;
    gd::Int sx = FightMath::sin12(pitch, tables);
    gd::Int cx = FightMath::cos12(pitch, tables);
    gd::PI32 rx{Fx::ONE, 0, 0, 0, cx, sx, 0, -sx, cx};
    gd::PI32 r = Fx::mul_matrix(rx, ry);
    for (gd::Int k = 3; k < 6; ++k) r[k] = Fx::s16(Fx::trunc12(r[k] * ASPECT));
    m->rot = r;
    m->t = Gte::apply_lv(r, gd::PI32{-view->x, -view->y, -view->z});
    m->h = view->h;
    return m;
}

// FUN_80036254: the matrix the fighters are drawn with. The overhead KO camera
// (0x800B08D4) draws them 0xA3A units closer to the eye.
gd::Ref<ViewMatrix> ViewMatrix::fighter_view(bool overhead) {
    gd::Ref<ViewMatrix> m = gd::make<ViewMatrix>();
    m->rot = rot;
    m->t = t.duplicate();
    m->h = h;
    if (overhead) m->t[2] = Fx::w32(t[2] - OVERHEAD_DEPTH);
    return m;
}

// FighterDrawParts -> FUN_80036CFC: the screen position (x, y) and depth of a joint's origin;
// its view-space translation is FUN_8003B970's (ApplyMatrixLV plus the view translation).
gd::PI32 ViewMatrix::screen_of(const gd::Ref<JointFrame>& joint) {
    gd::PI32 v = Gte::apply_lv(rot, joint->t);
    gd::PI32 tr{Fx::w32(v[0] + t[0]), Fx::w32(v[1] + t[1]), Fx::w32(v[2] + t[2])};
    return Gte::rtps_origin(tr, h, SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
}

}  // namespace t3
