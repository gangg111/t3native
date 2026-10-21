// remake/core/fight/fight_setup.gd
// What a VS match is started with (the menus' and the fight setup screen's choices): the two
// fighters, the stage, the options and the random seeds.
#include "core/fight/fight_setup.h"

namespace t3 {

// The humans as a bit per player (0x800AE3D8).
gd::Int FightSetup::human_mask() const {
    gd::Int mask = 0;
    for (gd::Int p = 0; p < 2; ++p) {
        if (cpu[p] == 0) {
            mask |= gd::Int(1) << p;
        }
    }
    return mask;
}

FightSetup::FightSetup() {
    for (gd::Int p = 0; p < 2; ++p) {
        key_tables.append(gd::PI32::from(DEFAULT_KEY_TABLE.begin(), DEFAULT_KEY_TABLE.end()));
    }
}

}  // namespace t3
