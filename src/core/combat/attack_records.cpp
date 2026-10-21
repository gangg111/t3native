// remake/core/combat/attack_records.gd
// Access to a move's attack descriptor (move +0x28): a record of section 9 of the move's bank,
// or one of the 13 built-in descriptors of the executable.
#include "core/combat/attack_records.h"

#include "content/fight_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/fighter/fighter_state.h"

namespace t3 {

// The bytes a descriptor pointer reaches and its offset in them, or an empty array for none.
gd::PBytes AttackRecords::bytes_of(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t) {
    if (move->builtin_attack >= 0) {
        return t->builtin_blob;
    }
    return move->bank->attacks;
}

gd::Int AttackRecords::offset_of(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t) {
    if (move->builtin_attack >= 0) {
        return t->builtin_attack_offsets[move->builtin_attack];
    }
    return move->attack_record;
}

bool AttackRecords::has_descriptor(const gd::Ref<MoveRow>& move) {
    return move->builtin_attack >= 0 || move->attack_record >= 0;
}

// The descriptor's first joint (a0), 0 without one (the game reads address 0 then, which
// holds a small value on the console and in the harness).
gd::Int AttackRecords::first_joint(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t) {
    if (!has_descriptor(move)) {
        return 0;
    }
    gd::PBytes bytes = bytes_of(move, t);
    return bytes[offset_of(move, t)];
}

// The first six bytes: a0, b0, a1, b1 and the layout word's two bytes.
gd::PBytes AttackRecords::header(const gd::Ref<MoveRow>& move, const gd::Ref<FightTables>& t) {
    if (!has_descriptor(move)) {
        return gd::PBytes{0, 0, 0, 0, 0, 0};
    }
    gd::Int at = offset_of(move, t);
    return bytes_of(move, t).slice(at, at + 6);
}

// AttackUsesJoint (0x8002EDA0): the running move's descriptor names the joint (0: any).
bool AttackRecords::uses_joint(const gd::Ref<FighterState>& f, gd::Int joint, const gd::Ref<FightTables>& t) {
    if (joint == 0) {
        return true;
    }
    gd::PBytes h = header(f->pose_move, t);
    for (gd::Int i = 0; i < 4; ++i) {
        if (gd::Int(h[i]) == joint) {
            return true;
        }
    }
    return false;
}

}  // namespace t3
