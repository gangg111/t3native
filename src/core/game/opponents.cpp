// remake/core/game/opponents.gd
// The CPU opponents of the modes (arcade.ovl; modes.md#arcade-ladder): the arcade ladder
// (FUN_800B2494), the team battle teams (FUN_800B2724, screens_sim.team_fill) and the survival
// picks (FUN_800B2B98, screens_sim.survival_pick). The picks draw from the frame generator
// (FUN_8004D13C, `x·5 + 1` at 0x8009F650).
#include "core/game/opponents.h"

#include "core/fight/fight_state.h"
#include "core/fighter/character.h"
#include "core/game/game_flow.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"

namespace t3 {

// FUN_8004D13C: the frame generator's next value.
gd::Int Opponents::frame_random(const gd::Ref<FightState>& fight) {
    fight->frame_rng = (fight->frame_rng * 5 + 1) & 0xFFFFFFFFLL;
    return fight->frame_rng;
}

// FUN_8004D068: a stable insertion sort of (id, key) pairs by the unsigned key.
void Opponents::sort_pairs(const gd::Arr<gd::Vector2i>& pairs, bool ascending) {
    gd::Int last = pairs.size() - 1;
    for (gd::Int i = 0; i < last; ++i) {
        gd::Int j = i;
        while (j >= 0) {
            gd::Int a = pairs[j].y;
            gd::Int b = pairs[j + 1].y;
            if (!(ascending ? b < a : a < b)) {
                break;
            }
            gd::Vector2i t = pairs[j];
            pairs[j] = pairs[j + 1];
            pairs[j + 1] = t;
            j -= 1;
        }
    }
}

gd::PI32 Opponents::_bits(gd::Int mask) {
    gd::PI32 out;
    for (gd::Int c = 0; c < CHARACTERS; ++c) {
        if ((gd::shr(mask, c) & 1) != 0) {
            out.append(c);
        }
    }
    return out;
}

// Draws one of `pool`'s first `count[0]` entries and moves the last into its place.
gd::Int Opponents::_draw(const gd::Ref<FightState>& fight, const gd::PI32& pool, const gd::PI32& count) {
    gd::Int i = gd::mod(frame_random(fight), count[0]);
    gd::Int c = pool[i];
    pool[i] = pool[count[0] - 1];
    count[0] -= 1;
    return c;
}

// FUN_800B2494: the ten stages (character, costume, stage index) at the mode context's +0x90,
// for the player's costume key `key`.
void Opponents::build_ladder(GameFlow* g, gd::Int key) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int boss = (key >> 2) == Character::HEIHACHI ? Character::JIN : Character::HEIHACHI;
    gd::Int pool_mask = 0;
    if (region->mode == GameMode::TIME_ATTACK) {
        pool_mask = g->progress->unlocked & TIME_ATTACK_MASK;
    } else {
        pool_mask = g->progress->unlocked & ARCADE_MASK;
        if (g->progress->ball_played != 0) {
            pool_mask |= GON_BIT;
        }
    }
    gd::Int left = pool_mask & LADDER_EXCLUDED & ~gd::shl(1, (key >> 2) & 31) & ~gd::shl(1, boss);
    gd::Int at = ModeRegion::CONTEXT + 0x90;
    gd::PI32 pool = _bits(left & ORIGINAL_TEN);
    gd::PI32 count{pool.size()};
    gd::Int stage = 0;
    while (stage < 4) {
        gd::Int c = _draw(g->fight, pool, count);
        left &= ~gd::shl(1, c);
        region->put8(at + 4 * stage, c);
        stage += 1;
    }
    pool = _bits(left);
    count[0] = pool.size();
    while (stage < 8) {
        region->put8(at + 4 * stage, _draw(g->fight, pool, count));
        stage += 1;
    }
    region->put8(at + 4 * 8, boss);
    region->put8(at + 4 * 9, Character::OGRE);
    for (gd::Int i = 0; i < LADDER_STAGES; ++i) {
        // The costume: 1 when the player's is not costume 0; Ogre and True Ogre always 0.
        gd::Int c = region->u8(at + 4 * i);
        region->put8(at + 4 * i + 2, i);
        region->put8(at + 4 * i + 1, c == Character::TRUE_OGRE || c == Character::OGRE ? 0 : ((key & 3) != 0 ? 1 : 0));
    }
}

