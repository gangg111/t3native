// remake/core/game/match_flow.gd
// FightMain (0x80050710, game state 8; modes.md#match-flow): character select, the mode's start
// and fight hooks, the VS screen, loading the fighters, the rounds, the mode's end hook, and the
// continue, game over and new challenger screens (sub-states 9–14, screens_sim.fight_main).
#include "core/game/match_flow.h"

#include "core/camera/camera_director.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_hud.h"
#include "core/fight/round_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/game/mode_rules.h"
#include "core/game/mode_start.h"
#include "core/game/ogre_scene.h"
#include "core/math/fx.h"
#include "platform/pad_state.h"

namespace t3 {

MatchFlow::MatchFlow(GameFlow* flow) : FlowPart(flow) {
    ogre = gd::make<OgreScene>(flow);
}

// FUN_80051204: the mode (or a challenger) asks for quick select.
bool MatchFlow::quick_select() { return (g->region->u32(0x5C) & QUICK_BYTES) != 0; }

void MatchFlow::step() {
    if (g->menu_exit()) {
        return;
    }
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightSimulation> sim = g->sim;
    switch (g->sub) {
        case Sub::SELECT:
            _select();
            break;
        case Sub::MODE_START:
            _mode_start();
            break;
        case Sub::NEXT_FIGHT:
            _next_fight();
            break;
        case Sub::LOAD:
        case Sub::LOAD_WAIT:
            if (g->sub == Sub::LOAD) {
                gd::Ref<GameGlobals> gl = g->globals;
                sim->load_fighters(gl->player_char, gl->player_costume, gl->player_cpu);
                g->sub = Sub::LOAD_WAIT;
            }
            if (((g->fight->vblank - sim->state_timer) & 0xFFFFFFFFLL) > FIRST_ROUND_FRAMES) {
                g->sub = Sub::DISPLAY_OFF;
            }
            break;
        case Sub::DISPLAY_OFF:
            g->display(1);
            g->sub = Sub::DISPLAY_ON;
            break;
        case Sub::DISPLAY_ON:
            g->display(0);
            g->sub = Sub::ROUND_START;
            break;
        case Sub::ROUND_START:
            sim->camera->overhead = 0;
            sim->round_start();
            g->fight->hud_shown = region->mode != GameMode::PRACTICE ? 1 : 0;
            g->fight->challenger_lock = 0;
            region->challenger = 0;
            sim->state_timer = CHALLENGER_WAIT;
            g->sub = Sub::FIGHT;
            break;
        case Sub::FIGHT:
            _fight();
            break;
        case Sub::CONTINUE_START:
        case Sub::CONTINUE:
            _continue(g->sub == Sub::CONTINUE_START);
            break;
        case Sub::GAME_OVER_START:
        case Sub::GAME_OVER:
            _game_over(g->sub == Sub::GAME_OVER_START);
            break;
        case Sub::CHALLENGER_START:
        case Sub::CHALLENGER:
            _challenger(g->sub == Sub::CHALLENGER_START);
            break;
        case Sub::OGRE_DISPLAY_ON:
            g->display(0);
            g->sub = Sub::OGRE_SETUP;
            break;
        case Sub::OGRE_SETUP:
            ogre->setup();
            g->sub = Sub::OGRE;
            break;
        case Sub::OGRE: {
            bool over = ogre->step();
            if (ogre->fade_drawn >= 0) {
                g->fades.append(ogre->fade_drawn);
            }
            if (over || (g->pressed(region->human_index) & PadState::START) != 0) {
                ogre->end();
                g->sub = Sub::OGRE_DISPLAY_OFF;
            }
            break;
        }
        case Sub::OGRE_DISPLAY_OFF:
            g->display(1);
            g->sub = Sub::TRUE_OGRE_DISPLAY_ON;
            break;
        case Sub::TRUE_OGRE_DISPLAY_ON:
            g->display(0);
            g->sub = Sub::TRUE_OGRE;
            break;
        case Sub::TRUE_OGRE:
            _true_ogre();
            break;
        case Sub::AREA_DISPLAY_OFF:
            g->display(1);
            g->sub = Sub::AREA_DISPLAY_ON;
            break;
        case Sub::AREA_DISPLAY_ON:
            g->display(0);
            g->sub = Sub::FORCE_BOSS;
            break;
        case Sub::FORCE_BOSS:
            // Tekken Force's area change: the level's boss (FUN_800B2B3C).
            sim->force_boss();
            g->sub = Sub::FIGHT;
            break;
        case Sub::DEMO_OVER:
            // The demonstration fight is over: the ranking, or the title at attract step 3.
            g->controllers_reset();
            if (g->progress->attract_step != 3) {
                g->goto_ranking();
            } else {
                g->goto_transition(GameFlow::State::TITLE);
            }
            break;
        default:
            // Practice's RESET (EXIT) and others.
            g->controllers_reset();
            g->sound(GameFlow::SOUND_START);
            g->goto_transition(GameFlow::State::MENU);
            break;
    }
}

// Character select (or quick select); the demonstration fight skips it.
void MatchFlow::_select() {
    gd::Ref<ModeRegion> region = g->region;
    g->fight->challenger_lock = 0;
    if (region->mode == GameMode::DEMO) {
        g->globals->controller[0] = 0;
        g->globals->controller[1] = 0;
        g->sub = Sub::MODE_START;
        _mode_start();
    } else {
        g->globals->controller[0] = g->progress->controller(0);
        g->globals->controller[1] = g->progress->controller(1);
        region->vs_next_state = GameFlow::State::FIGHT;
        region->vs_next_sub = Sub::MODE_START;
        if (!quick_select()) {
            g->queue_overlay(GameFlow::SLOT_SCREEN, GameFlow::OVERLAY_SELECT);
            g->goto_loader(GameFlow::State::SELECT, 0);
        } else {
            g->display(1);
            g->state = GameFlow::State::QUICK_SELECT;
            g->sub = Sub::SELECT;
        }
    }
}

// CONTINUE?: the loser's countdown over the frozen fight; the same fight, select again, or game
// over.
void MatchFlow::_continue(bool first) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightSimulation> sim = g->sim;
    if (first) {
        g->system_sound(SYSTEM_CONTINUE);
        g->music_volume(MUSIC_FADE, MUSIC_FADE);
        sim->state_timer = CONTINUE_FRAMES;
        region->continuing = 1;
        g->sub = Sub::CONTINUE;
    }
    sim->fight_frame();
    gd::Int r = _continue_countdown(region->match_loser);
    if (r == 1 || r == 2) {
        g->sub = r == 1 ? Sub::NEXT_FIGHT : Sub::SELECT;
        region->continuing = 0;
        g->globals->set_tie_winner(g->fight, region->match_winner);
    } else if (r != 0) {
        g->sub = Sub::GAME_OVER_START;
        region->continuing = 0;
    }
}

