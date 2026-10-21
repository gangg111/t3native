// remake/core/combat/hit_slot.gd
// One of a defender's two 0x2C-byte hit records (fighter +0x13C): the contact found by HitTest
// and its outcome from HitApply (combat.md#classification-and-damage).
#include "core/combat/hit_slot.h"

namespace t3 {

// HitClearSlots: the outcome (+0x1C ... +0x28), not the contact geometry.
void HitSlot::clear() {
    base_damage = 0;
    damage = 0;
    attacker = 0;
    used = 0;
    guarded = 0;
    chip = 0;
    hit = 0;
    counter = 0;
    close = 0;
    airborne = 0;
    no_damage = 0;
}

// HitBestSlot's severity: the summed weights of the outcome flags.
gd::Int HitSlot::score() const {
    return no_damage + 2 * guarded + 3 * chip + 4 * close + 5 * hit + 6 * counter + 7 * airborne;
}

}  // namespace t3
