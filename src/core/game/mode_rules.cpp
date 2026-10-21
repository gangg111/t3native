// remake/core/game/mode_rules.gd
#include "core/game/mode_rules.h"

#include "content/fight_tables.h"
#include "content/json_file.h"
#include "core/ball/tekken_ball.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/match_flow.h"
#include "core/game/mode_region.h"
#include "core/game/mode_start.h"
#include "core/game/opponents.h"
#include "core/game/progress_rules.h"

namespace t3 {

namespace {
// String.to_ascii_buffer() (not in the runtime): the string's bytes.
gd::PBytes _ascii_buffer(const std::string& s) {
    gd::PBytes out;
    for (char ch : s) out.append(static_cast<gd::Int>(static_cast<unsigned char>(ch)));
    return out;
}
}  // namespace

gd::Int ModeRules::_team(gd::Int side) {
    return ModeRegion::CONTEXT + TEAMS + TEAM_SIZE * side;
}

// The start hook (FightMain sub-state 1).
void ModeRules::mode_start() {
    switch (gd::Int(g->region->mode)) {
        case GameMode::ARCADE:
        case GameMode::TIME_ATTACK:
            _arcade_start();
            break;
        case GameMode::TEAM:
            _team_start();
            break;
        case GameMode::BALL:
            // FUN_800B4C58: both players' move tuning cleared.
            g->sim->ball()->drift_clear(0);
            g->sim->ball()->drift_clear(1);
            break;
        case GameMode::FORCE:
            _force_start();
            break;
        default:
            break;
    }
}

// The fight hook (FightMain sub-state 2).
void ModeRules::fight_start() {
    switch (gd::Int(g->region->mode)) {
        case GameMode::ARCADE:
        case GameMode::TIME_ATTACK:
            _arcade_fight();
            break;
        case GameMode::TEAM:
            _team_fight();
            break;
        case GameMode::SURVIVAL:
            _survival_fight();
            break;
        case GameMode::BALL:
            _ball_fight();
            break;
        case GameMode::FORCE:
            _force_fight();
            break;
        default:
            break;
    }
}

// The end hook of a decided match.
void ModeRules::match_end() {
    switch (gd::Int(g->region->mode)) {
        case GameMode::ARCADE:
            _arcade_end();
            break;
        case GameMode::VS:
            _vs_end();
            break;
        case GameMode::TEAM:
            _team_end();
            break;
        case GameMode::TIME_ATTACK:
            _time_attack_end();
            break;
        case GameMode::SURVIVAL:
            _survival_end();
            break;
        case GameMode::BALL:
            _ball_end();
            break;
        case GameMode::FORCE:
            _force_end();
            break;
        default:
            g->sub = MatchFlow::Sub::SELECT;
            break;
    }
}

// ---- arcade and time attack

void ModeRules::_arcade_start() {
    gd::Ref<ModeRegion> region = g->region;
    if (region->mode_started != 0) {
        return;
    }
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int p = gl->player_active[0] == 0 ? 1 : 0;
    Opponents::build_ladder(g, gd::Int(gl->player_char[p]) * 4 + gl->player_costume[p]);
    region->ctx_put32(OGRE_SCENE, 0);
}

void ModeRules::_arcade_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int entry = LADDER + 4 * region->fight_index;
    g->fight->ai_level = region->ctx8(entry + 2);
    if (region->human_count != 1) {
        return;
    }
    gd::Int cpu = region->cpu_index;
    gl->player_char[cpu] = region->ctx8(entry);
    gl->player_costume[cpu] = region->ctx8(entry + 1);
    gl->player_cpu[cpu] = 1;
    ModeStart::mirror_costumes(gl, cpu);
    region->ctx_put8(entry, gl->player_char[cpu]);
    region->ctx_put8(entry + 1, gl->player_costume[cpu]);
}