// FUN_800B2724: completes a team (13 bytes at `team`: members as costume keys, +10 count chosen,
// +11 costume, +12 size) from the unlocked characters not in it. With `active` set (meant for a
// CPU team; the game passes the side's active flag, bug #60) and three or more members, one of
// the two least-used characters is taken first. The rest come at random from the characters met least often in team
// battle, widening the met-count window until there are enough, shuffled into the free places.
void Opponents::team_fill(GameFlow* g, gd::Int active, gd::Int team, gd::Int other) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightState> fight = g->fight;
    gd::Int size = region->u8(team + 12);
    gd::Int chosen_count = region->u8(team + 10);
    gd::Int avail = g->progress->unlocked & TEAM_POOL;
    for (gd::Int i = 0; i < chosen_count; ++i) {
        avail &= ~gd::shl(1, (region->u8(team + i) >> 2) & 31);
    }
    gd::Int extra = 0;
    gd::Int chosen = 0;
    if (active != 0 && size >= 3) {
        gd::Arr<gd::Vector2i> pairs;
        for (gd::Int c : _bits(avail)) {
            pairs.append(gd::Vector2i(c, g->progress->usage(c)));
        }
        if (!pairs.is_empty()) {
            sort_pairs(pairs, true);
        }
        if (pairs.size() >= 2) {
            extra = 1;
            gd::Int c = pairs[frame_random(fight) & 1].x;
            chosen |= gd::shl(1, c & 31);
            avail &= ~gd::shl(1, c & 31);
        }
    }
    gd::Arr<gd::Vector2i> met;
    for (gd::Int c : _bits(avail)) {
        met.append(gd::Vector2i(c, region->ctx16(TEAM_MET + 2 * c)));
    }
    if (!met.is_empty()) {
        sort_pairs(met, true);
    }
    gd::Int need = size - (chosen_count + extra);
    gd::Int tolerance = 0;
    gd::Int n = 0;
    while (!met.is_empty()) {
        gd::Int threshold = (met[0].y + tolerance) & 0xFFFFFFFFLL;
        n = 0;
        while (n < met.size() && !(threshold < met[n].y)) {
            n += 1;
        }
        if (!(n < need)) {
            break;
        }
        tolerance += 1;
    }
    gd::Int pool_mask = 0;
    for (gd::Int k = 0; k < n; ++k) {
        pool_mask |= gd::shl(1, met[k].x & 31);
    }
    gd::PI32 pool = _bits(pool_mask);
    gd::PI32 count{pool.size()};
    for (gd::Int i = extra; i < size - chosen_count; ++i) {
        chosen |= gd::shl(1, _draw(fight, pool, count) & 31);
    }
    pool = _bits(chosen);
    count[0] = pool.size();
    gd::Int costume = region->u8(team + 11);
    for (gd::Int s = chosen_count; s < size; ++s) {
        gd::Int c = _draw(fight, pool, count);
        gd::Int key = (c * 4 + costume) & 0xFF;
        gd::Int other_count = region->u8(other + 10);
        for (gd::Int i = 0; i < other_count; ++i) {
            if (region->u8(other + i) == key) {
                key = (c * 4 + (costume ^ 1)) & 0xFF;
                break;
            }
        }
        region->put8(team + s, key);
    }
    for (gd::Int i = 0; i < size; ++i) {
        gd::Int c = region->u8(team + i) >> 2;
        region->ctx_put16(TEAM_MET + 2 * c, region->ctx16(TEAM_MET + 2 * c) + 1);
    }
}

// FUN_800B2B98: the next survival opponent (a costume key at +0x48). By the stage counter the
// pool is the ten original characters (stages 0–6), all but Ogre and True Ogre (7–16), then all,
// always within 0x157FFF, without the last four opponents. The pick is random among those met
// least often (within 4 of the least from 20 wins on), in a costume from the frame counter.
void Opponents::survival_pick(GameFlow* g) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int stage = region->ctx32(0x24);
    gd::Int mask = stage < 7 ? SURVIVAL_POOLS[0] : (stage >= 0x11 ? SURVIVAL_POOLS[2] : SURVIVAL_POOLS[1]);
    mask &= TIME_ATTACK_MASK;
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int v = region->ctx32(SURVIVAL_RING + 4 * i);
        if (v != CHARACTERS) {
            mask &= ~gd::shl(1, v & 31);
        }
    }
    gd::Arr<gd::Vector2i> pairs;
    for (gd::Int c : _bits(mask)) {
        pairs.append(gd::Vector2i(c, region->ctx16(SURVIVAL_MET + 2 * c)));
    }
    if (!pairs.is_empty()) {
        sort_pairs(pairs, true);
    }
    gd::Int threshold = (pairs[0].y + (region->ctx32(SURVIVAL_WINS) >= 0x14 ? 4 : 0)) & 0xFFFFFFFFLL;
    gd::Int n = 0;
    while (n < pairs.size() && !(threshold < pairs[n].y)) {
        n += 1;
    }
    gd::Int c = pairs[gd::mod(frame_random(g->fight), n)].x;
    region->ctx_put32(SURVIVAL_NEXT, c * 4 + (g->fight->vblank & 1));
    gd::Int index = region->ctx32(SURVIVAL_RING_INDEX) + 1;
    if (index >= 4) {
        index = 0;
    }
    region->ctx_put32(SURVIVAL_RING_INDEX, index);
    region->ctx_put32(SURVIVAL_RING + 4 * index, c);
}

}  // namespace t3
