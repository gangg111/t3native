// remake/presentation/world_space.gd
#include "presentation/world_space.h"

#include "core/fighter/joint_frame.h"
#include "core/math/fx.h"
#include "gfx3d/math3d.h"

namespace t3 {

Vector3 WorldSpace::point(double x, double y, double z) {
    // Vector3(x, -y, -z) / UNITS_PER_METRE: Godot's Vector3 holds floats (real_t), divided by a real_t.
    return Vector3(static_cast<float>(x), static_cast<float>(-y), static_cast<float>(-z)) / static_cast<float>(UNITS_PER_METRE);
}

Vector3 WorldSpace::point_i(const gd::PI32& v) {
    return point(static_cast<double>(gd::Int(v[0])), static_cast<double>(gd::Int(v[1])), static_cast<double>(gd::Int(v[2])));
}

// A game angle (4096 units per turn) in radians.
double WorldSpace::radians(gd::Int angle) {
    return static_cast<double>(angle) * 6.283185307179586 / ANGLE_UNITS;   // TAU
}

// The three rows of a joint's affine matrix in Godot space, applied to points in the
// game's part-local units: G = C.[R / 4096 | t] / 1000 with C = diag(1, -1, -1).
gd::Arr<Vector4> WorldSpace::joint_rows(const gd::Ref<JointFrame>& frame) {
    const gd::PI32& r = frame->rot;
    double k = 1.0 / (static_cast<double>(Fx::ONE) * UNITS_PER_METRE);
    double s = 1.0 / UNITS_PER_METRE;
    return gd::Arr<Vector4>{
        Vector4(static_cast<float>(gd::Int(r[0]) * k), static_cast<float>(gd::Int(r[1]) * k), static_cast<float>(gd::Int(r[2]) * k),
                static_cast<float>(gd::Int(frame->t[0]) * s)),
        Vector4(static_cast<float>(-gd::Int(r[3]) * k), static_cast<float>(-gd::Int(r[4]) * k), static_cast<float>(-gd::Int(r[5]) * k),
                static_cast<float>(-gd::Int(frame->t[1]) * s)),
        Vector4(static_cast<float>(-gd::Int(r[6]) * k), static_cast<float>(-gd::Int(r[7]) * k), static_cast<float>(-gd::Int(r[8]) * k),
                static_cast<float>(-gd::Int(frame->t[2]) * s)),
    };
}

}  // namespace t3