void ModeRules::_arcade_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    switch (gd::Int(region->human_count)) {
        case 1: {
            if (region->match_winner != region->human_index) {
                _lost();
                return;
            }
            _ladder_win();
            g->rules->gon_check(gl->player_char[region->cpu_index]);
            if (region->fight_index < 10) {
                g->sub = MatchFlow::Sub::NEXT_FIGHT;
                return;
            }
            gd::Int w = region->match_winner;
            _run_over(w);
            g->rules->record_clear(gl->player_char[w], gl->player_costume[w]);
            g->rules->count_play(gl->player_char[w]);
            g->fight->hud_shown = 0;
            gl->player_active[w] = 0;
            g->goto_screen_loader(GameFlow::State::ENDING);
            break;
        }
        case 2:
            region->match_count += 1;
            g->rules->count_result(gl->player_char[region->match_winner], gl->player_char[region->match_loser]);
            gl->player_active[region->match_loser] = 0;
            gl->player_keep[region->match_loser] = 0;
            gl->count_win(g->fight, region->match_winner);
            g->sub = MatchFlow::Sub::NEXT_FIGHT;
            break;
        default:
            break;
    }
}

void ModeRules::_time_attack_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    if (region->match_winner != region->human_index) {
        _lost();
        return;
    }
    _ladder_win();
    if (region->fight_index < 10) {
        g->sub = MatchFlow::Sub::NEXT_FIGHT;
        return;
    }
    gd::Int w = region->match_winner;
    _run_over(w);
    _time_record();
    g->rules->count_play(gl->player_char[w]);
    gl->player_active[w] = 0;
    g->goto_screen_loader(GameFlow::State::TIME_RESULT);
}

void ModeRules::_ladder_win() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int w = region->match_winner;
    region->clock_start = 0;
    region->total_time += region->fight_clock;
    _stage_record(region->fight_index, gl->player_char[w], gl->player_costume[w], w, region->fight_clock);
    region->fight_index += 1;
}

void ModeRules::_run_over(gd::Int w) {
    gd::Ref<GameGlobals> gl = g->globals;
    g->region->ctx_put32(0x38, w);
    g->region->ctx_put32(0x3C, gd::Int(gl->player_char[w]) * 4 + gl->player_costume[w]);
}

void ModeRules::_lost() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int loser = region->match_loser;
    region->clock_start = region->fight_clock;
    gl->player_active[loser] = 0;
    gl->player_keep[loser] = 0;
    g->rules->count_play(gl->player_char[loser]);
    g->sub = MatchFlow::Sub::CONTINUE_START;
}

void ModeRules::_stage_record(gd::Int stage, gd::Int character, gd::Int costume, gd::Int player, gd::Int frames) {
    gd::Int at = STAGE_RECORDS + 8 * stage;
    g->region->ctx_put32(at, frames);
    g->region->ctx_put8(at + 4, character);
    g->region->ctx_put8(at + 5, costume);
    g->region->ctx_put16(at + 6, player);
}

bool ModeRules::_time_record() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameProgress> progress = g->progress;
    progress->ranking_page = 0;
    gd::Int last = STAGE_RECORDS + 8 * LAST_STAGE;
    gd::Int character = region->ctx8(last + 4);
    gd::Int player = region->ctx_s16(last + 6);
    gd::Int total = 0;
    for (gd::Int i = 0; i < 10; ++i) {
        gd::Int at = STAGE_RECORDS + 8 * i;
        if (region->ctx_s16(at + 6) != player || region->ctx8(at + 4) != character) {
            return false;
        }
        total = (total + region->ctx32(at)) & 0xFFFFFFFFLL;
    }
    if (!(total < progress->time_record(character))) {
        return false;
    }
    progress->name_entry |= RECORD_TIME;
    gd::Int at = GameProgress::TIME_RECORDS + 8 * character;
    progress->record_entry = GameProgress::BASE + at;
    progress->entry_character = character;
    progress->entry_player = player & 0xFF;
    progress->set_time_record(character, total);
    progress->put_name(at + 4, BLANK_NAME);
    return true;
}

