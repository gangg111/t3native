#include "core/fighter/pose_solver.h"

namespace t3 {

using gd::Int;
using gd::PI32;

namespace {
const Int BASIS_SLOTS[] = {2, 3, 7};

enum LimbRoot { RIGHT_SHOULDER, LEFT_SHOULDER, HIPS };
// Limb solve order: IK root frame, target channel, offset index, swivel channel,
// upper and lower slot, arm or leg.
struct Limb {
    Int root, channel, offset, swivel, upper, lower;
    bool arm;
};
const Limb LIMBS[] = {
    {RIGHT_SHOULDER, 3, 0, 15, 4, 5, true},
    {LEFT_SHOULDER, 6, 1, 16, 8, 9, true},
    {HIPS, 9, 2, 17, 12, 13, false},
    {HIPS, 12, 3, 18, 15, 16, false},
};
}  // namespace

void PoseSolver::build(const PI32& pose, const gd::Arr<PI32>& slots) const {
    for (Int j = 0; j < 10; ++j) {
        Int c = EULER_CHANNEL + 3 * j;
        slots[tables->euler_slots[j]] = euler_to_matrix(pose[c], pose[c + 1], pose[c + 2]);
    }
    for (Int s : BASIS_SLOTS) slots[s] = Fx::mul_matrix(slots[s], tables->limb_basis);
    auto root = gd::make<JointFrame>(slots[0], PI32{pose[0], pose[1], pose[2]});
    auto chest = JointFrame::compose_offset(root, slots[1], tables->skeleton_offsets[0]);
    gd::Arr<gd::Ref<JointFrame>> frames{
        JointFrame::compose_offset(chest, slots[3], tables->skeleton_offsets[1]),
        JointFrame::compose_offset(chest, slots[7], tables->skeleton_offsets[2]),
        JointFrame::compose_offset(root, slots[11], tables->skeleton_offsets[3]),
    };
    gd::Arr<PI32> targets;
    for (const Limb& limb : LIMBS) {
        Int c = limb.channel;
        PI32 target{pose[c], pose[c + 1], pose[c + 2]};
        targets.append(_limb_target(target, frames[limb.root], tables->limb_offsets[limb.offset]));
    }
    for (Int i = 0; i < 4; ++i) {
        const Limb& limb = LIMBS[i];
        _solve_limb(targets[i], pose[limb.swivel], limb.arm, slots, limb.upper, limb.lower);
    }
}

PI32 PoseSolver::euler_to_matrix(Int x, Int y, Int z) const {
    Int sx = tables->sin16(x);
    Int cx = tables->cos16(x);
    Int sy = tables->sin16(y);
    Int cy = tables->cos16(y);
    Int sz = tables->sin16(z);
    Int cz = tables->cos16(z);
    Int a1 = Fx::sat16((sx * cz) >> 12);
    Int a2 = Fx::sat16((sx * sz) >> 12);
    Int a3 = Fx::sat16((sx * cy) >> 12);
    Int cxcz = (cx * cz) >> 12;
    Int b1 = Fx::sat16((sy * a1) >> 12);
    Int b2 = Fx::sat16((sy * a2) >> 12);
    Int b3 = Fx::sat16((sy * cxcz) >> 12);
    Int cxsz = (cx * sz) >> 12;
    PI32 m;
    m.resize(9);
    m[0] = Fx::s16(Fx::sat16((cy * cz) >> 12));
    m[1] = Fx::s16(b1 - cxsz);
    m[2] = Fx::s16(a2 + b3);
    m[3] = Fx::s16(Fx::sat16((cy * sz) >> 12));
    m[4] = Fx::s16(cxcz + b2);
    m[5] = Fx::s16(((cxsz * sy) >> 12) - a1);
    m[6] = Fx::s16(-sy);
    m[7] = Fx::s16(a3);
    m[8] = Fx::s16(Fx::sat16((cy * cx) >> 12));
    return m;
}

// IkLimbTarget: the target in the limb root's frame. The difference is rotated in two
// 15-bit halves (high*8 + low) for precision, as the game does through the GTE.
PI32 PoseSolver::_limb_target(const PI32& target, const gd::Ref<JointFrame>& root, const PI32& offset) const {
    PI32 rt = Fx::transpose(root->rot);
    PI32 hi{0, 0, 0};
    PI32 lo{0, 0, 0};
    for (Int i = 0; i < 3; ++i) {
        Int d = target[i] - root->t[i];
        if (d < 0) {
            hi[i] = -((-d) >> 15);
            lo[i] = -((-d) & 0x7FFF);
        } else {
            hi[i] = d >> 15;
            lo[i] = d & 0x7FFF;
        }
    }
    PI32 low = Fx::mvmva(rt, lo[0], lo[1], lo[2]);
    PI32 out{0, 0, 0};
    for (Int r = 0; r < 3; ++r) {
        Int high = Fx::sat16(rt[3 * r] * hi[0] + rt[3 * r + 1] * hi[1] + rt[3 * r + 2] * hi[2]);
        out[r] = low[r] + high * 8 + offset[r];
    }
    return out;
}

// IkSolveLimb: aim rotation into slot `upper_slot`, bend into `lower_slot`. All products
// wrap to 32 bits like the original C code (game-bugs.md #8 relies on it for arm targets
// near 71-74).
void PoseSolver::_solve_limb(const PI32& target, Int swivel, bool is_arm, const gd::Arr<PI32>& slots, Int upper_slot,
                            Int lower_slot) const {
    PI32 upper = slots[upper_slot];
    PI32 lower = slots[lower_slot];
    _aim_and_bend(target, swivel, is_arm, upper, lower);
    slots[upper_slot] = upper;
    slots[lower_slot] = lower;
}

void PoseSolver::_aim_and_bend(const PI32& target, Int swivel, bool is_arm, PI32 upper, PI32 lower) const {
    Int x = target[0];
    Int y = target[1];
    Int z = target[2];
    Int sq = Fx::w32(x * x + y * y + z * z);
    if (sq == 0) return;
    Int length = tables->square_root0(sq);
    Int inv = Fx::div_trunc(0x400000, length);
    Int ux = Fx::w32(x * inv) >> 10;
    Int uy = Fx::w32(y * inv) >> 10;
    Int uz = Fx::w32(z * inv) >> 10;
    Int k = ux + Fx::ONE;
    Int a;
    Int e;
    if (k == 0) {
        k = -0x800;
        a = Fx::w32(y * inv) >> 11;
        e = -Fx::ONE;
    } else {
        if (k < 0x2000) {
            k = Fx::div_trunc(-0x1000000, k);
            a = Fx::w32(k * uy) >> 12;
        } else {
            k = -0x800;
            a = (-uy) >> 1;
        }
        e = Fx::ONE;
    }
    Int b = (Fx::w32(a * uy) >> 12) + e;
    k = Fx::w32(k * uz) >> 12;
    upper[0] = Fx::s16(ux);
    upper[3] = Fx::s16(uy);
    upper[6] = Fx::s16(uz);
    Int c = tables->cos16(swivel);
    Int s = tables->sin16(swivel);
    Int p = Fx::w32(a * (e + ux)) >> 12;
    Int q = Fx::w32(k * (e + ux)) >> 12;
    Int r = Fx::w32(k * uy) >> 12;
    Int w = Fx::w32(a * uz) >> 12;
    Int v = (Fx::w32(k * uz) >> 12) + Fx::ONE;
    upper[1] = Fx::s16(Fx::w32(c * p + s * q) >> 12);
    upper[4] = Fx::s16(Fx::w32(c * b + s * r) >> 12);
    upper[7] = Fx::s16(Fx::w32(c * w + s * v) >> 12);
    upper[2] = Fx::s16(Fx::w32(-s * p + c * q) >> 12);
    upper[5] = Fx::s16(Fx::w32(-s * b + c * r) >> 12);
    upper[8] = Fx::s16(Fx::w32(-s * w + c * v) >> 12);
    Int d;
    Int cos_a;
    Int cos_b;
    if (is_arm) {
        if (length > ARM_MAX_REACH) {
            _set_identity(lower);
            return;
        }
        if (length < ARM_MIN_REACH) return;
        d = (wide_elbow ? inv * 0x9204 : Fx::w32(inv * 0x9204)) >> 11;
        cos_a = Fx::w32((length * 0x800 + d) * 0xD9) >> 16;
        cos_b = Fx::w32((length * 0x800 - d) * 0x11A) >> 16;
    } else {
        if (length > LEG_MAX_REACH) {
            _set_identity(lower);
            return;
        }
        if (length < LEG_MIN_REACH) return;
        d = Fx::w32(inv * 0x6F8) >> 11;
        cos_a = Fx::w32((length * 0x800 + d) * 0x92) >> 16;
        cos_b = Fx::w32((length * 0x800 - d) * 0x93) >> 16;
    }
    Int sin_a = 0;
    if (cos_a > Fx::ONE) cos_a = Fx::ONE;
    else if (cos_a < -Fx::ONE) cos_a = -Fx::ONE;
    else {
        sin_a = tables->square_root0(0x1000000 - cos_a * cos_a);
        if (!is_arm) sin_a = -sin_a;
    }
    _bend(upper, Fx::s16(sin_a), cos_a);
    Int sin_b = 0;
    if (cos_b > Fx::ONE) cos_b = Fx::ONE;
    else if (cos_b < -Fx::ONE) cos_b = -Fx::ONE;
    else sin_b = Fx::w32(sin_a * (is_arm ? -0x14D4 : -0x1012)) >> 12;
    Int diag = Fx::s16(Fx::trunc12(Fx::w32(cos_a * cos_b + sin_a * sin_b)));
    Int off = Fx::s16(Fx::trunc12(Fx::w32(-sin_a * cos_b + cos_a * sin_b)));
    lower[0] = diag;
    lower[1] = Fx::s16(-off);
    lower[2] = 0;
    lower[3] = off;
    lower[4] = diag;
    lower[5] = 0;
    lower[6] = 0;
    lower[7] = 0;
    lower[8] = Fx::ONE;
}

// IkBendMatrix: rotates columns 0 and 1 of the upper limb by the bend angle.
void PoseSolver::_bend(PI32 m, Int s, Int c) {
    Int a0 = m[0];
    Int a3 = m[3];
    Int a6 = m[6];
    m[0] = Fx::s16((a0 * c + m[1] * s) >> 12);
    m[3] = Fx::s16((a3 * c + m[4] * s) >> 12);
    m[1] = Fx::s16((-a0 * s + m[1] * c) >> 12);
    m[4] = Fx::s16((-a3 * s + m[4] * c) >> 12);
    m[6] = Fx::s16((a6 * c + m[7] * s) >> 12);
    m[7] = Fx::s16((-a6 * s + m[7] * c) >> 12);
}

void PoseSolver::_set_identity(PI32 m) {
    for (Int i = 0; i < 9; ++i) m[i] = Fx::IDENTITY()[i];
}

}  // namespace t3