// GAME OVER over the fight, then the ranking.
void MatchFlow::_game_over(bool first) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightSimulation> sim = g->sim;
    if (first) {
        sim->state_timer = 0;
        g->globals->player_active[region->match_loser] = 0;
        g->system_sound(SYSTEM_GAME_OVER);
        g->music(MUSIC_GAME_OVER);
        g->sub = Sub::GAME_OVER;
    }
    g->texts.append(gd::make<RoundHud::Message>("GAME OVER", 1, 2, 0x55, 200));
    sim->fight_frame();
    sim->state_timer += 1;
    if (sim->state_timer >= GAME_OVER_FRAMES) {
        g->goto_ranking();
    }
}

// A NEW CHALLENGER ENTERS!!: the fight freezes, then character select (or quick select at once).
void MatchFlow::_challenger(bool first) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightSimulation> sim = g->sim;
    bool done = false;
    if (first) {
        g->sound(SOUND_CHALLENGER);
        g->music_volume(0, 0x1E);
        sim->state_timer = CHALLENGER_WAIT;
        region->challenger = 1;
        done = quick_select();
        if (!done) {
            g->sub = Sub::CHALLENGER;
        }
    }
    if (!done) {
        if (sim->fight_frame() > 0 || sim->state_timer >= CHALLENGER_FREEZE) {
            g->fight->freeze = 1;
        }
        gd::Int t = sim->state_timer;
        if ((t & 0x18) != 0) {
            g->texts.append(gd::make<RoundHud::Message>("A NEW CHALLENGER\n     ENTERS!!", 7, 2, 8, 0x120));
        }
        if (t == CHALLENGER_LOAD) {
            g->slots[GameFlow::SLOT_SCREEN] = GameFlow::OVERLAY_SELECT;
        }
        sim->state_timer = t + 1;
        done = t + 1 >= CHALLENGER_FRAMES;
    }
    if (done) {
        g->display(1);
        if (region->team_members != 0) {
            g->globals->player_keep[1] = 0;
            g->globals->player_keep[0] = 0;
        }
        g->sub = Sub::SELECT;
    }
}

