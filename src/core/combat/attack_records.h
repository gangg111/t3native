#pragma once
// remake/core/combat/attack_records.gd
// Access to a move's attack descriptor (move +0x28): a record of section 9 of the move's bank,
// or one of the 13 built-in descriptors of the executable. A descriptor starts with the joint
// pairs (a0, b0, a1, b1); a record continues with a big-endian layout word and the baked points
// (divmot-banks.md#attack-records-section-9). The game reads past a built-in descriptor into the
// bytes after it, so those come from `FightTables.builtin_blob` as they lie in memory.
#include "gd/gd.h"

namespace t3 {

class MoveRow;
class FightTables;
class FighterState;

class AttackRecords {
public:
    // The bytes a descriptor pointer reaches and its offset in them, or an empty array for none.
    static gd::PBytes bytes_of(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t);

    static gd::Int offset_of(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t);

    static bool has_descriptor(const gd::Ref<MoveRow>& move);

    // The descriptor's first joint (a0), 0 without one (the game reads address 0 then, which
    // holds a small value on the console and in the harness).
    static gd::Int first_joint(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t);

    // The first six bytes: a0, b0, a1, b1 and the layout word's two bytes.
    static gd::PBytes header(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t);

    // AttackUsesJoint (0x8002EDA0): the running move's descriptor names the joint (0: any).
    static bool uses_joint(const gd::Ref<FighterState>& f, gd::Int joint, const gd::Ref<FightTables>& t);
};

}  // namespace t3
