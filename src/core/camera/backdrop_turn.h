#pragma once
// remake/core/camera/backdrop_turn.gd
// The panorama's turn (StageBackgroundDraw, FUN_8006E4A0; stages.md#panorama): each drawn frame the
// game keeps a point 4096 units ahead of the camera; while a round runs and the camera has not
// turned since the last frame (its yaw moved by at most one unit), the bearing from the camera to
// the last frame's point shows how far it slid sideways, and the backdrop turns by 2/3 of that
// (at most 0xAA units a frame). The angle turns the panorama about the fighters' midpoint.
#include "gd/gd.h"

namespace t3 {

class CameraView;
class PoseTables;
class CameraTables;

class BackdropTurn {
public:
    static constexpr gd::Int AHEAD_TURN_LIMIT = 0x11;     // the yaw's change (* 16) below which the camera counts as still
    static constexpr gd::Int TURN_DIVISOR = 0x18;         // the bearing's difference (* 16) / 24: 2/3 of it
    static constexpr gd::Int TURN_MAX = 0xAA;
    static constexpr gd::Int QUARTER = 0x400;

    gd::Int angle = 0;                     // 0x800A9650 (4096 units)
    gd::Int prev_yaw = 0;                  // 0x800A9658: the camera's yaw a frame before
    gd::Int yaw = 0;                       // 0x800A9664: the camera's yaw this frame
    gd::Int point_x = 0;                   // 0x800AE0D8: the point ahead of the camera
    gd::Int point_z = 0;                   // 0x800AE0E0

    // FUN_8006CB40 (the round start): the backdrop straight again.
    void reset();

    // FUN_8006CC44 (StageBackgroundSetup): the backdrop straight and the camera's yaw noted as both
    // this and the last frame's.
    void setup(const gd::Ref<CameraView>& view);

    // StageBackgroundDraw's turn of this frame (the sine table is the pose tables').
    void step(const gd::Ref<CameraView>& view, gd::Int round_state, const gd::Ref<PoseTables>& poses,
              const gd::Ref<CameraTables>& camera_tables);

    // FUN_8006E4A0.
    gd::Int _turn(const gd::Ref<CameraView>& view, gd::Int round_state, const gd::Ref<PoseTables>& poses,
                  const gd::Ref<CameraTables>& camera_tables);
};

}  // namespace t3
