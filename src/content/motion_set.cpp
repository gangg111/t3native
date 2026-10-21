// remake/content/motion_set.gd
#include "content/motion_set.h"

#include "content/motion_bank.h"
#include "content/move_row.h"

namespace t3 {

MotionSet::Ref::Ref(gd::Ref<MotionBank> in_bank, gd::Int in_value) {
    bank = std::move(in_bank);
    value = in_value;
}

MotionSet::MotionSet(gd::Ref<MotionBank> own_bank, gd::Ref<MotionBank> common_bank) {
    own = std::move(own_bank);
    common = std::move(common_bank);
}

gd::Ref<MotionSet::Ref> MotionSet::move_for_slot(gd::Int slot) const {
    gd::Int entry = own->move_index[slot];
    if (entry == MotionBank::EMPTY_SLOT) {
        return nullptr;
    }
    if (entry >= MotionBank::COMMON_FLAG) {
        return gd::make<Ref>(common, entry - MotionBank::COMMON_FLAG);
    }
    return gd::make<Ref>(own, entry);
}

gd::Ref<MotionSet::Ref> MotionSet::anim_of(const gd::Ref<Ref>& move) const {
    gd::Int word = move->bank->moves[move->value][MotionBank::ROW_ANIM];
    if (word >= MotionBank::COMMON_FLAG) {
        return gd::make<Ref>(common, word - MotionBank::COMMON_FLAG);
    }
    return gd::make<Ref>(move->bank, word);
}

gd::Ref<MotionSet::Ref> MotionSet::anim_for_slot(gd::Int slot) const {
    gd::Ref<Ref> move = move_for_slot(slot);
    if (move == nullptr) {
        return nullptr;
    }
    gd::Ref<Ref> anim = anim_of(move);
    return anim->bank->has_anim(anim->value) ? anim : nullptr;
}

gd::Ref<MoveRow> MotionSet::row_for_slot(gd::Int slot) const {
    gd::Ref<Ref> move = move_for_slot(slot);
    if (move == nullptr) {
        return nullptr;
    }
    return MoveRow::link(move->bank, move->value, common, gd::PI32());
}

}  // namespace t3
