#pragma once
// remake/core/combat/attack_word.gd
// The common attack words of a move (`attack`, combat.md#attack-levels-postures-and-guards):
// the high byte is the level, the low bits the postures it hits. The second mid, low and
// unblockable words are rarer variants the AI and Tekken Ball treat alike.
#include "gd/gd.h"

namespace t3 {

class AttackWord {
public:
    enum : gd::Int {
        LOW = 0x10F,                    // hits all postures, blocked by the crouching guard
        MID = 0x217,                    // hits all postures, blocked by the standing guard
        MID_2 = 0x31F,
        HIGH = 0x412,                   // hits standing fighters only (ducked by crouching)
        LOW_2 = 0x51F,
        UNBLOCKABLE = 0x607,            // also ground hits
        UNBLOCKABLE_2 = 0x706,
        THROW = 0x800,                  // connects only through the throw system
    };
};

}  // namespace t3
