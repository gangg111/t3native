// remake/core/fighter/fighter_hands.gd
// The hand and face channels of a fighter: `HandFaceCommand` (0x800346F8) sets a target shape
// and `FighterHandPoses` (0x80034BC4) moves towards it every animated frame and picks the
// vertex variant of each hand part (3dmk-models.md#vertex-variants).
#include "core/fighter/fighter_hands.h"

#include "content/fighter_tables.h"
#include "core/fighter/character.h"
#include "core/math/fx.h"

namespace t3 {

void FighterHands::command(gd::Int hands, gd::Int shape, gd::Int speed, gd::Int char_id) {
    if (shape >= FACE_OPEN) {
        if (shape - FACE_OPEN < 2) {
            face_speed = Fx::s16(speed);
            face = shape - FACE_OPEN;
        }
        return;
    }
    if (shape >= 8) {
        return;
    }
    if (char_id == Character::TRUE_OGRE) {
        if (speed == 0) {
            speed = 1;
        }
        gd::Int ogre = speed < 0 ? Fx::div_trunc(OGRE_LEVEL, 1 - speed) : Fx::div_trunc(OGRE_LEVEL, speed + 1);
        ogre_rate = 0;
        if (shape == 2 || shape == 3) {
            ogre_target = OGRE_LEVEL;
            if (ogre_level < OGRE_LEVEL) {
                ogre_rate = ogre;
            }
        } else {
            ogre_target = 0;
            if (ogre_level > 0) {
                ogre_rate = ogre;
            }
        }
        target[0] = shape == 2 ? OGRE_LEVEL : 0;
        rate[0] = ogre;
        return;
    }
    for (gd::Int c = 0; c < CHANNELS; ++c) {
        if ((hands & (c + 1)) == 0) {
            continue;
        }
        gd::Int value = shape == 0 ? 0 : (shape == 1 ? FIST : shape + FIST - 1);
        if (current[c] <= FIST && value <= FIST) {
            if (speed != 0) {
                rate[c] = speed < 0 ? Fx::div_trunc(FIST, 1 - speed) : Fx::div_trunc(FIST, speed + 1);
            }
        } else {
            current[c] = value;
            rate[c] = 0;
        }
        target[c] = value;
    }
}

void FighterHands::update(gd::Int char_id, gd::Int air_free, const gd::Ref<FighterTables>& tables) {
    if (char_id == Character::TRUE_OGRE) {
        _update_wings(air_free, tables);
        _approach(0);
        gd::Int level = ogre_level;
        if (level < ogre_target) {
            ogre_level = Fx::s16(level + ogre_rate);
            if (!(ogre_level < ogre_target)) {
                ogre_rate = 0;
                ogre_level = ogre_target;
            }
        } else if (ogre_target < level) {
            ogre_level = Fx::s16(level - ogre_rate);
            if (!(ogre_target < ogre_level)) {
                ogre_rate = 0;
                ogre_level = ogre_target;
            }
        }
        gd::Int v = Fx::s16(current[0]) >> 7;
        variant[0] = v != 0 ? v - 1 : 0;
        return;
    }
    for (gd::Int c = 0; c < CHANNELS; ++c) {
        _approach(c);
        variant[c] = variant_of(current[c]);
    }
}

void FighterHands::_approach(gd::Int c) {
    if (current[c] < target[c]) {
        current[c] = Fx::s16(current[c] + rate[c]);
        if (!(current[c] < target[c])) {
            rate[c] = 0;
            current[c] = target[c];
        }
    } else if (target[c] < current[c]) {
        current[c] = Fx::s16(current[c] - rate[c]);
        if (!(target[c] < current[c])) {
            rate[c] = 0;
            current[c] = target[c];
        }
    }
}

void FighterHands::_update_wings(gd::Int air_free, const gd::Ref<FighterTables>& tables) {
    if (tables == nullptr) {
        return;
    }
    if (wing_phase == 0) {
        if (air_free == 0) {
            if (wing_frame != 0) {
                wing_frame -= 1;
            }
        } else {
            wing_frame = (wing_frame + 2) & 0xFF;
            if (tables->wing_open[wing_frame] == -1) {
                wing_phase = 1;
                wing_frame = 0;
                wing_first = 1;
            }
        }
    } else if (wing_phase == 1) {
        gd::Int steps = 1;
        if (wing_first != 0) {
            steps = 2;
            wing_first = 0;
        }
        wing_frame = (wing_frame + steps) & 0xFF;
        if (tables->wing_beat[wing_frame] == -1) {
            if (air_free == 0) {
                wing_frame = 0x10;
                wing_phase = 0;
            } else {
                wing_frame = 0;
            }
        }
    }
    wing_variant = (wing_phase == 0 ? tables->wing_open : tables->wing_beat)[wing_frame];
}

gd::Int FighterHands::variant_of(gd::Int value) {
    if (value > FIST) {
        return value - (FIST - 3);
    }
    gd::Int v = value >> 7;
    return v != 0 ? v - 1 : 0;
}

}  // namespace t3
