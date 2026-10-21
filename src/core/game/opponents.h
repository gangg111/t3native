#pragma once
// remake/core/game/opponents.gd
// The CPU opponents of the modes (arcade.ovl; modes.md#arcade-ladder): the arcade ladder
// (FUN_800B2494), the team battle teams (FUN_800B2724, screens_sim.team_fill) and the survival
// picks (FUN_800B2B98, screens_sim.survival_pick). The picks draw from the frame generator
// (FUN_8004D13C, `x·5 + 1` at 0x8009F650).
//
// Not mechanical: the static functions take the flow as `GameFlow*` (callers pass `g` or `this`).
#include "gd/gd.h"

namespace t3 {

class GameFlow;
class FightState;

class Opponents {
public:
    static constexpr gd::Int CHARACTERS = 22;
    static constexpr gd::Int ORIGINAL_TEN = 0x3FF;
    static constexpr gd::Int LADDER_EXCLUDED = 0xFFEF9FFFLL;  // stages 5–8: no Heihachi, Ogre or True Ogre
    static constexpr gd::Int TIME_ATTACK_MASK = 0x157FFF;
    static constexpr gd::Int ARCADE_MASK = 0x1FFFFF;
    static constexpr gd::Int GON_BIT = 0x20000;
    static constexpr gd::Int LADDER_STAGES = 10;
    static constexpr gd::Int TEAM_POOL = 0x1F7FFF;
    static constexpr gd::Int TEAM_MET = 0x74;                // mode context: u16 per character, the team battle met counts
    // (untyped const array of ints)
    static inline const gd::Arr<gd::Int> SURVIVAL_POOLS{0x3FF, 0xFBFFF, 0x1FFFFF};
    static constexpr gd::Int SURVIVAL_RING = 0x50;           // mode context: the last four opponents
    static constexpr gd::Int SURVIVAL_RING_INDEX = 0x4C;
    static constexpr gd::Int SURVIVAL_MET = 0x60;            // mode context: u16 per character
    static constexpr gd::Int SURVIVAL_WINS = 0x44;
    static constexpr gd::Int SURVIVAL_NEXT = 0x48;

    // FUN_8004D13C: the frame generator's next value.
    static gd::Int frame_random(const gd::Ref<FightState>& fight);
    // FUN_8004D068: a stable insertion sort of (id, key) pairs by the unsigned key.
    static void sort_pairs(const gd::Arr<gd::Vector2i>& pairs, bool ascending);
    static gd::PI32 _bits(gd::Int mask);
    // Draws one of `pool`'s first `count[0]` entries and moves the last into its place.
    static gd::Int _draw(const gd::Ref<FightState>& fight, const gd::PI32& pool, const gd::PI32& count);
    // FUN_800B2494: the ten stages (character, costume, stage index) at the mode context's +0x90,
    // for the player's costume key `key`.
    static void build_ladder(GameFlow* g, gd::Int key);
    // FUN_800B2724: completes a team (13 bytes at `team`: members as costume keys, +10 count chosen,
    // +11 costume, +12 size) from the unlocked characters not in it. With `active` set (meant for a
    // CPU team; the game passes the side's active flag, bug #60) and three or more members, one of
    // the two least-used characters is taken first. The rest come at random from the characters met least often in team
    // battle, widening the met-count window until there are enough, shuffled into the free places.
    static void team_fill(GameFlow* g, gd::Int active, gd::Int team, gd::Int other);
    // FUN_800B2B98: the next survival opponent (a costume key at +0x48). By the stage counter the
    // pool is the ten original characters (stages 0–6), all but Ogre and True Ogre (7–16), then all,
    // always within 0x157FFF, without the last four opponents. The pick is random among those met
    // least often (within 4 of the least from 20 wins on), in a costume from the frame counter.
    static void survival_pick(GameFlow* g);
};

}  // namespace t3
