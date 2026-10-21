#pragma once
// remake/presentation/fighter/arcade_attachments.gd
// The joints of an arcade model's own attachments (hair, sashes, tails, skirts:
// CharacterModel.arcade_attachments), built after each simulation step from the fight's
// skeleton. The fight keeps the PlayStation's skeleton and attachments for its own state; these
// joints only carry the arcade mesh's faces.
//
// Each attachment hangs from its parent joint at its offset, turned by its rest angles plus its
// record's offsets (FUN_8019b634). A swinging one (FUN_8019b6d8) then turns towards a blend of
// the direction to its smoothed tip of the previous step, gravity (with the helicopter stage's
// wind) and its rest direction, seen in its rest frame, as a yaw and a pitch; mode 1 aims at a
// point on another joint when that point lies further along the record's axis, mode 2 keeps the
// tip off a line fixed to another joint, and costume slot 0x24's second attachment leans
// towards a spot between joints 13 and 16. Verified case by case against the arcade's routine
// (tools/research/arcade_attachment_cases.py, tests/presentation/test_arcade_attachments.gd).
#include <cstdint>
#include <string>

#include "content/character_model.h"
#include "gd/gd.h"

namespace t3 {

class FighterTables;
class JointFrame;
class PoseSolver;

class ArcadeAttachments {
public:
    static inline const std::string TABLES_FILE = "characters/arcade_attachments.json";
    static constexpr gd::Int FIRST_ATTACHMENT_ROW = 18;
    static inline const gd::Arr<gd::Int> REST_ONLY_SLOTS{0x20, 0x21};   // their first two attachments keep identity
    static constexpr gd::Int REST_ONLY_COUNT = 2;
    // Record fields (s16 words; the first three are the rest angle offsets).
    static constexpr gd::Int AXIS = 3;             // the yaw and pitch's axis record (`limits`)
    static constexpr gd::Int SMOOTHING = 4;        // the tip moves 1/n of the way each step
    static constexpr gd::Int TOWARDS = 5;          // weight of the tip's direction (of WEIGHT_ONE)
    static constexpr gd::Int REST = 6;             // weight of the rest direction ... and of gravity (the rest)
    static constexpr gd::Int LENGTH = 7;           // the tip's distance along the attachment
    static constexpr gd::Int GRAVITY = 8;          // the part of the down direction that is down (the rest is wind)
    static constexpr gd::Int MODE = 9;
    static constexpr gd::Int TARGET_JOINT = 10;
    static constexpr gd::Int TARGET_POINT = 11;    // three words: a point in the target joint
    static constexpr gd::Int TARGET_AXIS = 14;     // mode 1: the target angles' axis record; mode 2: the line's reach
    static constexpr gd::Int MODE_TARGET = 1;
    static constexpr gd::Int MODE_LINE = 2;
    static constexpr gd::Int WEIGHT_ONE = 0x100;
    static constexpr gd::Int SLOT_FLIP = 0x26;     // mode 1 flips to FLIP_AXIS behind the target
    static constexpr gd::Int FLIP_AXIS = 0x28;
    static constexpr gd::Int SLOT_CLAMP_A = 0x1A;  // clamps 0-1 on every attachment
    static constexpr gd::Int SLOT_CLAMP_B = 0x24;  // clamps 2-3 on the first four; the lean on attachment 1
    static constexpr gd::Int CLAMP_B_COUNT = 4;
    static constexpr gd::Int LEAN_ATTACHMENT = 1;
    static constexpr gd::Int LEAN_FROM = 13;
    static constexpr gd::Int LEAN_TO = 16;
    static constexpr gd::Int LEAN_MIN = 0x2400;    // squared distance below which there is no lean
    static constexpr gd::Int LEAN_REACH = 0x60;
    static constexpr gd::Int LEAN_LIFT = 0x40;

    // TODO: Assets (remake/platform/assets.gd) is not ported: the imported root Assets.path reads
    // from, set by the owner (the app, the tests).
    static inline std::string assets_root = "";