// ---- VS

void ModeRules::_vs_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int result = region->match_result;
    region->match_count += 1;
    if (result != 0) {
        if (result < 3) {
            g->globals->count_win(g->fight, region->match_winner);
            gd::Int at = 0x38 + 4 * region->match_winner;
            region->ctx_put16(at, region->ctx16(at) + 1);
            g->rules->count_result(g->globals->player_char[region->match_winner], g->globals->player_char[region->match_loser]);
            g->rules->count_play(g->globals->player_char[region->match_winner]);
        } else if (result == 3) {
            g->globals->set_tie_winner(g->fight, g->fight->tie_choice);
            region->ctx_put16(0x40, region->ctx16(0x40) + 1);
            g->rules->count_draw(g->globals->player_char[0], g->globals->player_char[1]);
        }
    }
    g->globals->player_keep[1] = 0;
    g->globals->player_keep[0] = 0;
    g->sub = MatchFlow::Sub::SELECT;
}

// ---- Tekken Ball

void ModeRules::_ball_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    g->fight->ai_level = (g->fight->ai_difficulty * 4 + 1) & 0xFF;
    if (region->human_count != 1) {
        return;
    }
    gd::Int key = _ball_opponent();
    gd::Int cpu = region->cpu_index;
    gl->player_char[cpu] = key >> 2;
    gl->player_costume[cpu] = key & 3;
    gl->player_cpu[cpu] = 1;
    ModeStart::mirror_costumes(gl, cpu);
}

gd::Int ModeRules::_ball_opponent() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Arr<gd::Vector2i> pairs;
    gd::Int mask = g->progress->unlocked & BALL_POOL;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        if ((gd::shr(mask, c) & 1) != 0) {
            pairs.append(gd::Vector2i(c, region->ctx16(BALL_MET + 2 * c)));
        }
    }
    bool fixed = g->fight->rules->fix_ball_opponent;
    Opponents::sort_pairs(pairs, fixed);
    gd::Int count = 0;
    while (count < pairs.size() && !(pairs[0].y + 4 < pairs[count].y)) {
        count += 1;
    }
    gd::Int key = BALL_FIRST_OPPONENT;
    gd::Int met = fixed ? gd::Int(Character::GON) : gd::Int(-1);
    if (g->progress->ball_played == 0) {
        g->progress->ball_played = 1;
    } else {
        met = pairs[gd::mod(Opponents::frame_random(g->fight), count)].x;
        key = met * 4 + (g->fight->vblank & 1);
    }
    region->ctx_put32(BALL_OPPONENT, key);
    if (met >= 0) {
        region->ctx_put16(BALL_MET + 2 * met, region->ctx16(BALL_MET + 2 * met) + 1);
    }
    return key;
}

void ModeRules::_ball_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    switch (gd::Int(region->human_count)) {
        case 1:
            g->rules->count_play(gl->player_char[region->human_index]);
            if (region->match_winner == region->human_index) {
                g->rules->gon_check(gl->player_char[region->cpu_index]);
            }
            break;
        case 2:
            g->rules->count_play(gl->player_char[0]);
            g->rules->count_play(gl->player_char[1]);
            gl->player_active[region->match_loser] = 0;
            break;
        default:
            break;
    }
    gl->player_keep[1] = 0;
    gl->player_keep[0] = 0;
    g->sub = MatchFlow::Sub::SELECT;
}

// ---- Tekken Force

void ModeRules::_force_start() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<TekkenForce> force = g->fight->force;
    force->mode_start();
    region->ctx_put32(TekkenForce::FINAL_SCORE, 0);
    region->ctx_put8(TekkenForce::NEW_RECORD, 0);
    region->ctx_put16(TekkenForce::DEFEATED, 0);
    region->ctx_put8(TekkenForce::RESULT_FLAGS, 0);
    if (((g->progress->unlocked >> 19) & 1) != 0) {
        region->ctx_put8(TekkenForce::RESULT_FLAGS, region->ctx8(TekkenForce::RESULT_FLAGS) | 0x10);
    }
    for (gd::Int k = 0; k < 4; ++k) {
        region->ctx_put8(TekkenForce::BOSSES + k, KEY_NONE);
    }
    force->manual_target_off();
}

