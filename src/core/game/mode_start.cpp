// remake/core/game/mode_start.gd
// FUN_800DAF2C (title.ovl; menu_sim.mode_start, verified): a mode starts. The options go into
// the fight variables, the players' choices are cleared, the mode's rule bytes (0x800AFF54 …)
// and counters are set, and the game goes to the fight preparation (state 7).
#include "core/game/mode_start.h"

#include "content/flow_data.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fighter/fighter_state.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"

namespace t3 {

void ModeStart::start(GameFlow* g, gd::Int mode, gd::Int players) {
    _options(g, mode);
    _players(g, players);
    g->fight->replay->init_attract(g->state == GameFlow::State::ENBU);
    if (_mode_rules(g, mode)) {
        g->progress->ranking_page = 2;
    }
    gd::Ref<FightState> fight = g->fight;
    if (fight->round_time_option > 4) {
        fight->timer_stopped = 1;
    }
    g->region->mode_overlay = mode >= 0 && mode < MODE_OVERLAYS.size() ? MODE_OVERLAYS[mode] : 0;
    g->state = GameFlow::State::PREPARE;
    g->sub = 0;
}

// The options into the fight variables, the mode's counters cleared.
void ModeStart::_options(GameFlow* g, gd::Int mode) {
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameProgress> progress = g->progress;
    g->music_stop();
    // The records' own numbers again (+0x12 pad slot, +0x1E index; the third record is player 1's
    // side): Tekken Ball copies a hitter's whole record into the third one.
    for (gd::Int i = 0; i < FightState::RECORDS; ++i) {
        fight->fighters[i]->player_index = gd::mini(i, 1);
        fight->fighters[i]->index = i;
    }
    fight->stage_cached = -1;           // FUN_8006C870(0x1F)
    for (gd::Int at : gd::Arr<gd::Int>{0x69, 0x5F, 0x61, 0x62, 0x63}) {
        region->put8(at, 0);
    }
    fight->timer_stopped = 0;
    fight->practice = 0;
    for (gd::Int at : gd::Arr<gd::Int>{0x74, 0x78, 0x7C, 0x80, 0x84}) {
        region->put32(at, 0);
    }
    g->fight->challenger_lock = 0;
    fight->attract = 0;
    region->character_change = progress->character_change;
    region->quick_select_option = progress->quick_select;
    region->rounds_option = progress->fight_count;
    fight->ai_difficulty = progress->difficulty;
    fight->round_time_option = progress->round_time;
    fight->chip_damage = progress->guard_damage;
    fight->ai_level = 0;
    region->mode = mode;
}

// The players' controllers and choices cleared; `players` picks who starts active.
void ModeStart::_players(GameFlow* g, gd::Int players) {
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameProgress> progress = g->progress;
    gd::Ref<GameGlobals> globals = g->globals;
    for (gd::Int p = 0; p < 2; ++p) {
        globals->controller[p] = progress->controller(p);
        gd::Int held = g->held(p);
        if ((held & PadState::START) != 0) {
            region->challenger_quick = held == QUICK_CHALLENGE ? 1 : 0;
        }
        // FUN_80051A98: no carried health.
        fight->fighters[p]->health = 0;
        fight->fighters[p]->carried_health = 0;
        if (progress->cursor_hold == 0) {
            progress->set_last_character(p, NO_PICK);
        }
        globals->player_char[p] = NO_CHARACTER;
        globals->player_costume[p] = 0;
        globals->player_keep[p] = 0;
        globals->player_active[p] = 0;
    }
    gd::Int first = (players ^ 1) & 1;
    globals->set_tie_winner(fight, first);
    globals->player_active[first] = 1;
}

// The mode's rule bytes and its own settings; true when the ranking shows its usage page next.
bool ModeStart::_mode_rules(GameFlow* g, gd::Int mode) {
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> globals = g->globals;
    bool tail = true;
    if (mode >= 0 && mode < FEATURES.size()) {
        gd::PI32 features = FEATURES[mode];
        gd::Int feature_count = features.size();
        for (gd::Int k = 0; k < feature_count; ++k) {
            region->put8(0x54 + k, features[k]);
        }
    }
    switch (mode) {
        case GameMode::ARCADE:
            _clear_stage_records(region);
            break;
        case GameMode::VS:
        case GameMode::BALL:
            if (mode == GameMode::VS) {
                globals->player_active[1] = 1;
                globals->player_active[0] = 1;
            } else {
                fight->round_time_option = 5;
                fight->chip_damage = 0;
            }
            region->put8(0x8A, 3);
            region->put8(0x8E, 3);
            region->put32(0x90, region->u16(0x92) << 16);
            region->put16(0x88, 0);
            region->put16(0x8C, 0);
            region->put32(0x98, 0xFFFFFFFFLL);
            region->put32(0x9C, 0xFFFFFFFFLL);
            region->put32(0x94, 0xFFFFFFFFLL);
            break;
        case GameMode::TEAM:
            region->rounds_option = 0;
            region->put8(0xB5, 4);
            region->put8(0xC2, 4);
            for (gd::Int k = 0; k < 22; ++k) {
                region->put16(0xEE - 2 * k, 0);
            }
            break;
        case GameMode::TIME_ATTACK:
            region->character_change = 0;
            fight->ai_difficulty = 1;
            region->rounds_option = 1;
            fight->round_time_option = 2;
            fight->chip_damage = 0;
            _clear_stage_records(region);
            break;
        case GameMode::SURVIVAL:
            fight->ai_difficulty = 1;
            region->rounds_option = 0;
            fight->round_time_option = 2;
            fight->chip_damage = 0;
            region->put32(0x90, 0);
            region->put32(0x94, 0);
            region->put32(0x98, 0);
            for (gd::Int k = 0; k < 22; ++k) {
                region->put16(0xDA - 2 * k, 0);
            }
            region->put32(0x9C, 0);
            for (gd::Int k = 0; k < 4; ++k) {
                region->put32(0xAC - 4 * k, NO_CHARACTER);
            }
            break;
        case GameMode::PRACTICE:
            region->rounds_option = 0;
            fight->round_time_option = 5;
            fight->practice = 1;
            fight->chip_damage = 0;
            tail = false;
            break;
        case GameMode::DEMO:
            globals->controller[0] = 0;
            globals->controller[1] = 0;
            fight->round_time_option = 2;
            fight->attract = 1;
            _demo_fighters(g);
            tail = false;
            break;
        case GameMode::FORCE:
            fight->round_time_option = 4;
            region->rounds_option = 0;
            fight->chip_damage = 0;
            break;
        default:
            tail = false;
            break;
    }
    return tail;
}

// FUN_80051644: the arcade and time attack stage records (ten of 8 bytes at 0x800AFF90).
void ModeStart::_clear_stage_records(const gd::Ref<ModeRegion>& region) {
    for (gd::Int k = 0; k < 10; ++k) {
        region->put32(0x90 + 8 * k, 0);
        region->put8(0x94 + 8 * k, 0);
        region->put8(0x95 + 8 * k, 0);
        region->put16(0x96 + 8 * k, 0);
    }
}

// FUN_800DB54C: the demonstration fight's characters: the next of the list, and a random other
// one; both in their second costume when bit 9 of the total usage is set.
void ModeStart::_demo_fighters(GameFlow* g) {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Ref<GameGlobals> globals = g->globals;
    gd::Int total = 0;
    for (gd::Int c = 0; c < 22; ++c) {
        total += progress->usage(c);
    }
    gd::PI32 chars;
    for (gd::Int c : g->data->demo_characters) {
        if ((gd::shr(progress->unlocked, c & 31) & 1) != 0) {
            chars.append(c);
        }
    }
    gd::Int n = chars.size();
    gd::Int i = progress->demo_position + 1;
    if (n <= i) {
        i = 0;
    }
    progress->demo_position = i;
    gd::Int costume = (total & 0x200) != 0 ? 1 : 0;
    globals->player_cpu[0] = 1;
    globals->player_char[0] = chars[i];
    globals->player_costume[0] = costume;
    mirror_costumes(globals, 0);
    globals->player_cpu[1] = 1;
    gd::Int r = g->fight->vblank & 0xFFF;
    gd::Int q = n > 1 ? gd::mod(r, n - 1) : r;
    globals->player_char[1] = chars[gd::mod(q + i + 1, n)];
    globals->player_costume[1] = costume;
    mirror_costumes(globals, 1);
}

// FUN_8004F3CC: in a mirror match the side that chose later (unless only the other one is a
// human) takes another costume: 0 and 1 swap, 2 and 3 become 0.
void ModeStart::mirror_costumes(const gd::Ref<GameGlobals>& globals, gd::Int player) {
    gd::Int other = (player + 1) & 1;
    gd::PI32 a{globals->player_char[player], globals->player_costume[player],
        globals->player_keep[player], globals->player_active[player]};
    gd::PI32 b{globals->player_char[other], globals->player_costume[other],
        globals->player_keep[other], globals->player_active[other]};
    if (b[2] != 0 && a[0] == b[0] && a[1] == b[1]) {
        if (b[3] == 0 && a[3] != 0) {
            b[1] = a[1] < 2 ? (gd::Int(a[1]) ^ 1) : 0;
        } else {
            a[1] = b[1] < 2 ? (gd::Int(b[1]) ^ 1) : 0;
        }
    }
    globals->player_char[player] = a[0];
    globals->player_costume[player] = a[1];
    globals->player_keep[player] = 1;
    globals->player_char[other] = b[0];
    globals->player_costume[other] = b[1];
}

}  // namespace t3
