#pragma once
// remake/core/fighter/fighter_skeleton.gd
// A fighter's joints for one frame: pose vector → local rotations (PoseSolver) → root
// (RootUpdate) → world joint frames (FighterComposeJoints).
//
// Rules: docs/research/formats/animation.md#pose-to-matrices and
// docs/research/code/moves.md#root-motion.
// The costume attachments 18–21 swing with AttachmentDynamics (FUN_80037F10) when fighter
// tables are given; without them, or without a dynamics record, they follow their parent joint
// rigidly at the KMD offset.
// The fight builds its joints with FighterAnimation (motion blending, head look-at, True
// Ogre's sway); this skeleton serves the attract demonstration and the model viewer.
#include "content/character_model.h"   // CharacterModel::PART_COUNT
#include "gd/gd.h"

namespace t3 {

class AttachmentDynamics;
class FighterTables;
class JointFrame;
class PoseSolver;
class PoseTables;

class FighterSkeleton {
public:
    static constexpr gd::Int JOINTS = 18;                  // joints built from the pose vector
    static constexpr gd::Int ALL_JOINTS = CharacterModel::PART_COUNT;

    gd::Ref<PoseSolver> solver;
    gd::Ref<PoseTables> tables;
    gd::Arr<gd::PI32> offsets;
    gd::PI32 parents;
    gd::Int scale = 0;                                     // model scale, 4.12
    gd::Arr<gd::PI32> local_slots;                         // persist between frames, as in the game
    gd::Ref<JointFrame> root;                              // created in the constructor
    gd::Arr<gd::Ref<JointFrame>> joints;
    gd::Ref<AttachmentDynamics> attachments;

    FighterSkeleton(const gd::Ref<PoseTables>& pose_tables, const gd::Ref<CharacterModel>& model,
                    const gd::Ref<FighterTables>& fighter_tables = nullptr);

    // Root displacement of a pose vector (AnimDecodeRoot × model scale, AnimScaleRoot).
    gd::PI32 root_displacement(const gd::PI32& pose) const;

    // Poses the skeleton on the ground at `anchor` (the move's anchor, fighter posX/Y/Z)
    // with the given facing and heading (16-bit angles), no tilt.
    void update(const gd::PI32& pose, const gd::PI32& anchor, gd::Int facing, gd::Int heading);

    // FUN_8003A6B4: Rx·Ry·Rz from three 16-bit angles with the game's GTE rounding.
    gd::PI32 root_rotation(gd::Int x, gd::Int y, gd::Int z) const;

    static gd::PI32 rotation_xyz(gd::Int x, gd::Int y, gd::Int z, const gd::Ref<PoseTables>& tables);
};

}  // namespace t3
