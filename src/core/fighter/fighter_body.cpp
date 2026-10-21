// remake/core/fighter/fighter_body.gd
// A fighter's skeleton as the fight code keeps it: the model's joint offsets and parents
// (FighterSetupParts, KmdRelocate), the 18 local joint matrices of the pose, their copy of the
// previous frame and the blend deltas, and the world joints (24 blocks: 22 draw parts and two
// spare slots for weapon points).
#include "core/fighter/fighter_body.h"

#include "content/character_model.h"
#include "core/fighter/attachment_dynamics.h"
#include "core/fighter/joint_frame.h"

namespace t3 {

FighterBody::FighterBody(const gd::Ref<CharacterModel>& character, const gd::Ref<FighterTables>& fighter_tables,
                         const gd::Ref<PoseTables>& pose_tables, const gd::Ref<PoseSolver>& solver) {
    // Object defaults, in declaration order.
    root = gd::make<JointFrame>();
    look_at = gd::make<LookAt>();

    model = character;
    offsets = character->joint_offsets();
    parents = character->joint_parents();
    for (gd::Int i = 0; i < PARTS; ++i) {
        present.append(false);
    }
    // FighterSetupPart: an attachment part (18–21) without a parent row has no matrix.
    for (gd::Int i = 0; i < character->parts.size(); ++i) {
        gd::Ref<CharacterModel::Part> part = character->parts[i];
        if (i < JOINTS || part->parent != -1) {
            present[part->joint] = true;
        }
    }
    attachments = gd::make<AttachmentDynamics>(fighter_tables, pose_tables, solver, character);
    for (gd::Int i = 0; i < JOINT_BLOCKS; ++i) {
        joints.append(gd::make<JointFrame>());
    }
    draw_joints = joints.duplicate();
    for (gd::Int i = 0; i < JOINTS; ++i) {
        local_mats.append(Fx::identity());
    }
    for (gd::Int i = 0; i < JOINTS - 1; ++i) {
        prev_local_mats.append(gd::PI32{0, 0, 0, 0, 0, 0, 0, 0, 0});
        blend_delta.append(gd::PI32{0, 0, 0, 0, 0, 0, 0, 0, 0});
    }
    for (gd::Int i = 0; i < 4; ++i) {
        attach_local.append(Fx::identity());
    }
}

void FighterBody::keep_matrices(const gd::Ref<FighterBody>& old) {
    root_mat = old->root_mat.duplicate();
    root = gd::make<JointFrame>(old->root->rot.duplicate(), old->root->t.duplicate());
    for (gd::Int i = 0; i < JOINT_BLOCKS; ++i) {
        joints[i] = gd::make<JointFrame>(old->joints[i]->rot.duplicate(), old->joints[i]->t.duplicate());
        draw_joints[i] = gd::make<JointFrame>(old->draw_joints[i]->rot.duplicate(), old->draw_joints[i]->t.duplicate());
    }
    for (gd::Int i = 0; i < JOINTS; ++i) {
        local_mats[i] = old->local_mats[i].duplicate();
    }
    for (gd::Int i = 0; i < JOINTS - 1; ++i) {
        prev_local_mats[i] = old->prev_local_mats[i].duplicate();
        blend_delta[i] = old->blend_delta[i].duplicate();
    }
    for (gd::Int i = 0; i < attach_local.size(); ++i) {
        attach_local[i] = old->attach_local[i].duplicate();
    }
    ogre_sway = old->ogre_sway;
    gon_gaze = old->gon_gaze;
}

gd::Int FighterBody::scale_base() const {
    return model->scale_fixed();
}

}  // namespace t3
