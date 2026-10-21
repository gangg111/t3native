// remake/core/camera/backdrop_turn.gd
// The panorama's turn (StageBackgroundDraw, FUN_8006E4A0; stages.md#panorama): each drawn frame the
// game keeps a point 4096 units ahead of the camera; while a round runs and the camera has not
// turned since the last frame (its yaw moved by at most one unit), the bearing from the camera to
// the last frame's point shows how far it slid sideways, and the backdrop turns by 2/3 of that
// (at most 0xAA units a frame). The angle turns the panorama about the fighters' midpoint.
#include "core/camera/backdrop_turn.h"

#include "content/camera_tables.h"
#include "content/pose_tables.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_view.h"
#include "core/fight/round_state.h"
#include "core/math/fx.h"

namespace t3 {

// FUN_8006CB40 (the round start): the backdrop straight again.
void BackdropTurn::reset() { angle = 0; }

// FUN_8006CC44 (StageBackgroundSetup): the backdrop straight and the camera's yaw noted as both
// this and the last frame's.
void BackdropTurn::setup(const gd::Ref<CameraView>& view) {
    angle = 0;
    prev_yaw = view->yaw & 0xFFF;
    yaw = prev_yaw;
}

// StageBackgroundDraw's turn of this frame (the sine table is the pose tables').
void BackdropTurn::step(const gd::Ref<CameraView>& view, gd::Int round_state, const gd::Ref<PoseTables>& poses,
                        const gd::Ref<CameraTables>& camera_tables) {
    angle = (angle + _turn(view, round_state, poses, camera_tables)) & 0xFFF;
}

// FUN_8006E4A0.
gd::Int BackdropTurn::_turn(const gd::Ref<CameraView>& view, gd::Int round_state, const gd::Ref<PoseTables>& poses,
                            const gd::Ref<CameraTables>& camera_tables) {
    gd::Int bearing = Fx::s16(CameraMath::atan2_units4096(point_x - view->x, point_z - view->z, camera_tables));
    gd::Int y = view->yaw & 0xFFF;
    prev_yaw = yaw & 0xFFF;
    yaw = y;
    point_x = Fx::w32(poses->sin12(-y) + view->x);
    point_z = Fx::w32(poses->cos12(-y) + view->z);
    gd::Int u = (bearing - QUARTER) & 0xFFF;
    if (round_state == RoundState::INTRO) return 0;
    gd::Int turned = Fx::s16(prev_yaw * 16 - y * 16);
    if (turned < 0) turned = Fx::s16(-turned);
    if (turned >= AHEAD_TURN_LIMIT) return 0;
    gd::Int d = Fx::s16(u * 16 - y * 16);
    if (d < 0) {
        gd::Int q = (((y - u) * 16) & 0xFFFF) / TURN_DIVISOR;
        return (q > TURN_MAX ? -TURN_MAX : -q) & 0xFFF;
    }
    gd::Int q = d / TURN_DIVISOR;
    return q <= TURN_MAX ? q & 0xFFF : TURN_MAX;
}

}  // namespace t3
