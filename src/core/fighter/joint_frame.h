#pragma once
// remake/core/fighter/joint_frame.gd: a joint's rotation (4.12, row-major) and translation in the
// game's world units.
#include "core/math/fx.h"

namespace t3 {

class JointFrame {
public:
    gd::PI32 rot;
    gd::PI32 t;

    explicit JointFrame(gd::PI32 rotation = Fx::identity(), gd::PI32 translation = gd::PI32{0, 0, 0})
        : rot(rotation), t(translation) {}

    // ComposeJoint / ComposeJointOffset: R = P.R*L (MVMVA per column, s16), and
    // t = P.t + P.R*offset, the product read from the 32-bit MAC registers (not saturated).
    // The offset is loaded into the 16-bit GTE vector registers, so it is truncated to s16.
    static gd::Ref<JointFrame> compose_offset(const gd::Ref<JointFrame>& parent, const gd::PI32& local_rot,
                                              const gd::PI32& offset) {
        const gd::PI32& r = parent->rot;
        gd::Int x = Fx::s16(offset[0]);
        gd::Int y = Fx::s16(offset[1]);
        gd::Int z = Fx::s16(offset[2]);
        gd::PI32 d{
            parent->t[0] + ((r[0] * x + r[1] * y + r[2] * z) >> 12),
            parent->t[1] + ((r[3] * x + r[4] * y + r[5] * z) >> 12),
            parent->t[2] + ((r[6] * x + r[7] * y + r[8] * z) >> 12),
        };
        return gd::make<JointFrame>(Fx::mul_matrix(r, local_rot), d);
    }
};

}  // namespace t3
