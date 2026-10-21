#pragma once
// remake/content/motion_set.gd
// A fighter's motion bank together with the common bank `divmot99`: resolves move slots
// and animation words that point into the common bank (`MoveLookup`).
#include "gd/gd.h"

namespace t3 {

class MotionBank;
class MoveRow;

class MotionSet {
public:
    // A move row or an animation stream in one of the two banks.
    class Ref {
    public:
        gd::Ref<MotionBank> bank;
        gd::Int value = 0;   // row index or stream offset

        Ref(gd::Ref<MotionBank> in_bank, gd::Int in_value);
    };

    gd::Ref<MotionBank> own;
    gd::Ref<MotionBank> common;

    MotionSet(gd::Ref<MotionBank> own_bank, gd::Ref<MotionBank> common_bank);

    // The move row of a slot, or null for an empty slot.
    gd::Ref<Ref> move_for_slot(gd::Int slot) const;
    // The animation stream of a move row.
    gd::Ref<Ref> anim_of(const gd::Ref<Ref>& move) const;
    // The animation of a slot's move, or null when the slot is empty or has no stream.
    gd::Ref<Ref> anim_for_slot(gd::Int slot) const;
    // The decoded move row of a slot (a fresh MoveRow, not the fight's linked rows), or null.
    gd::Ref<MoveRow> row_for_slot(gd::Int slot) const;
};

}  // namespace t3
