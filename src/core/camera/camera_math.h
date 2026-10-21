#pragma once
// remake/core/camera/camera_math.gd
// Integer helpers of the camera code: `ISqrt` and `Atan2Units4096` with the game's
// rounding (camera.md). Ports of `fight_math.isqrt` and `fight_math.atan2_4096`.
#include "gd/gd.h"

namespace t3 {

class CameraTables;

class CameraMath {
public:
    // ISqrt (0x8002A578) of an unsigned 32-bit value: Newton iteration from a power of two.
    static gd::Int isqrt(gd::Int n);

    // Atan2Units4096 (FUN_8004B634): the angle of (x, z) in 4096 units, as a signed 16-bit value.
    static gd::Int atan2_units4096(gd::Int x, gd::Int z, const gd::Ref<CameraTables>& tables);

    static gd::Int _octant(gd::Int t, const gd::Ref<CameraTables>& tables);

    // `(v < 0 ? v + 0x3F : v) >> 6`: an 18-bit camera angle back to 4096 units.
    static gd::Int sar6(gd::Int v);
};

}  // namespace t3