void ModeRules::_force_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int lv = region->fight_index;
    g->fight->ai_level = lv < TekkenForce::FORCE_AI_LEVELS.size() ? TekkenForce::FORCE_AI_LEVELS[lv] : 0;
    gd::Int cpu = region->cpu_index;
    gl->player_char[cpu] = TekkenForce::ENEMY;
    gl->player_costume[cpu] = 0;
    gl->player_cpu[cpu] = 1;
    g->fight->force->put32(TekkenForce::HUMAN, region->human_index);
    region->ctx_put8(0x3E, region->human_index);
}

void ModeRules::_force_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Ref<TekkenForce> force = g->fight->force;
    gd::Ref<FighterState> p = force->player();
    if (force->gu32(TekkenForce::LEVEL_ENDED) == 0 || region->match_winner != region->human_index) {
        gl->player_active[1] = 0;
        gl->player_active[0] = 0;
        gl->player_keep[1] = 0;
        gl->player_keep[0] = 0;
        g->rules->count_play(p->char_id);
        _force_save_hiscore(force);
        g->sub = MatchFlow::Sub::GAME_OVER_START;
        return;
    }
    region->fight_index += 1;
    if (region->fight_index < 4) {
        g->sub = MatchFlow::Sub::NEXT_FIGHT;
        return;
    }
    if (region->fight_index == 4) {
        if (force->gu32(TekkenForce::FINAL_CHALLENGE) != 0) {
            g->sub = MatchFlow::Sub::NEXT_FIGHT;
            return;
        }
        g->rules->count_play(p->char_id);
        _force_leave(gl);
        g->fight->hud_shown = 0;
    } else {
        g->rules->count_play(p->char_id);
        _force_leave(gl);
        g->fight->hud_shown = 0;
        if (((g->progress->unlocked >> 19) & 1) == 0) {
            g->rules->unlock_character(Character::DOCTOR_B);
        }
    }
    _force_save_hiscore(force);
    region->ctx_put32(TekkenForce::FINAL_SCORE, force->score());
    g->goto_screen_loader(GameFlow::State::FORCE_RESULT);
}

void ModeRules::_force_leave(const gd::Ref<GameGlobals>& gl) {
    gl->player_active[1] = 0;
    gl->player_active[0] = 0;
    gl->player_keep[1] = 0;
    gl->player_keep[0] = 0;
}

void ModeRules::_force_save_hiscore(const gd::Ref<TekkenForce>& force) {
    g->progress->force_hi_score = force->gu32(TekkenForce::HISCORE);
    g->rules->request_save();
}

// ---- team battle

void ModeRules::_team_start() {
    gd::Ref<ModeRegion> region = g->region;
    region->fight_index = 0;
    region->ctx_put32(0x28, 0);
    region->ctx_put8(TEAM_ROUND, 0);
    for (gd::Int i = 0; i < 16; ++i) {
        region->ctx_put16(TEAM_OUTCOMES + 2 * i, 0);
    }
    for (gd::Int side = 0; side < 2; ++side) {
        gd::Int team = _team(side);
        gd::Int other = _team((side + 1) & 1);
        region->put8(team + 8, 0xFF);
        region->put8(team + 9, 0);
        if (g->globals->player_active[side] == 0) {
            region->put8(team + 10, 0);
            region->put8(team + 12, region->u8(other + 12));
            region->put8(team + 11, region->u8(other + 11) == 0 ? 1 : 0);
        }
        // Bug #60: the game passes the side's active flag where the CPU flag is meant.
        gd::Int active = g->globals->player_active[side];
        gd::Int cpu_side = g->fight->rules->fix_team_least_used ? (active == 0 ? 1 : 0) : active;
        Opponents::team_fill(g, cpu_side, team, other);
    }
}

