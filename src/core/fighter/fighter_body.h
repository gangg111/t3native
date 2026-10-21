#pragma once
// remake/core/fighter/fighter_body.gd
// A fighter's skeleton as the fight code keeps it: the model's joint offsets and parents
// (FighterSetupParts, KmdRelocate), the 18 local joint matrices of the pose, their copy of the
// previous frame and the blend deltas, and the world joints (24 blocks: 22 draw parts and two
// spare slots for weapon points).
#include "core/math/fx.h"
#include "gd/gd.h"

namespace t3 {

class AttachmentDynamics;
class CharacterModel;
class FighterTables;
class JointFrame;
class PoseSolver;
class PoseTables;

class FighterBody {
public:
    static constexpr gd::Int JOINTS = 18;
    static constexpr gd::Int PARTS = 22;
    static constexpr gd::Int JOINT_BLOCKS = 24;

    // The head look-at state of a player (animation.md#procedural-head-and-eyes).
    class LookAt {
    public:
        gd::PI32 current{0, 0, 0, 0};   // +0: angles (x, y, z) and a spare word
        gd::PI32 target{0, 0, 0, 0};    // +8
        gd::Int active = 0;             // +0x10
        gd::Int snap = 1;               // +0x11
        gd::Int hold = 0;               // +0x12
    };

    gd::Ref<CharacterModel> model;
    gd::Arr<gd::PI32> offsets;
    gd::PI32 parents;
    // Array[bool]: uint8_t elements, because std::vector<bool> has no bool& for Arr::operator[].
    gd::Arr<uint8_t> present;                   // draw parts with a matrix
    gd::Ref<AttachmentDynamics> attachments;
    gd::PI32 root_mat = Fx::identity();         // +0xF54
    gd::Ref<JointFrame> root;                   // +0x8B0 (created in the constructor)
    gd::Arr<gd::Ref<JointFrame>> joints;        // +0x8F4
    // The joints as FighterComposeJoints built them. The parts are drawn from these (the second
    // matrix of each block, +0x20, made while composing), so the weapon points CollisionShapesUpdate
    // writes into the spare blocks afterwards do not move Mokujin's stick.
    gd::Arr<gd::Ref<JointFrame>> draw_joints;
    // Part 18's draw flag (+0x7C0). FUN_800363B0 sets it every frame for Mokujin: his stick
    // (attachment 18) shows only while he fights with Yoshimitsu's moves (bank type 4).
    bool stick_shown = true;
    gd::Arr<gd::PI32> local_mats;               // +0xF74 rotations (translations: offsets)
    gd::Arr<gd::PI32> prev_local_mats;          // +0x13B0 slots 1–17
    gd::Arr<gd::PI32> blend_delta;              // +0x15F0 slots 1–17
    gd::Arr<gd::PI32> attach_local;             // +0x11B4 attachments' local rotations
    gd::PI32 pose_vector;                       // the decoded pose of the running frame (0x800A8AB0)
    gd::Int decoded_root_dy = 0;                // 0x800A8A52: the unscaled root dy of the running frame
    gd::Ref<LookAt> look_at;                    // 0x8009E920 per player (created in the constructor)
    gd::Int ogre_sway = 0;                      // +0x1294 True Ogre's sway counter
    gd::Int gon_gaze = 0;                       // GonEyesFollow's look direction of Gon's eyes (−17 to 17), shown by the view

    FighterBody(const gd::Ref<CharacterModel>& character, const gd::Ref<FighterTables>& fighter_tables,
                const gd::Ref<PoseTables>& pose_tables, const gd::Ref<PoseSolver>& solver);

    // A reload into the same record (FighterSetupParts on a loaded fighter) sets only the model's
    // offsets and parents: the matrices the record holds stay as they were.
    void keep_matrices(const gd::Ref<FighterBody>& old);

    gd::Int scale_base() const;
};

}  // namespace t3
