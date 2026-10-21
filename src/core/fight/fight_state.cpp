// remake/core/fight/fight_state.gd
// The whole state of a fight (remake-plan.md#simulation-core): both fighter records and the
// game globals the fight code keeps between frames. Names follow the documentation; the
// comment gives the global's address (Japan Rev.1) where it helps to follow the RE notes.
#include "core/fight/fight_state.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/backdrop_turn.h"
#include "core/effects/effect_pool.h"
#include "core/fight/replay_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"

namespace t3 {

FightState::FightState(const gd::Ref<FightTables>& fight_tables, const gd::Ref<RuleSet>& fight_rules) {
    // Object-valued member defaults, in declaration order.
    region = gd::make<ModeRegion>();
    progress = gd::make<GameProgress>();
    backdrop = gd::make<BackdropTurn>();
    replay = gd::make<ReplayState>();
    effects = gd::make<EffectPool>();
    rng = gd::make<CRand>();
    // The properties over region / progress (their get / set).
    mode.bind([this] { return gd::Int(region->mode); }, [this](gd::Int v) { region->mode = v; });
    frame_counter.bind([this] { return gd::Int(region->frame_counter); }, [this](gd::Int v) { region->frame_counter = v; });
    rounds_option.bind([this] { return gd::Int(region->rounds_option); }, [this](gd::Int v) { region->rounds_option = v; });
    chip_damage.bind([this] { return gd::Int(region->chip_damage); }, [this](gd::Int v) { region->chip_damage = v; });
    pause_allowed.bind([this] { return gd::Int(region->pause_allowed); }, [this](gd::Int v) { region->pause_allowed = v; });
    collapse_on_ko.bind([this] { return gd::Int(region->collapse_on_ko); }, [this](gd::Int v) { region->collapse_on_ko = v; });
    carry_health.bind([this] { return gd::Int(region->carry_health); }, [this](gd::Int v) { region->carry_health = v; });
    match_result.bind([this] { return gd::Int(region->match_result); }, [this](gd::Int v) { region->match_result = v; });
    match_winner.bind([this] { return gd::Int(region->match_winner); }, [this](gd::Int v) { region->match_winner = v; });
    match_loser.bind([this] { return gd::Int(region->match_loser); }, [this](gd::Int v) { region->match_loser = v; });
    fight_clock.bind([this] { return gd::Int(region->fight_clock); }, [this](gd::Int v) { region->fight_clock = v; });
    true_ogre_mask.bind([this] { return gd::Int(region->true_ogre_mask); }, [this](gd::Int v) { region->true_ogre_mask = v; });
    draw_wins.bind([this] { return gd::Int(region->draw_wins); }, [this](gd::Int v) { region->draw_wins = v; });
    team_round.bind([this] { return gd::Int(region->team_round); }, [this](gd::Int v) { region->team_round = v; });
    unlocked.bind([this] { return gd::Int(progress->unlocked); }, [this](gd::Int v) { progress->unlocked = v; });
    fighter_distance.bind([this] { return gd::Int(region->fighter_distance); }, [this](gd::Int v) { region->fighter_distance = v; });
    challenger.bind([this] { return gd::Int(region->challenger); }, [this](gd::Int v) { region->challenger = v; });

    // _init
    tables = fight_tables;
    rules = fight_rules;
    for (gd::Int i = 0; i < RECORDS; ++i) {
        gd::Ref<FighterState> f = gd::make<FighterState>();
        // BootInit: the third record belongs to player 1's side (its move index and pad).
        f->index = i;
        f->player_index = gd::mini(i, 1);
        f->opp_index = 1 - gd::mini(i, 1);
        f->cur_opp_index = f->opp_index;
        fighters.append(f);
    }
    for (gd::Int i = 0; i < 3; ++i) {
        prev_roots.append(gd::PI32{0, 0, 0, 0});
        segment_anchor.append(gd::PI32{0, 0, 0});
        segment_end0.append(gd::PI32{0, 0, 0});
        segment_end1.append(gd::PI32{0, 0, 0});
    }
}

// The records that fight (two, three in Tekken Force).
gd::Arr<gd::Ref<FighterState>> FightState::active() const {
    return fighters.slice(0, count);
}

// MoveLookup (0x8002D3CC): the move row of a slot in the fighter's merged move index.
gd::Ref<MoveRow> FightState::move_for_slot(const gd::Ref<FighterState>& f, gd::Int slot) const {
    return Fx::s16(slot) >= 0 ? slots[f->player_index][Fx::s16(slot) & 0xFFFF] : nullptr;
}

// FighterOpponentIndex (0x80045E50): the throw partner during a throw, else the last attacker
// when this move was interrupted by a hit, else the last fighter this move touched, else the
// default opponent.
gd::Int FightState::opponent_index(const gd::Ref<FighterState>& f) const {
    if (f->in_throw != 0) {
        return f->throw_partner;
    }
    if (f->was_hit_this_move != 0) {
        return f->last_attacker;
    }
    if (f->contact != 0) {
        return f->last_hit_target;
    }
    return f->opp_index;
}

// FighterOpponent (0x80045EB0).
gd::Ref<FighterState> FightState::opponent(const gd::Ref<FighterState>& f) const {
    return fighters[opponent_index(f)];
}

// FUN_80078940: the move lists start at their first move.
void FightState::command_scroll_reset() {
    command_cursor = gd::PI32{0, 0};
    command_offset = gd::PI32{0, 0};
}

}  // namespace t3
