#pragma once
// remake/core/fighter/pose_solver.gd: pose vector (49 channels) -> the rotations of the 18 local
// joint matrices.
//
// Rules from docs/research/formats/animation.md#pose-to-matrices: ten Euler joints, the limb
// basis, the normalised skeleton, and four two-bone IK limbs. The arithmetic follows the game's
// GTE rounding; results are checked against golden traces.
#include "content/pose_tables.h"
#include "core/fighter/joint_frame.h"

namespace t3 {

class PoseSolver {
public:
    static constexpr gd::Int SLOT_COUNT = 18;
    static constexpr gd::Int EULER_CHANNEL = 19;
    static constexpr gd::Int ARM_MAX_REACH = 0x215;
    static constexpr gd::Int ARM_MIN_REACH = 0x47;
    static constexpr gd::Int LEG_MAX_REACH = 0x37B;
    static constexpr gd::Int LEG_MIN_REACH = 3;

    gd::Ref<PoseTables> tables;
    bool wide_elbow = false;   // Gameplay fix #8: a 64-bit product for the arm bend

    explicit PoseSolver(gd::Ref<PoseTables> pose_tables) : tables(std::move(pose_tables)) {}

    // Writes the rotations of `slots` (18 x PackedInt32Array(9)) for one pose vector.
    // Slots the IK leaves alone (targets closer than the minimum reach) keep their values.
    void build(const gd::PI32& pose, const gd::Arr<gd::PI32>& slots) const;

    // EulerToMatrix: Rz(z)*Ry(y)*Rx(x) with the game's rounding.
    gd::PI32 euler_to_matrix(gd::Int x, gd::Int y, gd::Int z) const;

    gd::PI32 _limb_target(const gd::PI32& target, const gd::Ref<JointFrame>& root, const gd::PI32& offset) const;
    void _solve_limb(const gd::PI32& target, gd::Int swivel, bool is_arm, const gd::Arr<gd::PI32>& slots,
                    gd::Int upper_slot, gd::Int lower_slot) const;
    void _aim_and_bend(const gd::PI32& target, gd::Int swivel, bool is_arm, gd::PI32 upper, gd::PI32 lower) const;
    static void _bend(gd::PI32 m, gd::Int s, gd::Int c);
    static void _set_identity(gd::PI32 m);
};

}  // namespace t3