    static inline gd::Ref<PoseSolver> _solver;
    static inline gd::Arr<gd::PI32> _limits;
    static inline gd::PI32 _clamps;
    static inline gd::Ref<FighterTables> _fighter_tables;

    gd::Ref<CharacterModel> model;
    gd::PI32 wind{0, 0, 0};                        // FUN_8019c738's wind (x, y, z), the helicopter stage's
    gd::Arr<uint8_t> _started;                     // Array[bool]
    gd::Arr<gd::PI32> _tips;
    gd::Int _target_yaw = 0;                       // mode 1's angles (fighter +0x1268 / +0x126C)
    gd::Int _target_pitch = 0;
    gd::Arr<gd::Ref<JointFrame>> _previous;        // every joint of the last step
    gd::PI32 _joints;                              // the joints the model's attachments hang from

    explicit ArcadeAttachments(const gd::Ref<CharacterModel>& character);

    // Starts again from the rest rotations (FUN_8019b634, when a model is set up).
    void reset();
    // Whether the model has an attachment on `joint`.
    bool has_joint(gd::Int joint) const;
    // The Euler conversion of the game's tables (PoseSolver.euler_to_matrix), loaded once.
    static gd::Ref<PoseSolver> solver();
    static gd::Ref<FighterTables> fighter_tables();
    // The arcade's axis records (0x801FD170: six signed bytes each) and clamps (0x801FD2F0).
    static void load_tables();
    // The skeleton's `joints` followed by the attachments' joints, CharacterModel.JOINT_COUNT in all
    // (identity where the model has no attachment).
    gd::Arr<gd::Ref<JointFrame>> update(const gd::Arr<gd::Ref<JointFrame>>& joints);
    gd::PI32 _local(const gd::Ref<CharacterModel::ArcadeAttachment>& a, const gd::Ref<JointFrame>& previous,
                    const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints);
    // FUN_8019b6d8 for a started swinging attachment `i`: its local rotation this step, from its
    // rest `angles`, `record`, its own joint of the last step, its parent's and the other joints
    // of this step, by joint. Updates the smoothed tip and mode 1's target angles.
    gd::PI32 swing(gd::Int slot, gd::Int i, const gd::PI32& angles, const gd::PI32& record, const gd::Ref<JointFrame>& previous,
                   const gd::Ref<JointFrame>& parent, const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints);
    // Mode 1's target angles (FUN_8019bed0): towards the record's point on the target joint, seen
    // in the rest frame; true when costume slot 0x26 flips its axis (the point is behind).
    bool _aim(gd::Int slot, const gd::PI32& record, const gd::PI32& to_rest, const gd::PI32& pivot,
              const gd::Ref<JointFrame>& target);
    // Mode 2 (FUN_8019c15c): the direction `v` (scaled by the tip's length) is kept off the line
    // through the record's point on the target joint along the joint's x axis: within the reach it
    // is pushed out to the reach.
    gd::PI32 _line(const gd::PI32& record, const gd::PI32& to_rest, const gd::PI32& v, const gd::PI32& pivot,
                   const gd::Ref<JointFrame>& target);
    // FUN_8019c598: costume slot 0x24's lean towards a spot LEAN_REACH from joint 13 towards
    // joint 16, lifted by LEAN_LIFT, when the joints are far enough apart.
    gd::PI32 _lean(const gd::PI32& to_rest, const gd::Ref<JointFrame>& previous, const gd::Arr<gd::Ref<JointFrame>>& joints,
                   const gd::PI32& v);
    // The joint of an arcade model row (rows 18-23 are the attachments, from ARCADE_JOINT).
    static gd::Int joint_of_row(gd::Int row);
    // C's signed division by 256 (towards zero), as the compiled `(x + 0xFF) >> 8` for negatives.
    static gd::Int _shift8(gd::Int x);
    static gd::Int _shift12(gd::Int x);
};

}  // namespace t3