// Sub-state TRUE_OGRE: the boss of the last stage becomes True Ogre, and the match goes on with the
// same round score (RoundTallySave / RoundTallyRestore; one-round matches restart at 0).
void MatchFlow::_true_ogre() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<GameGlobals> gl = g->globals;
    region->ctx_put8(ModeRules::LADDER + 4 * 9, Character::TRUE_OGRE);
    region->match_result = 0;
    region->match_winner = 0;
    region->match_loser = 0;
    region->put8(0x69, 0);
    g->modes->fight_start();
    g->modes->stage_and_music();
    if (fight->max_rounds < 2) {
        fight->rounds_played = 0;
        fight->fighters[0]->round_wins = 0;
        fight->fighters[1]->round_wins = 0;
    }
    gd::Int played = fight->rounds_played;
    gd::PI32 wins{fight->fighters[0]->round_wins, fight->fighters[1]->round_wins};
    g->sim->load_fighters(gl->player_char, gl->player_costume, gl->player_cpu);
    fight->rounds_played = played;
    fight->fighters[0]->round_wins = wins[0];
    fight->fighters[1]->round_wins = wins[1];
    g->sub = Sub::DISPLAY_OFF;
}

// Sub-state MODE_START: the mode's start hook, then the match counters (falls into NEXT_FIGHT).
void MatchFlow::_mode_start() {
    g->modes->mode_start();
    g->region->mode_started = 1;
    g->sub = Sub::NEXT_FIGHT;
    g->fight->command_scroll_reset();
    _next_fight();
}

// Sub-state NEXT_FIGHT: the humans, the mode's fight hook, the stage and music, then the VS screen.
void MatchFlow::_next_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::PI32 active = g->globals->player_active;
    region->human_count = (active[0] != 0 ? 1 : 0) + (active[1] != 0 ? 1 : 0);
    region->human_mask = (active[0] != 0 ? 1 : 0) | (active[1] != 0 ? 2 : 0);
    g->fight->human_mask = region->human_mask;
    switch (gd::Int(region->human_mask)) {
        case 0:
        case 3:
            region->human_index = 0;
            region->cpu_index = 0;
            break;
        case 1:
            region->human_index = 0;
            region->cpu_index = 1;
            break;
        case 2:
            region->human_index = 1;
            region->cpu_index = 0;
            break;
        default:
            break;
    }
    region->match_result = 0;
    region->match_winner = 0;
    region->match_loser = 0;
    region->fight_clock = region->clock_start;
    g->modes->fight_start();
    if (region->mode == GameMode::FORCE) {
        // FUN_800B5910 with the human's pad, and one human on side 0.
        g->fight->force->manual_target_setup(g->sim->pads->physical[g->fight->force->human()]);
        region->human_mask = 1;
        g->fight->human_mask = 1;
        region->human_index = 0;
        region->cpu_index = 1;
    }
    g->sim->state_timer = g->fight->vblank;
    g->modes->stage_and_music();
    region->vs_next_state = GameFlow::State::FIGHT;
    region->vs_next_sub = Sub::LOAD;
    g->state = GameFlow::State::VS;
    g->sub = Sub::SELECT;
    g->display(1);
}

