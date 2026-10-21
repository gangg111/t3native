#pragma once
// remake/content/move_row.gd
// One 56-byte move row of a motion bank, decoded and linked as `DivmotLinkBank` (0x8002D068)
// links it: the animation (own or common bank), its length byte, the damage with the throw
// damage of its `0x0B` events, and the event, sound and branch lists and attack record of the
// row's own bank. Field meanings: moves.md#move-row-fields-used-at-run-time.
#include <string>

#include "gd/gd.h"

namespace t3 {

class MotionBank;

class MoveRow {
public:
    static constexpr gd::Int COMMON_FLAG = 0x40000000;
    static constexpr gd::Int NO_LIST = 0xFFFFFFFFLL;
    static constexpr gd::Int THROW_DAMAGE_EVENT = 0x0B;
    static constexpr gd::Int DAMAGE_MASK = 0x3FFF;

    gd::Ref<MotionBank> bank;        // the bank the row belongs to
    gd::Int index = 0;               // row number in its bank
    gd::Ref<MotionBank> anim_bank;   // the bank of the animation stream (own or common)
    gd::Int anim = 0;                // stream offset in section 6 of `anim_bank`
    gd::Int state = 0;               // +0x04
    gd::Int attack = 0;              // +0x08
    gd::Int ai_hints = 0;            // +0x0A
    gd::Int attack_word = 0;         // +0x08 as a u32: the attack, the AI hints (bits 16-23) and byte +0x0B
    gd::Int branches = -1;           // +0x0C: first branch row in `bank.branches`
    gd::Int stance_slot = 0;         // +0x10
    gd::Int end_turn = 0;            // +0x12 (s16)
    gd::Int damage_word = 0;         // +0x14: damage (bits 0-13, throw damage added) and class (14-15)
    gd::Int step = 0;                // +0x16 (s16)
    gd::Int length = 0;              // +0x18: the stream's first byte
    gd::Int air_first = 0;           // +0x19
    gd::Int air_last = 0;            // +0x1A
    gd::Int hold_frame = 0;          // +0x1B
    gd::Int sounds = -1;             // +0x1C: first word in `bank.sounds`, -1 none
    gd::Int events = -1;             // +0x20: first word in `bank.events`, -1 none
    gd::Int flags = 0;               // +0x24
    gd::Int attack_record = -1;      // +0x28: byte offset in `bank.attacks`, -1 none or built in
    gd::Int builtin_attack = -1;     // +0x28: index of a built-in descriptor (fight tables), -1 none
    gd::Int hit_freeze = 0;          // +0x2C
    gd::Int active_first = 0;        // +0x2D
    gd::Int active_last = 0;         // +0x2E
    gd::Int body_profile = 0;        // +0x30
    gd::Int reaction = 0;            // +0x32
    gd::Int close_reaction = 0;      // +0x34

    // Decodes row `row_index` of `owner` (words as converted) and links it; `common` is the common
    // bank (the owner itself for the common bank), `builtins` the built-in descriptor ids.
    static gd::Ref<MoveRow> link(const gd::Ref<MotionBank>& owner, gd::Int row_index, const gd::Ref<MotionBank>& common,
                                 const gd::PI32& builtins);

    gd::Int damage() const { return damage_word & DAMAGE_MASK; }
    gd::Int attack_class() const { return damage_word >> 14; }
    std::string _to_string() const;
};

}  // namespace t3
