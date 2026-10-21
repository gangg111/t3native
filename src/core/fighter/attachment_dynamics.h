#pragma once
// remake/core/fighter/attachment_dynamics.gd
// The swinging costume attachments (draw parts 18–21: hair, belts, scarves), FUN_80037F10.
//
// Each frame an attachment with a dynamics record turns its rest orientation towards a blend
// of three directions, seen in its rest frame: towards where its tip was on the previous frame
// (inertia), downwards (gravity, off while the tip touches the floor) and its rest direction.
// The blend becomes a yaw and a pitch through `ratan2` with per-record axis signs and scales,
// a few costumes clamp or cancel angles, and mode 1 aims at a point on another joint instead
// when that point lies further along the record's axis. The local rotation is then
// `rest · Euler(0, yaw, pitch)`.
#include "gd/gd.h"

namespace t3 {

class CharacterModel;
class FighterTables;
class JointFrame;
class PoseSolver;
class PoseTables;

class AttachmentDynamics {
public:
    static constexpr gd::Int FIRST_PART = 18;
    static constexpr gd::Int COUNT = 4;
    static inline const gd::Arr<gd::Int> REST_ONLY_SLOTS{0x20, 0x21};   // the first two attachments keep their local rotation
    // Record fields (s16).
    static constexpr gd::Int LIMIT = 0;
    static constexpr gd::Int INERTIA = 2;
    static constexpr gd::Int GRAVITY = 3;
    static constexpr gd::Int LENGTH = 4;
    static constexpr gd::Int BALANCE = 5;
    static constexpr gd::Int MODE = 6;
    static constexpr gd::Int TARGET_JOINT = 7;
    static constexpr gd::Int TARGET_OFFSET = 8;
    static constexpr gd::Int TARGET_LIMIT = 11;
    static constexpr gd::Int MODE_TARGET = 1;
    static constexpr gd::Int WEIGHT_ONE = 0x100;
    // Costume slots with special handling in FUN_80037F10 / FUN_80038684.
    static constexpr gd::Int SLOT_NO_YAW_0 = 0x0E;
    static constexpr gd::Int SLOT_NO_YAW_PARTS = 0x0F;
    static constexpr gd::Int SLOT_NO_YAW_2 = 0x1B;
    static constexpr gd::Int SLOT_FLIP = 0x26;
    static constexpr gd::Int SLOT_CLAMP_A = 0x1A;
    static inline const gd::Arr<gd::Int> SLOT_CLAMP_B{0x24, 0x29};
    static constexpr gd::Int FLIP_LIMIT = 2;
    static constexpr gd::Int FLIP_TARGET_LIMIT = 8;
    static constexpr gd::Int MIN_PITCH = 0x800;
    static inline const gd::Arr<gd::Int> LEAN_JOINTS{16, 13};   // FUN_800388C0: the direction from joint 13 to joint 16
    static constexpr gd::Int LEAN_MIN = 0x4000;
    static constexpr gd::Int LEAN_LIFT = 0x40;

    gd::Ref<FighterTables> tables;
    gd::Ref<PoseSolver> solver;
    gd::Ref<PoseTables> pose_tables;
    gd::Int slot = 0;
    gd::Arr<gd::PI32> rest;
    gd::Arr<gd::PI32> records;
    gd::Arr<gd::PI32> local;
    // Array[bool]: uint8_t elements, because std::vector<bool> has no bool& for Arr::operator[].
    gd::Arr<uint8_t> started{0, 0, 0, 0};
    gd::Int target_yaw = 0;                         // +0x1388
    gd::Int target_pitch = 0;                       // +0x138C

    AttachmentDynamics(const gd::Ref<FighterTables>& fighter_tables, const gd::Ref<PoseTables>& poses,
                       const gd::Ref<PoseSolver>& pose_solver, const gd::Ref<CharacterModel>& model);

    // The local rotation of attachment `i` this frame. `parent` is its parent joint (this frame),
    // `previous` its own joint on the previous frame, `offset` its local translation, `joints`
    // the fighter's joints of this frame (0–17 are already built).
    gd::PI32 update(gd::Int i, const gd::Ref<JointFrame>& parent, const gd::Ref<JointFrame>& previous,
                    const gd::PI32& offset, const gd::Arr<gd::Ref<JointFrame>>& joints);

    // FUN_80038684: the target angles towards a point fixed to another joint (mode 1); true when
    // costume slot 0x26 flips to its other limit record.
    bool _aim(const gd::PI32& p, const gd::PI32& to_rest, const gd::PI32& pivot,
              const gd::Arr<gd::Ref<JointFrame>>& joints);

    // FUN_800388C0: the first attachment of costume slots 0x24 and 0x29 points at a spot 128 units
    // from joint 13 towards joint 16, lifted by 64, when those joints are far enough apart.
    gd::PI32 _lean(const gd::PI32& to_rest, const gd::Ref<JointFrame>& previous,
                   const gd::Arr<gd::Ref<JointFrame>>& joints, const gd::PI32& out);
};

}  // namespace t3