void ModeRules::_team_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    for (gd::Int side = 0; side < 2; ++side) {
        gd::Int team = _team(side);
        gd::Int current = region->u8(team + 9);
        if (current == region->u8(team + 8)) {
            continue;
        }
        region->put8(team + 8, current);
        fighter_reset(side);
        gd::Int key = region->u8(team + current);
        gl->player_char[side] = key >> 2;
        gl->player_costume[side] = key & 3;
        gl->player_cpu[side] = gl->player_active[side] == 0 ? 1 : 0;
        ModeStart::mirror_costumes(gl, side);
        region->put8(team + current, ((gd::Int(gl->player_char[side]) << 2) | gl->player_costume[side]) & 0xFF);
    }
    g->fight->ai_level = 0;
    if (region->human_count == 1) {
        gd::Int team = _team(region->cpu_index);
        gd::Int current = region->u8(team + 9);
        gd::Int size = region->u8(team + 0xC);
        gd::Int level = g->fight->ai_difficulty + 3 + (g->fight->vblank & 3);
        if (current + 2 == size) {
            level += 1;
        } else if (current + 1 == size) {
            level += 3;
        }
        g->fight->ai_level = gd::mini(level & 0xFF, 9);
    }
}

void ModeRules::_team_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int result = region->match_result;
    gd::Int round_at = TEAM_OUTCOMES + 2 * region->ctx8(TEAM_ROUND);
    region->ctx_put16(round_at, result);
    gd::Int played = -1;
    if (result != 0 && result < 3) {
        gl->count_win(g->fight, region->match_winner);
        gd::Int loser_team = _team(region->match_loser);
        region->put8(loser_team + 9, region->u8(loser_team + 9) + 1);
        if (region->human_count == 2) {
            g->rules->count_result(gl->player_char[region->match_winner], gl->player_char[region->match_loser]);
        } else if (region->human_count == 1 && region->match_loser == region->human_index) {
            played = region->human_index;
        }
    } else if (result == 3) {
        region->put8(_team(1) + 9, region->u8(_team(1) + 9) + 1);
        region->put8(_team(0) + 9, region->u8(_team(0) + 9) + 1);
        if (region->human_count == 2) {
            g->rules->count_draw(gl->player_char[0], gl->player_char[1]);
        } else if (region->human_count == 1) {
            played = region->human_index;
        }
    }
    if (played >= 0) {
        g->rules->count_play(gl->player_char[played]);
    }
    region->ctx_put8(TEAM_ROUND, region->ctx8(TEAM_ROUND) + 1);
    gd::Int t0 = _team(0);
    gd::Int t1 = _team(1);
    if (region->u8(t0 + 9) < region->u8(t0 + 12) && region->u8(t1 + 9) < region->u8(t1 + 12)) {
        g->sub = MatchFlow::Sub::NEXT_FIGHT;
        return;
    }
    for (gd::Int side = 0; side < 2; ++side) {
        gd::Int team = _team(side);
        if (region->u8(team + 9) < region->u8(team + 12) && (gd::shr(region->human_mask, side) & 1) != 0
                && region->match_result != 3) {
            g->rules->count_play(gl->player_char[side]);
        }
        gl->player_active[side] = 0;
        gl->player_keep[side] = 0;
    }
    g->goto_screen_loader(GameFlow::State::TEAM_RESULT);
}

void ModeRules::fighter_reset(gd::Int fighter) {
    gd::Ref<FighterState> f = g->fight->fighters[fighter];
    f->health = 0;
    f->carried_health = 0;
}

// ---- survival

