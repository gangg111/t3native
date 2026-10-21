// remake/core/game/progress_rules.gd
// The rules that change the game's progress (modes.md#unlocks, #records), written from the
// verified port `tools/research/screens_sim.py`: the unlock schedule, the play-count unlocks, arcade
// clears, Gon's unlock, the character statistics and the save request.
#include "core/game/progress_rules.h"

#include "content/flow_data.h"
#include "core/fight/rule_set.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"

namespace t3 {

ProgressRules::ProgressRules(const gd::Ref<GameProgress>& game_progress, const gd::Ref<GameGlobals>& game_globals,
                             const gd::Ref<ModeRegion>& mode_region, const gd::Ref<FlowData>& flow,
                             const gd::Ref<RuleSet>& rule_set) {
    progress = game_progress;
    globals = game_globals;
    region = mode_region;
    schedule = flow->unlock_schedule;
    fight_rules = rule_set;
}

gd::Int ProgressRules::popcount(gd::Int v) {
    gd::Int n = 0;
    v &= 0xFFFFFFFFLL;
    while (v != 0) {
        n += v & 1;
        v >>= 1;
    }
    return n;
}

// FUN_80056498.
void ProgressRules::unlock_character(gd::Int character) {
    gd::Int bit = gd::shl(1, character);
    if ((progress->unlocked & bit) == 0) {
        progress->new_character = character;
        progress->fights_since_unlock = 0;
    }
    progress->unlocked |= bit;
}

// `--unlock`: every character, Start costume, mode and Theater movie open, as after the schedule's
// last step but without its side effects (no "just unlocked" character, no reset of the play
// count). The movies open with the arcade clears of every character and the three second clears.
void ProgressRules::open_all(const gd::Ref<GameProgress>& p) {
    p->cleared |= ALL_CHARACTERS;
    p->cleared2 |= SECOND_CLEARS;
    p->unlocked |= ALL_CHARACTERS;
    p->start_costumes = (p->start_costumes | START_COSTUME_MASK) & START_COSTUME_MASK;
    p->ball_new = gd::maxi(p->ball_new, 1);
    p->theater_new = gd::maxi(p->theater_new, 1);
    p->unlock_class = 2;
}

void ProgressRules::update_unlock_class() {
    if ((progress->unlocked & DOCTOR_B_BIT) != 0) {
        progress->unlock_class = 2;
    } else {
        progress->unlock_class = popcount(progress->unlocked) >= 15 ? 1 : 0;
    }
}

void ProgressRules::add_start_costume(gd::Int bit) {
    if ((progress->start_costumes & bit) == 0) {
        progress->fights_since_unlock = 0;
    }
    progress->start_costumes = (progress->start_costumes | bit) & START_COSTUME_MASK;
}

// ArcadeUnlocks (FUN_800564C8): the first `n` steps of the schedule.
void ProgressRules::arcade_unlocks(gd::Int n) {
    if ((n & 0xFFFFFFFFLL) > 14) {
        n = 14;
    }
    gd::Int steps = gd::maxi(n, 0);
    for (gd::Int step = 0; step < steps; ++step) {
        gd::Int kind = schedule[2 * step];
        gd::Int value = schedule[2 * step + 1];
        switch (kind) {
            case 0:
                progress->fights_since_unlock = 0;
                break;
            case 1:
                unlock_character(value & 31);
                break;
            case 2:
                add_start_costume(gd::shl(1, value & 31));
                break;
            case 3:
                _open_theater();
                break;
            case 4:
                if (progress->ball_new == 0) {
                    progress->ball_new = 1;
                    progress->fights_since_unlock = 0;
                }
                break;
            case 5:
                for (gd::Int c = 0; c < 22; ++c) {
                    if ((gd::shr(ALL_CHARACTERS, c) & 1) != 0) {
                        unlock_character(c);
                    }
                    if ((gd::shr(START_COSTUME_MASK, c) & 1) != 0) {
                        add_start_costume(gd::shl(1, c));
                    }
                }
                if (progress->ball_new == 0) {
                    progress->ball_new = 1;
                    progress->fights_since_unlock = 0;
                }
                if (progress->theater_new == 0) {
                    progress->theater_new = 1;
                    progress->fights_since_unlock = 0;
                }
                update_unlock_class();
                break;
            default:
                break;
        }
    }
    _open_theater();
    update_unlock_class();
}

void ProgressRules::_open_theater() {
    if ((progress->cleared & 0x3FF) == 0x3FF && progress->theater_new == 0) {
        progress->theater_new = 1;
        progress->fights_since_unlock = 0;
    }
}

// FUN_800567A0: Start costumes by usage, then one more schedule step per 100 (then 50) fights.
void ProgressRules::play_unlocks() {
    for (const gd::PI32& rule : USAGE_COSTUMES) {
        if (progress->usage(rule[0]) >= gd::Int(rule[1])) {
            add_start_costume(rule[2]);
        }
    }
    update_unlock_class();
    gd::Int need = progress->play_steps_seen != 0 ? 50 : 100;
    if (progress->fights_since_unlock >= need) {
        progress->play_steps_seen = 1;
        gd::Int steps = gd::mini(progress->play_steps + 1, 14);
        progress->fights_since_unlock = 0;
        progress->play_steps = steps;
        arcade_unlocks(steps);
    }
}

// FUN_8004C678: after a statistics update, the play-count unlocks and a pending save.
void ProgressRules::request_save() {
    play_unlocks();
    globals->save_pending = 1;
}

// FUN_8005696C: beating Gon as the CPU in arcade or Tekken Ball unlocks him.
void ProgressRules::gon_check(gd::Int character) {
    if (character == Character::GON && (progress->unlocked & (gd::Int(1) << Character::GON)) == 0
            && (region->mode == GameMode::ARCADE || region->mode == GameMode::BALL)) {
        progress->unlocked |= gd::Int(1) << Character::GON;
        progress->new_character = Character::GON;
        progress->fights_since_unlock = 0;
    }
}

// FUN_800B2350 (arcade.ovl): an arcade clear, and the steps the clears open (unlock_steps).
void ProgressRules::record_clear(gd::Int character, gd::Int costume) {
    if (region->mode != GameMode::ARCADE) {
        return;
    }
    progress->new_character = NO_CHARACTER;
    gd::Int first = progress->cleared;
    gd::Int second = progress->cleared2;
    switch (character) {
        case 11:
            if (costume == 1) {
                second |= 0x800;
            } else {
                first |= 0x800;
            }
            break;
        case 8:
            if (costume < 2) {
                first |= 0x100;
            } else {
                second |= 0x100;
            }
            break;
        case 16:
            if ((first & 0x10000) == 0) {
                first |= 0x10000;
            } else {
                second |= 0x10000;
            }
            break;
        default:
            first |= gd::shl(1, character & 31);
            break;
    }
    progress->cleared = first & ALL_CHARACTERS;
    progress->cleared2 = second & ALL_CHARACTERS;
    arcade_unlocks(unlock_steps((first | second) & ALL_CHARACTERS, fight_rules->fix_unlock_pace));
}

// The unlock steps a cleared mask opens: the bit positions below the number of cleared
// characters that are set in 0x83FF (bug #29), or with `fixed` the cleared characters in it.
gd::Int ProgressRules::unlock_steps(gd::Int cleared, bool fixed) {
    if (fixed) {
        return popcount(cleared & UNLOCK_STEP_MASK);
    }
    gd::Int n = 0;
    gd::Int count = popcount(cleared);
    for (gd::Int i = 0; i < count; ++i) {
        if ((gd::shl(1, i) & UNLOCK_STEP_MASK) != 0) {
            n += 1;
        }
    }
    return n;
}

void ProgressRules::_bump(gd::Int character, gd::Int which) {
    gd::Int c = character % 22;
    progress->set_stat(c, which, gd::mini(progress->stat(c, which) + 1, FIGHTS_CAP));
    progress->fights_since_unlock = gd::mini(progress->fights_since_unlock + 1, FIGHTS_CAP);
}

// FUN_8005169C: a finished run or a VS win of `character`.
void ProgressRules::count_play(gd::Int character) {
    _bump(character, 0);
    request_save();
}

// FUN_80051740: a win and a loss.
void ProgressRules::count_result(gd::Int winner, gd::Int loser) {
    gd::Int w = winner % 22;
    progress->set_stat(w, 1, gd::mini(progress->stat(w, 1) + 1, FIGHTS_CAP));
    gd::Int l = loser % 22;
    progress->set_stat(l, 2, gd::mini(progress->stat(l, 2) + 1, FIGHTS_CAP));
    progress->fights_since_unlock = gd::mini(progress->fights_since_unlock + 1, FIGHTS_CAP);
    request_save();
}

// FUN_80051838: a draw for both characters.
void ProgressRules::count_draw(gd::Int a, gd::Int b) {
    _bump(a, 3);
    _bump(b, 3);
    request_save();
}

}  // namespace t3
