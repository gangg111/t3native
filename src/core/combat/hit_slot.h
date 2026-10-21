#pragma once
// remake/core/combat/hit_slot.gd
// One of a defender's two 0x2C-byte hit records (fighter +0x13C): the contact found by HitTest
// and its outcome from HitApply (combat.md#classification-and-damage).
#include "gd/gd.h"

namespace t3 {

class HitSlot {
public:
    gd::PI32 point{0, 0, 0};       // +0x00 the segment end at contact
    gd::PI32 direction{0, 0, 0};   // +0x10 end - start (s16)
    gd::Int zone = 0;              // +0x18 hurt cylinder
    gd::Int base_damage = 0;       // +0x1C the attacker's damage (u16)
    gd::Int damage = 0;            // +0x1E damage of the hit (s16)
    gd::Int attacker = 0;          // +0x20
    gd::Int used = 0;              // +0x21
    gd::Int guarded = 0;           // +0x22
    gd::Int chip = 0;              // +0x23
    gd::Int hit = 0;               // +0x24
    gd::Int counter = 0;           // +0x25
    gd::Int close = 0;             // +0x26
    gd::Int airborne = 0;          // +0x27
    gd::Int no_damage = 0;         // +0x28

    // HitClearSlots: the outcome (+0x1C ... +0x28), not the contact geometry.
    void clear();

    // HitBestSlot's severity: the summed weights of the outcome flags.
    gd::Int score() const;
};

}  // namespace t3