void ModeRules::_survival_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Ref<FightState> fight = g->fight;
    gd::Int wins = region->ctx_s32(Opponents::SURVIVAL_WINS);
    region->ctx_put32(SURVIVAL_RANK, _survivor_rank(wins));
    Opponents::survival_pick(g);
    gd::Int cpu = region->cpu_index;
    gd::Int key = region->ctx32(Opponents::SURVIVAL_NEXT);
    gl->player_char[cpu] = (key >> 2) & 0xFFFF;
    gl->player_costume[cpu] = key & 3;
    gl->player_cpu[cpu] = 1;
    ModeStart::mirror_costumes(gl, cpu);
    fighter_reset(cpu);
    fight->ai_difficulty = 1;
    gd::Int level = 0;
    bool hard = false;
    if (wins < 5) {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 3 + 1;
    } else if (wins < 10) {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 3 + 2;
    } else if (wins < 0x14) {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 5 + 3;
    } else if (wins < 0x32) {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 5 + 3;
        hard = (fight->vblank & 0x10) != 0;
    } else if (wins % 10 == 0) {
        level = 0;
    } else if (wins % 5 != 3) {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 7 + 3;
        hard = (fight->vblank & 0x14) != 0;
    } else {
        level = (Opponents::frame_random(fight) & 0xFFFF) % 4 + 6;
        hard = true;
    }
    if (hard) {
        fight->ai_difficulty = 2;
    }
    fight->ai_level = gd::mini(level & 0xFF, 9);
    fight->ai_difficulty = gd::mini(fight->ai_difficulty, 2);
}

gd::Int ModeRules::_survivor_rank(gd::Int wins) {
    for (gd::Int i = 0; i < SURVIVOR_COUNT; ++i) {
        if (g->progress->survivor(i).y < wins) {
            return i + 1;
        }
    }
    return 0;
}

void ModeRules::_survival_end() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int human = region->human_index;
    gd::Int character = gl->player_char[region->match_loser];
    if (region->match_winner == human) {
        gl->count_win(g->fight, human);
        region->fight_index += 1;
        gd::Int met = Opponents::SURVIVAL_MET + 2 * character;
        region->ctx_put16(met, region->ctx16(met) + 1);
        gd::Int total = 0;
        for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
            total += region->ctx16(Opponents::SURVIVAL_MET + 2 * c);
        }
        region->ctx_put32(Opponents::SURVIVAL_WINS, total);
        g->sub = MatchFlow::Sub::NEXT_FIGHT;
        return;
    }
    region->ctx_put32(0x38, human);
    region->ctx_put32(0x3C, (character << 2) | gl->player_costume[human]);
    _survivor_record(human, character, gl->win_streak);
    gl->player_active[human] = 0;
    gl->player_keep[human] = 0;
    g->rules->count_play(character);
    g->goto_screen_loader(GameFlow::State::SURVIVAL_RESULT);
}

bool ModeRules::_survivor_record(gd::Int player, gd::Int character, gd::Int wins) {
    gd::Ref<GameProgress> progress = g->progress;
    progress->ranking_page = 1;
    if (_survivor_rank(wins) == 0) {
        return false;
    }
    progress->entry_character = character & 0xFF;
    progress->entry_player = player & 0xFF;
    gd::Arr<gd::PBytes> entries;
    gd::Arr<gd::Vector2i> pairs;
    for (gd::Int i = 0; i < SURVIVOR_COUNT; ++i) {
        gd::Int at = GameProgress::SURVIVORS + 8 * i;
        entries.append(progress->bytes.slice(at, at + 8));
        pairs.append(gd::Vector2i(i, progress->u16(at + 2)));
    }
    gd::Ref<ByteBlock> fresh = gd::make<ByteBlock>(8);
    fresh->put16(0, character);
    fresh->put16(2, wins);
    gd::PBytes name = _ascii_buffer(BLANK_NAME);
    gd::Int name_size = name.size();
    for (gd::Int k = 0; k < name_size; ++k) {
        fresh->put8(4 + k, name[k]);
    }
    entries.append(fresh->bytes);
    pairs.append(gd::Vector2i(SURVIVOR_COUNT, wins));
    Opponents::sort_pairs(pairs, false);
    for (gd::Int i = 0; i < SURVIVOR_COUNT; ++i) {
        gd::Int at = GameProgress::SURVIVORS + 8 * i;
        gd::PBytes e = entries[pairs[i].x];
        progress->put16(at, e.decode_u16(0));
        progress->put16(at + 2, e.decode_u16(2));
        gd::Int n = 0;
        while (n < 4 && e[4 + n] != 0) {
            n += 1;
        }
        progress->put_name(at + 4, e.slice(4, 4 + n).get_string_from_ascii());
        if (pairs[i].x == SURVIVOR_COUNT) {
            progress->survivor_entry = GameProgress::BASE + at;
        }
    }
    progress->name_entry |= RECORD_SURVIVOR;
    return true;
}

