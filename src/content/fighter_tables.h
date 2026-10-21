#pragma once
// remake/content/fighter_tables.gd
// Per-costume tables of the fighter code, converted from the game executable
// (`imported/tables/fighter.json`).
#include <string>

#include "gd/gd.h"

namespace t3 {

class FighterTables {
public:
    static constexpr gd::Int NO_KEY = 0;

    gd::PI32 costume_keys;               // costume key (charId * 4 + costume) -> costume slot
    gd::PI32 costume_hand_shapes;        // costume slot -> default hand shape (FUN_800364E8)
    gd::Arr<gd::PI32> attachment_limits; // FUN_80037F10's six-int records
    gd::PI32 attachment_clamps;          // (max, min) for costume slot 0x1A, then 0x24 / 0x29
    gd::PI32 ratan;                      // libgte ratan2 table
    gd::PI32 rsqrt;                      // VectorNormal's reciprocal square roots
    gd::PI32 wing_open;                  // True Ogre's wing variants while they open (-1 ends)
    gd::PI32 wing_beat;                  // the variants of one wing beat (-1 ends)

    static gd::Ref<FighterTables> load_from(const std::string& path);

    // FUN_80036440: the first costume key that selects `slot`, or key 0 when none does
    // (the game then also sets the slot to that key's slot).
    gd::Int key_of_slot(gd::Int slot) const;
    // FUN_800364E8: the costume's own hand shape (move event 21), 0 beyond the table.
    gd::Int hand_shape(gd::Int slot) const;
};

}  // namespace t3
