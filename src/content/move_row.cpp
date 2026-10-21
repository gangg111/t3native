// remake/content/move_row.gd
#include "content/move_row.h"

#include "content/motion_bank.h"
#include "core/math/fx.h"

namespace t3 {

gd::Ref<MoveRow> MoveRow::link(const gd::Ref<MotionBank>& owner, gd::Int row_index, const gd::Ref<MotionBank>& common,
                               const gd::PI32& builtins) {
    gd::PI64 w = owner->moves[row_index];
    auto r = gd::make<MoveRow>();
    r->bank = owner;
    r->index = row_index;
    gd::Int anim_word = w[0];
    if (anim_word >= COMMON_FLAG) {
        r->anim_bank = common;
        r->anim = anim_word - COMMON_FLAG;
    } else {
        r->anim_bank = owner;
        r->anim = anim_word;
    }
    r->length = r->anim_bank->anim_first_byte(r->anim);
    r->state = w[1];
    r->attack = gd::Int(w[2]) & 0xFFFF;
    r->attack_word = gd::Int(w[2]) & 0xFFFFFFFFLL;
    r->ai_hints = (gd::Int(w[2]) >> 16) & 0xFF;
    r->branches = w[3];
    r->stance_slot = gd::Int(w[4]) & 0xFFFF;
    r->end_turn = Fx::s16(gd::Int(w[4]) >> 16);
    r->step = Fx::s16(gd::Int(w[5]) >> 16);
    r->air_first = (gd::Int(w[6]) >> 8) & 0xFF;
    r->air_last = (gd::Int(w[6]) >> 16) & 0xFF;
    r->hold_frame = (gd::Int(w[6]) >> 24) & 0xFF;
    r->sounds = gd::Int(w[7]) == NO_LIST ? -1 : gd::Int(w[7]);
    r->events = gd::Int(w[8]) == NO_LIST ? -1 : 2 * gd::Int(w[8]);
    r->flags = w[9];
    gd::Int descriptor = Fx::w32(w[10]);
    if (descriptor >= 0) {
        r->builtin_attack = builtins.find(descriptor);
        if (r->builtin_attack < 0) {
            r->attack_record = descriptor - 4;
        }
    }
    r->hit_freeze = gd::Int(w[11]) & 0xFF;
    r->active_first = (gd::Int(w[11]) >> 8) & 0xFF;
    r->active_last = (gd::Int(w[11]) >> 16) & 0xFF;
    r->body_profile = gd::Int(w[12]) & 0xFFFF;
    r->reaction = (gd::Int(w[12]) >> 16) & 0xFFFF;
    r->close_reaction = gd::Int(w[13]) & 0xFFFF;
    // MoveTotalDamage: the u16 damage word plus the arguments of the 0x0Bxx events.
    gd::Int damage = gd::Int(w[5]) & 0xFFFF;
    if (r->events >= 0) {
        gd::Int i = r->events;
        while (gd::Int(owner->events[i]) != 0) {
            gd::Int command = owner->events[i + 1];
            if (command >> 8 == THROW_DAMAGE_EVENT) {
                damage += command & 0xFF;
            }
            i += 2;
        }
    }
    r->damage_word = damage & 0xFFFF;
    return r;
}

std::string MoveRow::_to_string() const {
    return bank->name + ":" + gd::str(index);
}

}  // namespace t3