void ModeRules::stage_and_music() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Ref<FightTables> t = g->fight->tables;
    gd::Int key = 0;
    gd::Int stage = 0;
    switch (gd::Int(region->mode)) {
        case GameMode::VS: {
            gd::Int c0 = gl->player_char[0];
            gd::Int k0 = 0;
            if (c0 > 0x15 || gl->player_costume[0] > 3) {
                k0 = (0x16 << 2) & 0xFF;
            } else {
                k0 = ((c0 << 2) | gl->player_costume[0]) & 0xFF;
            }
            gd::Int k1 = KEY_NONE;
            if (!(gl->player_char[1] > 0x15 || gl->player_costume[1] > 3)) {
                k1 = ((gd::Int(gl->player_char[1]) << 2) | gl->player_costume[1]) & 0xFF;
            }
            gd::PI32 keys{k0, k1};
            key = keys[gl->other_player];
            gd::Int kept = region->ctx32(VS_STAGE_KEY);
            if (kept != 0xFFFFFFFFLL && k0 != region->ctx32(VS_STAGE_KEY + 4) && k1 != region->ctx32(VS_STAGE_KEY + 8)) {
                key = kept;
            }
            region->ctx_put32(VS_STAGE_KEY, key);
            region->ctx_put32(VS_STAGE_KEY + 4, k0);
            region->ctx_put32(VS_STAGE_KEY + 8, k1);
            if (key > 0x5C) {
                key = KEY_NONE;
            }
            stage = JsonFile::number(t->character(key)["stage"]);
            break;
        }
        case GameMode::TEAM:
        case GameMode::PRACTICE:
            key = _key(gl->other_player);
            stage = JsonFile::number(t->character(key)["stage"]);
            break;
        case GameMode::BALL:
            key = _key(_player_of_humans());
            stage = TEKKEN_BALL_STAGE;
            break;
        case GameMode::FORCE:
            // The level's stage (15 + level); the Doctor B. level plays on stage 15 with track 0x19.
            key = _key(g->fight->force->human());
            stage = FORCE_FIRST_STAGE + region->fight_index;
            if (stage > FORCE_LAST_STAGE) {
                g->fight->music = FORCE_EXTRA_MUSIC;
                g->fight->stage = FORCE_FIRST_STAGE;
                region->put8(0x6A, FORCE_FIRST_STAGE);
                region->put8(0x6B, FORCE_EXTRA_MUSIC);
                return;
            }
            break;
        default:
            key = _key(_player_of_humans());
            stage = JsonFile::number(t->character(key)["stage"]);
            break;
    }
    g->fight->music = JsonFile::number(t->character(key)["music"]);
    g->fight->stage = stage;
    region->put8(0x6A, stage);
    region->put8(0x6B, g->fight->music);
}

gd::Int ModeRules::_player_of_humans() {
    switch (gd::Int(g->region->human_count)) {
        case 0:
            return 0;
        case 1:
            return g->region->cpu_index;
        case 2:
            return g->globals->other_player;
        default:
            break;
    }
    return 0;
}

gd::Int ModeRules::_key(gd::Int player) {
    gd::Int key = (gd::Int(g->globals->player_char[player]) << 2) | g->globals->player_costume[player];
    return key > 0x5C ? KEY_NONE : key;
}

}  // namespace t3
