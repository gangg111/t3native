// remake/core/fighter/attachment_dynamics.gd
// The swinging costume attachments (draw parts 18–21: hair, belts, scarves), FUN_80037F10.
#include "core/fighter/attachment_dynamics.h"

#include "content/character_model.h"
#include "content/fighter_tables.h"
#include "content/pose_tables.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/pose_solver.h"
#include "core/math/fx.h"
#include "core/math/gte.h"

namespace t3 {

AttachmentDynamics::AttachmentDynamics(const gd::Ref<FighterTables>& fighter_tables, const gd::Ref<PoseTables>& poses,
                                       const gd::Ref<PoseSolver>& pose_solver, const gd::Ref<CharacterModel>& model) {
    tables = fighter_tables;
    pose_tables = poses;
    solver = pose_solver;
    slot = model->costume_slot;
    for (gd::Int i = 0; i < COUNT; ++i) {
        gd::Ref<CharacterModel::Part> part = model->parts[FIRST_PART + i];
        rest.append(part->rest.size() == 3 ? part->rest : gd::PI32{0, 0, 0});
        records.append(part->dynamics);
        local.append(Fx::identity());
    }
}

gd::PI32 AttachmentDynamics::update(gd::Int i, const gd::Ref<JointFrame>& parent, const gd::Ref<JointFrame>& previous,
                                    const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints) {
    gd::PI32 p = records[i];
    if (p.is_empty() || (REST_ONLY_SLOTS.has(slot) && i < 2)) {
        return local[i];
    }
    gd::PI32 r = rest[i];
    if (!started[i]) {
        started[i] = true;
        local[i] = solver->euler_to_matrix(r[0], r[1], r[2]);
        return local[i];
    }
    gd::PI32 r0 = solver->euler_to_matrix(r[0], r[1], r[2]);
    gd::PI32 world_rest = Fx::mul_matrix(parent->rot, r0);
    gd::PI32 to_rest = Fx::transpose(world_rest);
    gd::PI32 tip = Gte::apply(previous->rot, p[LENGTH], 0, 0);
    for (gd::Int k = 0; k < 3; ++k) {
        tip[k] = Fx::w32(tip[k] + previous->t[k]);
    }
    gd::PI32 pivot = Gte::apply(parent->rot, offset[0], offset[1], offset[2]);
    for (gd::Int k = 0; k < 3; ++k) {
        pivot[k] = Fx::w32(pivot[k] + parent->t[k]);
    }
    bool gravity_on = true;
    if (tip[1] >= 0) {
        gravity_on = false;
        tip[1] = 0;
    }
    gd::PI32 d{Fx::w32(tip[0] - pivot[0]), Fx::w32(tip[1] - pivot[1]), Fx::w32(tip[2] - pivot[2])};
    gd::PI32 dir = Gte::vector_normal(Gte::apply_lv(to_rest, d), tables->rsqrt);
    gd::PI32 down{0, 0, 0};
    if (gravity_on) {
        gd::Int side = Fx::s16(p[BALANCE] >> 4);
        down = Gte::apply(to_rest, side, Fx::s16((WEIGHT_ONE - p[BALANCE]) * 0x10), side);
    }
    gd::PI32 out{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        gd::Int v = Fx::w32(dir[k] * p[INERTIA] + down[k] * p[GRAVITY]);
        out[k] = (v < 0 ? v + 0xFF : v) >> 8;
    }
    out[0] = Fx::w32(out[0] + (WEIGHT_ONE - (p[INERTIA] + p[GRAVITY])) * 0x10);
    bool flipped = false;
    if (p[MODE] == MODE_TARGET) {
        flipped = _aim(p, to_rest, pivot, joints);
    } else if (SLOT_CLAMP_B.has(slot) && i == 0) {
        out = _lean(to_rest, previous, joints, out);
    }
    gd::Int lim_index = p[LIMIT];
    if (slot == SLOT_FLIP && flipped) {
        lim_index = FLIP_LIMIT;
    }
    gd::PI32 lim = tables->attachment_limits[lim_index];
    gd::Int yaw_raw = Gte::ratan2(Fx::w32(lim[0] * out[2]), Fx::w32(lim[1] * out[0]), tables->ratan);
    gd::Int flat = pose_tables->square_root0(Fx::w32(out[0] * out[0] + out[2] * out[2]));
    gd::Int pitch_raw = Gte::ratan2(Fx::w32(lim[2] * out[1]), Fx::w32(lim[3] * flat), tables->ratan);
    gd::Int yaw = Fx::w32(yaw_raw * lim[4]);
    gd::Int pitch = Fx::w32(pitch_raw * lim[5]);
    if (p[MODE] == MODE_TARGET) {
        gd::PI32 t = tables->attachment_limits[p[TARGET_LIMIT]];
        gd::Int f = Fx::s16(t[5]);
        gd::Int e = Fx::s16(t[4]);
        if (Fx::w32(f * target_yaw + e * target_pitch) < Fx::w32(f * yaw + e * pitch)) {
            yaw = target_yaw;
            pitch = target_pitch;
        }
    }
    gd::Int s_yaw = Fx::s16(yaw << 4);
    gd::Int s_pitch = Fx::w32(pitch << 4);
    if (slot == SLOT_NO_YAW_0 && i == 0) {
        s_yaw = 0;
    }
    if (slot == SLOT_NO_YAW_PARTS && (i == 1 || i == 2)) {
        s_yaw = 0;
    }
    if (slot == SLOT_NO_YAW_2 && i == 2) {
        s_yaw = 0;
    }
    if (slot == SLOT_FLIP) {
        if (i == 0) {
            s_pitch = gd::maxi(s_pitch, MIN_PITCH);
        } else if (i == 3) {
            s_yaw = 0;
        }
    }
    gd::Int pitch16 = Fx::s16(s_pitch);
    gd::Int clamp_at = -1;
    if (SLOT_CLAMP_B.has(slot) && i < 4) {
        clamp_at = 2;
    } else if (slot == SLOT_CLAMP_A) {
        clamp_at = 0;
    }
    if (clamp_at >= 0) {
        gd::Int hi = tables->attachment_clamps[clamp_at];
        gd::Int lo = tables->attachment_clamps[clamp_at + 1];
        if (s_yaw > hi) {
            s_yaw = hi;
        } else if (s_yaw < lo) {
            s_yaw = lo;
        }
        if (pitch16 > hi) {
            pitch16 = hi;
        } else if (pitch16 < lo) {
            pitch16 = lo;
        }
    }
    local[i] = Fx::mul_matrix(r0, solver->euler_to_matrix(0, s_yaw, pitch16));
    return local[i];
}

bool AttachmentDynamics::_aim(const gd::PI32& p, const gd::PI32& to_rest, const gd::PI32& pivot,
                              const gd::Arr<gd::Ref<JointFrame>>& joints) {
    gd::Ref<JointFrame> j = joints[p[TARGET_JOINT]];
    gd::PI32 v = Gte::apply_lv(j->rot, gd::PI32{gd::Int(p[TARGET_OFFSET]), gd::Int(p[TARGET_OFFSET + 1]),
                                                gd::Int(p[TARGET_OFFSET + 2])});
    for (gd::Int k = 0; k < 3; ++k) {
        v[k] = Fx::w32(v[k] + j->t[k] - pivot[k]);
    }
    v = Gte::apply_lv(to_rest, v);
    gd::Int lim_index = p[LIMIT];
    bool flipped = false;
    if (slot == SLOT_FLIP && v[0] < 0) {
        lim_index = FLIP_TARGET_LIMIT;
        flipped = true;
    }
    gd::PI32 lim = tables->attachment_limits[lim_index];
    target_yaw = Fx::w32(Gte::ratan2(Fx::w32(lim[0] * v[2]), Fx::w32(lim[1] * v[0]), tables->ratan) * lim[4]);
    gd::Int flat = pose_tables->square_root0(Fx::w32(v[0] * v[0] + v[2] * v[2]));
    target_pitch = Fx::w32(Gte::ratan2(Fx::w32(lim[2] * v[1]), Fx::w32(lim[3] * flat), tables->ratan) * lim[5]);
    return flipped;
}

gd::PI32 AttachmentDynamics::_lean(const gd::PI32& to_rest, const gd::Ref<JointFrame>& previous,
                                   const gd::Arr<gd::Ref<JointFrame>>& joints, const gd::PI32& out) {
    gd::PI32 a = joints[LEAN_JOINTS[1]]->t;
    gd::PI32 b = joints[LEAN_JOINTS[0]]->t;
    gd::PI32 d{Fx::w32(b[0] - a[0]), Fx::w32(b[1] - a[1]), Fx::w32(b[2] - a[2])};
    gd::Int sq = Fx::w32(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (sq < LEAN_MIN) {
        return out;
    }
    gd::Int length = pose_tables->square_root0(sq);
    if (length == 0) {
        length = 1;
    }
    gd::PI32 v{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        v[k] = Fx::w32(Fx::div_trunc(Fx::w32(d[k] << 7), length) + a[k]);
    }
    v[2] = Fx::w32(v[2] + LEAN_LIFT);
    for (gd::Int k = 0; k < 3; ++k) {
        v[k] = Fx::w32(v[k] - previous->t[k]);
    }
    return Gte::vector_normal(Gte::apply_lv(to_rest, v), tables->rsqrt);
}

}  // namespace t3