// Sub-state FIGHT: the fight, challengers, and the round's hand-over.
void MatchFlow::_fight() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<FightSimulation> sim = g->sim;
    gd::Int timer = sim->state_timer - 1;
    if (sim->state_timer == 0) {
        timer = 0;
        if (region->challengers != 0) {
            bool a = challenger_join(0);
            bool b = challenger_join(1);
            if (a || b) {
                g->sub = Sub::CHALLENGER_START;
                return;
            }
        }
    }
    sim->state_timer = timer;
    gd::Int result = sim->fight_frame();
    if (region->mode == GameMode::DEMO && result > RoundState::RESULT) {
        g->sub = Sub::DEMO_OVER;
        return;
    }
    if (result == RoundState::AREA_CHANGE) {
        g->sub = Sub::AREA_DISPLAY_OFF;
        return;
    }
    if (result < RoundState::END) {
        return;
    }
    if (result == RoundState::OGRE_SCENE) {
        g->display(1);
        if (region->mode != GameMode::TIME_ATTACK) {
            g->sub = Sub::OGRE_DISPLAY_ON;
        } else {
            g->sub = Sub::TRUE_OGRE_DISPLAY_ON;
        }
        return;
    }
    if (result == RoundState::PRACTICE_EXIT) {
        g->sub = Sub::EXIT;
        return;
    }
    if (region->mode == GameMode::PRACTICE) {
        g->sub = Sub::SELECT;
        g->globals->player_active[g->globals->other_player] = 0;
        g->globals->player_keep[1] = 0;
        g->globals->player_keep[0] = 0;
        return;
    }
    if (region->match_result == 0) {
        g->sub = Sub::DISPLAY_OFF;
        return;
    }
    g->modes->match_end();
}

// ChallengerJoin (FUN_80051244): a player who is not playing presses Start.
bool MatchFlow::challenger_join(gd::Int player) {
    if ((g->pressed(player) & PadState::START) == 0 || g->fight->challenger_lock != 0) {
        return false;
    }
    if (g->globals->player_active[player] != 0) {
        return false;
    }
    gd::Int held = g->held(player);
    if ((held & PadState::START) != 0) {
        g->region->challenger_quick = held == ModeStart::QUICK_CHALLENGE ? 1 : 0;
    }
    g->globals->player_active[player] = 1;
    g->globals->player_keep[player] = 0;
    g->globals->other_player = player;
    return true;
}

// FUN_800502D8: 1 the same fight again, 2 back to select, 0 counting, −1 over. CONTINUE? n
// counts down from 9, with system sound 4 + n for each digit from 8 on.
gd::Int MatchFlow::_continue_countdown(gd::Int player) {
    gd::Int pressed = g->pressed(player);
    gd::Ref<FightSimulation> sim = g->sim;
    if ((pressed & PadState::START) != 0) {
        g->globals->player_active[player] = 1;
        g->sound(GameFlow::SOUND_START);
        if (g->region->character_change == 0) {
            g->globals->player_keep[player] = 1;
            return 1;
        }
        g->globals->player_keep[player] = 0;
        return 2;
    }
    gd::Int t = sim->state_timer;
    if ((pressed & GameFlow::FACE_BUTTONS) != 0) {
        t = t < DIGIT_FRAMES ? 0 : t - DIGIT_FRAMES;
        sim->state_timer = t;
    }
    if (t == 0) {
        return -1;
    }
    if (t > 0) {
        sim->state_timer = t - 1;
    }
    gd::Int shown = sim->state_timer + DIGIT_FRAMES;
    gd::Int n = Fx::div_trunc(shown, DIGIT_FRAMES);
    if (n >= 0 && n < 9 && shown - DIGIT_FRAMES * n == DIGIT_FRAMES - 1) {
        g->system_sound(n != 0 ? 0x1004 + n : SYSTEM_GAME_OVER);
    }
    g->texts.append(gd::make<RoundHud::Message>("CONTINUE? " + gd::str(n), 9, 2, 0x3F, 200));
    return 0;
}

}  // namespace t3
