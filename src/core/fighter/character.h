#pragma once
// remake/core/fighter/character.gd
// Character ids (`charId`, fighter +0x18; costume key = id · 4 + costume): the selection index
// the unlock, clear and costume masks use bit by bit (modes.md#unlocks).
#include "gd/gd.h"

namespace t3 {

class Character {
public:
    enum : gd::Int {
        PAUL = 0,
        LAW = 1,
        LEI = 2,
        KING = 3,
        YOSHIMITSU = 4,
        NINA = 5,
        HWOARANG = 6,
        XIAOYU = 7,
        EDDY = 8,                   // costume 2: Tiger
        JIN = 9,
        JULIA = 10,
        KUMA = 11,                  // costume 1: Panda
        BRYAN = 12,
        HEIHACHI = 13,
        OGRE = 14,
        MOKUJIN = 15,
        GUN_JACK = 16,
        GON = 17,
        ANNA = 18,
        DOCTOR_B = 19,
        TRUE_OGRE = 20,
        FORCE_ENEMY = 21,           // Tekken Force's enemies
    };
};

}  // namespace t3
