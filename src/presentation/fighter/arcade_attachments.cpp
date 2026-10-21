// remake/presentation/fighter/arcade_attachments.gd
#include "presentation/fighter/arcade_attachments.h"

#include "content/fighter_tables.h"
#include "content/json_file.h"
#include "content/pose_tables.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/pose_solver.h"
#include "core/math/fx.h"
#include "core/math/gte.h"
#include "gd/io.h"

namespace t3 {

ArcadeAttachments::ArcadeAttachments(const gd::Ref<CharacterModel>& character) {
    model = character;
    for (const auto& a : model->arcade_attachments) {
        _joints.append(a->joint);
    }
    reset();
}

// Starts again from the rest rotations (FUN_8019b634, when a model is set up).
void ArcadeAttachments::reset() {
    _started.clear();
    _tips.clear();
    for (gd::Int i = 0; i < CharacterModel::JOINT_COUNT - CharacterModel::ARCADE_JOINT; ++i) {
        _started.append(0);
        _tips.append(gd::PI32{0, 0, 0});
    }
    _target_yaw = 0;
    _target_pitch = 0;
    _previous.clear();
}

// Whether the model has an attachment on `joint`.
bool ArcadeAttachments::has_joint(gd::Int joint) const { return _joints.has(joint); }

// The Euler conversion of the game's tables (PoseSolver.euler_to_matrix), loaded once.
gd::Ref<PoseSolver> ArcadeAttachments::solver() {
    if (_solver == nullptr) {
        _solver = gd::make<PoseSolver>(PoseTables::load_from(gd::path_join(assets_root, "tables/pose.json")));
    }
    return _solver;
}

gd::Ref<FighterTables> ArcadeAttachments::fighter_tables() {
    if (_fighter_tables == nullptr) {
        _fighter_tables = FighterTables::load_from(gd::path_join(assets_root, "tables/fighter.json"));
    }
    return _fighter_tables;
}

// The arcade's axis records (0x801FD170: six signed bytes each) and clamps (0x801FD2F0).
void ArcadeAttachments::load_tables() {
    if (!_limits.is_empty()) {
        return;
    }
    gd::Json data = JsonFile::read(gd::path_join(assets_root, TABLES_FILE));
    const gd::Json& limits = data.get("limits");   // data.get("limits", [])
    if (limits.is_array()) {
        for (const gd::Json& record : limits.items()) {
            _limits.append(JsonFile::ints(record));
        }
    }
    _clamps = JsonFile::ints(data.get("clamps"));
}

// The skeleton's `joints` followed by the attachments' joints, CharacterModel.JOINT_COUNT in all
// (identity where the model has no attachment).
gd::Arr<gd::Ref<JointFrame>> ArcadeAttachments::update(const gd::Arr<gd::Ref<JointFrame>>& joints) {
    gd::Arr<gd::Ref<JointFrame>> all = joints.duplicate();
    while (all.size() < CharacterModel::JOINT_COUNT) {
        all.append(gd::make<JointFrame>());
    }
    if (_previous.size() == all.size()) {
        for (const auto& a : model->arcade_attachments) {
            all[a->joint] = _previous[a->joint];
        }
    }
    for (const auto& a : model->arcade_attachments) {
        gd::PI32 offset{gd::Int(a->offset.x), gd::Int(a->offset.y), -gd::Int(a->offset.z)};
        gd::Ref<JointFrame> previous = all[a->joint];
        gd::Ref<JointFrame> parent = all[a->parent];
        gd::PI32 local = _local(a, previous, offset, all);
        all[a->joint] = JointFrame::compose_offset(parent, local, offset);
    }
    _previous = all;
    return all;
}

gd::PI32 ArcadeAttachments::_local(const gd::Ref<CharacterModel::ArcadeAttachment>& a, const gd::Ref<JointFrame>& previous,
                                   const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints) {
    gd::Int i = a->joint - CharacterModel::ARCADE_JOINT;
    gd::PI32 angles{Fx::s16(gd::Int(a->rest[0]) + gd::Int(a->record[0])), Fx::s16(gd::Int(a->rest[1]) + gd::Int(a->record[1])),
                    Fx::s16(gd::Int(a->rest[2]) + gd::Int(a->record[2]))};
    if (a->static_record) {
        return solver()->euler_to_matrix(angles[0], angles[1], angles[2]);
    }
    if (REST_ONLY_SLOTS.has(model->costume_slot) && i < REST_ONLY_COUNT) {
        return Fx::identity();
    }
    if (!_started[i]) {
        _started[i] = 1;
        return solver()->euler_to_matrix(angles[0], angles[1], angles[2]);
    }
    return swing(model->costume_slot, i, angles, a->record, previous, joints[a->parent], offset, joints);
}

// FUN_8019b6d8 for a started swinging attachment `i`: its local rotation this step, from its
// rest `angles`, `record`, its own joint of the last step, its parent's and the other joints
// of this step, by joint. Updates the smoothed tip and mode 1's target angles.
gd::PI32 ArcadeAttachments::swing(gd::Int slot, gd::Int i, const gd::PI32& angles, const gd::PI32& record,
                                  const gd::Ref<JointFrame>& previous, const gd::Ref<JointFrame>& parent,
                                  const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints) {
    load_tables();
    gd::Ref<FighterTables> tables = fighter_tables();
    gd::PI32 r0 = solver()->euler_to_matrix(angles[0], angles[1], angles[2]);
    gd::PI32 to_rest = Fx::transpose(Fx::mul_matrix(parent->rot, r0));
    // The tip of the last step, smoothed over SMOOTHING steps.
    gd::PI32 tip = Gte::apply_lv(previous->rot, gd::PI32{record[LENGTH], 0, 0});
    gd::PI32 smoothed = _tips[i];
    gd::Int n = record[SMOOTHING];
    for (gd::Int k = 0; k < 3; ++k) {
        tip[k] = Fx::w32(gd::Int(tip[k]) + gd::Int(previous->t[k]));
        smoothed[k] = Fx::div_trunc(Fx::w32((n - 1) * smoothed[k] + tip[k]), n);
        tip[k] = smoothed[k];
    }
    gd::PI32 pivot = Gte::apply_lv(parent->rot, offset);
    for (gd::Int k = 0; k < 3; ++k) {
        pivot[k] = Fx::w32(gd::Int(pivot[k]) + gd::Int(parent->t[k]));
    }
    bool gravity_on = true;
    if (tip[1] >= 0) {
        gravity_on = false;
        tip[1] = 0;
    }
    gd::PI32 d{Fx::w32(gd::Int(tip[0]) - gd::Int(pivot[0])), Fx::w32(gd::Int(tip[1]) - gd::Int(pivot[1])),
               Fx::w32(gd::Int(tip[2]) - gd::Int(pivot[2]))};
    gd::PI32 dir = Gte::vector_normal(Gte::apply_lv(to_rest, d), tables->rsqrt);
    gd::PI32 down{0, 0, 0};
    if (gravity_on) {
        gd::Int g = record[GRAVITY];
        down = Gte::apply(to_rest, _shift8(wind[0] * g), (WEIGHT_ONE - g) * 0x10, _shift8(wind[2] * g));
    }
    gd::PI32 v{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        v[k] = _shift8(Fx::w32(record[TOWARDS] * dir[k] + record[REST] * down[k]));
    }
    v[0] = Fx::w32(gd::Int(v[0]) + (WEIGHT_ONE - (gd::Int(record[TOWARDS]) + gd::Int(record[REST]))) * 0x10);
    bool flipped = false;
    if (record[MODE] == MODE_LINE) {
        v = _line(record, to_rest, v, pivot, joints[joint_of_row(record[TARGET_JOINT])]);
    } else if (record[MODE] == MODE_TARGET) {
        flipped = _aim(slot, record, to_rest, pivot, joints[joint_of_row(record[TARGET_JOINT])]);
    }
    if (slot == SLOT_CLAMP_B && i == LEAN_ATTACHMENT) {
        v = _lean(to_rest, previous, joints, v);
    }
    gd::Int axis = record[AXIS] & 0xFFFF;
    if (slot == SLOT_FLIP && flipped) {
        axis = FLIP_AXIS;
    }
    gd::PI32 lim = _limits[axis];
    gd::Int yaw = Fx::w32(Gte::ratan2(Fx::w32(lim[0] * v[2]), Fx::w32(lim[1] * v[0]), tables->ratan) * lim[4]);
    gd::Int flat = solver()->tables->square_root0(Fx::w32(v[0] * v[0] + v[2] * v[2]));
    gd::Int pitch = Fx::w32(Gte::ratan2(Fx::w32(lim[2] * v[1]), Fx::w32(lim[3] * flat), tables->ratan) * lim[5]);
    if (record[MODE] == MODE_TARGET) {
        gd::PI32 t = _limits[record[TARGET_AXIS]];
        if (Fx::w32(t[5] * _target_yaw + t[4] * _target_pitch) < Fx::w32(t[5] * yaw + t[4] * pitch)) {
            yaw = _target_yaw;
            pitch = _target_pitch;
        }
    }
    gd::Int s_yaw = Fx::s16(yaw << 4);
    gd::Int s_pitch = Fx::s16(pitch << 4);
    gd::Int clamp_at = -1;
    if (slot == SLOT_CLAMP_A) {
        clamp_at = 0;
    } else if (slot == SLOT_CLAMP_B && i < CLAMP_B_COUNT) {
        clamp_at = 2;
    }
    if (clamp_at >= 0) {
        gd::Int hi = _clamps[clamp_at];
        gd::Int lo = _clamps[clamp_at + 1];
        if (s_yaw > hi) {
            s_yaw = hi;
        } else if (s_yaw < lo) {
            s_yaw = lo;
        }
        if (s_pitch > hi) {
            s_pitch = hi;
        } else if (s_pitch < lo) {
            s_pitch = lo;
        }
    }
    return Fx::mul_matrix(r0, solver()->euler_to_matrix(0, s_yaw, s_pitch));
}

// Mode 1's target angles (FUN_8019bed0): towards the record's point on the target joint, seen
// in the rest frame; true when costume slot 0x26 flips its axis (the point is behind).
bool ArcadeAttachments::_aim(gd::Int slot, const gd::PI32& record, const gd::PI32& to_rest, const gd::PI32& pivot,
                             const gd::Ref<JointFrame>& target) {
    gd::PI32 v = Gte::apply_lv(target->rot, gd::PI32{record[TARGET_POINT], record[TARGET_POINT + 1],
                                                     record[TARGET_POINT + 2]});
    for (gd::Int k = 0; k < 3; ++k) {
        v[k] = Fx::w32(gd::Int(v[k]) + gd::Int(target->t[k]) - gd::Int(pivot[k]));
    }
    v = Gte::apply_lv(to_rest, v);
    gd::Int axis = record[AXIS] & 0xFFFF;
    bool flipped = false;
    if (slot == SLOT_FLIP && v[0] < 0) {
        flipped = true;
        axis = FLIP_AXIS;
    }
    gd::PI32 lim = _limits[axis];
    gd::Ref<FighterTables> tables = fighter_tables();
    _target_yaw = Fx::w32(Gte::ratan2(Fx::w32(lim[0] * v[2]), Fx::w32(lim[1] * v[0]), tables->ratan) * lim[4]);
    gd::Int flat = solver()->tables->square_root0(Fx::w32(v[0] * v[0] + v[2] * v[2]));
    _target_pitch = Fx::w32(Gte::ratan2(Fx::w32(lim[2] * v[1]), Fx::w32(lim[3] * flat), tables->ratan) * lim[5]);
    return flipped;
}

// Mode 2 (FUN_8019c15c): the direction `v` (scaled by the tip's length) is kept off the line
// through the record's point on the target joint along the joint's x axis: within the reach it
// is pushed out to the reach.
gd::PI32 ArcadeAttachments::_line(const gd::PI32& record, const gd::PI32& to_rest, const gd::PI32& v, const gd::PI32& pivot,
                                  const gd::Ref<JointFrame>& target) {
    gd::Int length = record[LENGTH];
    gd::PI32 along = Gte::apply_lv(target->rot, gd::PI32{0x1000, gd::Int(record[TARGET_POINT + 1]) << 12,
                                                         gd::Int(record[TARGET_POINT + 2]) << 12});
    gd::PI32 at = Gte::apply_lv(target->rot, gd::PI32{0, gd::Int(record[TARGET_POINT + 1]) << 12,
                                                      gd::Int(record[TARGET_POINT + 2]) << 12});
    for (gd::Int k = 0; k < 3; ++k) {
        along[k] = Fx::w32(gd::Int(along[k]) + gd::Int(target->t[k]) * 0x1000 - gd::Int(pivot[k]) * 0x1000);
        at[k] = Fx::w32(gd::Int(at[k]) + gd::Int(target->t[k]) * 0x1000 - gd::Int(pivot[k]) * 0x1000);
    }
    at = Gte::apply_lv(to_rest, at);
    along = Gte::apply_lv(to_rest, along);
    gd::PI32 c{0, 0, 0};
    gd::PI32 e{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        c[k] = Fx::w32(gd::Int(at[k]) - length * v[k]);
        e[k] = Fx::w32(gd::Int(along[k]) - gd::Int(at[k]));
    }
    gd::Int dot = _shift12(Fx::w32(c[0] * e[0] + c[1] * e[1] + c[2] * e[2]));
    // p: the line's point nearest the tip; the tip is pushed out from it to the reach.
    gd::PI32 p{0, 0, 0};
    gd::Int sq = 0;
    for (gd::Int k = 0; k < 3; ++k) {
        p[k] = _shift12(Fx::w32(gd::Int(at[k]) - _shift12(Fx::w32(dot * e[k]))));
        gd::Int q = _shift12(Fx::w32(length * v[k])) - p[k];
        sq = Fx::w32(sq + q * q);
    }
    gd::Int reach = record[TARGET_AXIS];
    if (sq >= reach * reach) {
        return v;
    }
    gd::Int root = solver()->tables->square_root0(sq);
    if (root == 0) {
        root = 1;
    }
    gd::PI32 out{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        out[k] = Fx::w32(Fx::div_trunc(Fx::w32((_shift12(Fx::w32(length * v[k])) - p[k]) * reach), root) + p[k]);
    }
    return out;
}

// FUN_8019c598: costume slot 0x24's lean towards a spot LEAN_REACH from joint 13 towards
// joint 16, lifted by LEAN_LIFT, when the joints are far enough apart.
gd::PI32 ArcadeAttachments::_lean(const gd::PI32& to_rest, const gd::Ref<JointFrame>& previous,
                                  const gd::Arr<gd::Ref<JointFrame>>& joints, const gd::PI32& v) {
    const gd::PI32& a = joints[LEAN_FROM]->t;
    const gd::PI32& b = joints[LEAN_TO]->t;
    gd::PI32 d{Fx::w32(gd::Int(b[0]) - gd::Int(a[0])), Fx::w32(gd::Int(b[1]) - gd::Int(a[1])),
               Fx::w32(gd::Int(b[2]) - gd::Int(a[2]))};
    gd::Int sq = Fx::w32(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (sq < LEAN_MIN) {
        return v;
    }
    gd::Int length = solver()->tables->square_root0(sq);
    if (length == 0) {
        length = 1;
    }
    gd::PI32 w{0, 0, 0};
    for (gd::Int k = 0; k < 3; ++k) {
        w[k] = Fx::w32(Fx::div_trunc(Fx::w32(d[k] * LEAN_REACH), length) + a[k]);
    }
    w[2] = Fx::w32(gd::Int(w[2]) + LEAN_LIFT);
    for (gd::Int k = 0; k < 3; ++k) {
        w[k] = Fx::w32(gd::Int(w[k]) - gd::Int(previous->t[k]));
    }
    return Gte::vector_normal(Gte::apply_lv(to_rest, w), fighter_tables()->rsqrt);
}

// The joint of an arcade model row (rows 18-23 are the attachments, from ARCADE_JOINT).
gd::Int ArcadeAttachments::joint_of_row(gd::Int row) {
    return row < FIRST_ATTACHMENT_ROW ? row : CharacterModel::ARCADE_JOINT + row - FIRST_ATTACHMENT_ROW;
}

// C's signed division by 256 (towards zero), as the compiled `(x + 0xFF) >> 8` for negatives.
gd::Int ArcadeAttachments::_shift8(gd::Int x) { return (x < 0 ? x + 0xFF : x) >> 8; }

gd::Int ArcadeAttachments::_shift12(gd::Int x) { return (x < 0 ? x + 0xFFF : x) >> 12; }

}  // namespace t3
