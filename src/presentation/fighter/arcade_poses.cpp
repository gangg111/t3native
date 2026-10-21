// remake/presentation/fighter/arcade_poses.gd
#include "presentation/fighter/arcade_poses.h"

#include "core/fighter/fighter_hands.h"

namespace t3 {

// The variant a hand (or jaw) channel's value selects.
gd::Int ArcadePoses::hand_variant(gd::Int value) {
    return value <= FighterHands::FIST ? value >> BLEND_SHIFT : value - SHAPE_BASE;
}

// The variant of each hand channel (the order of `FighterHands.current`).
gd::PI32 ArcadePoses::hand_variants(const gd::Ref<FighterHands>& hands) {
    gd::PI32 out;
    for (gd::Int value : hands->current) {
        out.append(hand_variant(value));
    }
    return out;
}

// True Ogre's wings (the wing sequence's variant) and tail (the level channel) variants.
gd::PI32 ArcadePoses::wing_variants(const gd::Ref<FighterHands>& hands) {
    return gd::PI32{hands->wing_variant, gd::Int(hands->current[0]) >> TAIL_LEVEL_SHIFT};
}

}  // namespace t3
