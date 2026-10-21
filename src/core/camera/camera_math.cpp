// remake/core/camera/camera_math.gd
// Integer helpers of the camera code: `ISqrt` and `Atan2Units4096` with the game's
// rounding (camera.md). Ports of `fight_math.isqrt` and `fight_math.atan2_4096`.
#include "core/camera/camera_math.h"

#include "content/camera_tables.h"
#include "core/math/fx.h"

namespace t3 {

// ISqrt (0x8002A578) of an unsigned 32-bit value: Newton iteration from a power of two.
gd::Int CameraMath::isqrt(gd::Int n) {
    n &= 0xFFFFFFFFLL;
    if (n == 0) return 0;
    gd::Int guess = 1;
    gd::Int rest = n;
    while (guess < rest) {
        guess <<= 1;
        rest >>= 1;
    }
    while (true) {
        gd::Int prev = guess;
        guess = (gd::div(n, prev) + prev) >> 1;
        if (guess >= prev) return prev;
    }
    return 0;
}

// Atan2Units4096 (FUN_8004B634): the angle of (x, z) in 4096 units, as a signed 16-bit value.
gd::Int CameraMath::atan2_units4096(gd::Int x, gd::Int z, const gd::Ref<CameraTables>& tables) {
    x = Fx::w32(x);
    z = Fx::w32(z);
    if (x == 0 && z == 0) return 0;
    gd::Int r = 0;
    if (x < 0) {
        if (z < 0) {
            if (-x < -z)
                r = 0xC00 - _octant(Fx::div_trunc(Fx::w32(x << 10), z), tables);
            else
                r = _octant(Fx::div_trunc(Fx::w32(z << 10), x), tables) + 0x800;
        } else if (-x < z) {
            r = _octant(Fx::div_trunc(Fx::w32(x * -0x400), z), tables) + 0x400;
        } else {
            r = 0x800 - _octant(Fx::div_trunc(Fx::w32(z << 10), -x), tables);
        }
    } else if (z < 0) {
        if (x < -z)
            r = _octant(Fx::div_trunc(Fx::w32(x << 10), -z), tables) + 0xC00;
        else
            r = 0x1000 - _octant(Fx::div_trunc(Fx::w32(z * -0x400), x), tables);
    } else if (x < z) {
        r = 0x400 - _octant(Fx::div_trunc(Fx::w32(x << 10), z), tables);
    } else {
        r = _octant(Fx::div_trunc(Fx::w32(z << 10), x), tables);
    }
    return Fx::s16(r);
}

gd::Int CameraMath::_octant(gd::Int t, const gd::Ref<CameraTables>& tables) {
    return gd::Int(tables->atan_table[t]) + (t >> 1);
}

// `(v < 0 ? v + 0x3F : v) >> 6`: an 18-bit camera angle back to 4096 units.
gd::Int CameraMath::sar6(gd::Int v) { return v < 0 ? (v + 0x3F) >> 6 : v >> 6; }

}  // namespace t3
