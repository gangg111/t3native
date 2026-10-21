// remake/core/fighter/fighter_skeleton.gd
// A fighter's joints for one frame: pose vector → local rotations (PoseSolver) → root
// (RootUpdate) → world joint frames (FighterComposeJoints).
#include "core/fighter/fighter_skeleton.h"

#include "content/pose_tables.h"
#include "core/fighter/attachment_dynamics.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/pose_solver.h"
#include "core/math/fx.h"

namespace t3 {

FighterSkeleton::FighterSkeleton(const gd::Ref<PoseTables>& pose_tables, const gd::Ref<CharacterModel>& model,
                                 const gd::Ref<FighterTables>& fighter_tables) {
    // Object default.
    root = gd::make<JointFrame>();

    tables = pose_tables;
    solver = gd::make<PoseSolver>(pose_tables);
    if (fighter_tables != nullptr) {
        attachments = gd::make<AttachmentDynamics>(fighter_tables, pose_tables, solver, model);
    }
    offsets = model->joint_offsets();
    parents = model->joint_parents();
    scale = model->scale_fixed();
    for (gd::Int i = 0; i < JOINTS; ++i) {
        local_slots.append(Fx::identity());
    }
    for (gd::Int i = 0; i < ALL_JOINTS; ++i) {
        joints.append(gd::make<JointFrame>());
    }
}

gd::PI32 FighterSkeleton::root_displacement(const gd::PI32& pose) const {
    return gd::PI32{
        Fx::s16((pose[0] * scale) >> 12),
        Fx::s16((pose[1] * scale) >> 12),
        Fx::s16((pose[2] * scale) >> 12),
    };
}

void FighterSkeleton::update(const gd::PI32& pose, const gd::PI32& anchor, gd::Int facing, gd::Int heading) {
    solver->build(pose, local_slots);
    gd::PI32 d = root_displacement(pose);
    gd::Int a = ((0x8000 - facing) >> 4) & 0xFFF;
    gd::Int c = tables->cos12(a);
    gd::Int s = tables->sin12(a);
    root = gd::make<JointFrame>(
        root_rotation(0, (heading - 0x8000) & 0xFFFF, 0),
        gd::PI32{
            anchor[0] + Fx::trunc12(d[0] * c - d[2] * s),
            anchor[1] + d[1],
            anchor[2] + Fx::trunc12(d[0] * s + d[2] * c),
        });
    for (gd::Int m = 0; m < JOINTS; ++m) {
        gd::Ref<JointFrame> parent = parents[m] < 0 ? root : joints[parents[m]];
        joints[m] = JointFrame::compose_offset(parent, local_slots[m], offsets[m]);
    }
    for (gd::Int m = JOINTS; m < ALL_JOINTS; ++m) {
        gd::Ref<JointFrame> parent = parents[m] < 0 ? root : joints[parents[m]];
        gd::PI32 local = Fx::identity();
        if (attachments != nullptr) {
            local = attachments->update(m - JOINTS, parent, joints[m], offsets[m], joints);
        }
        joints[m] = JointFrame::compose_offset(parent, local, offsets[m]);
    }
}

gd::PI32 FighterSkeleton::root_rotation(gd::Int x, gd::Int y, gd::Int z) const {
    return rotation_xyz(x, y, z, tables);
}

gd::PI32 FighterSkeleton::rotation_xyz(gd::Int x, gd::Int y, gd::Int z, const gd::Ref<PoseTables>& tables) {
    gd::Int cx = tables->cos12(x >> 4);
    gd::Int sx = tables->sin12(x >> 4);
    gd::Int cy = tables->cos12(y >> 4);
    gd::Int sy = tables->sin12(y >> 4);
    gd::Int cz = tables->cos12(z >> 4);
    gd::Int sz = tables->sin12(z >> 4);
    gd::Int a1 = Fx::sat16((sx * cz) >> 12);
    gd::Int a2 = Fx::sat16((sx * sz) >> 12);
    gd::Int a3 = Fx::sat16((sx * cy) >> 12);
    gd::Int cxcz = (cx * cz) >> 12;
    gd::Int b1 = Fx::sat16((sy * a1) >> 12);
    gd::Int b2 = Fx::sat16((sy * a2) >> 12);
    gd::Int b3 = Fx::sat16((sy * cxcz) >> 12);
    gd::Int cxsz = (cx * sz) >> 12;
    return gd::PI32{
        Fx::s16(Fx::sat16((cy * cz) >> 12)), Fx::s16(Fx::sat16((cy * -sz) >> 12)), Fx::s16(sy),
        Fx::s16(b1 + cxsz), Fx::s16(cxcz - b2), Fx::s16(-a3),
        Fx::s16(a2 - b3), Fx::s16(((cxsz * sy) >> 12) + a1), Fx::s16(Fx::sat16((cy * cx) >> 12)),
    };
}

}  // namespace t3
