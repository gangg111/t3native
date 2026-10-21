// remake/core/game/game_flow.gd
// The whole game as a frame-stepped state machine (modes.md#game-states): the main loop's frame
// work and the handler of the game state 0x800AE6CC, with the game's own states and sub-states,
// so that `tests/app/test_game_flow.gd` can compare every frame with the game running in the
// CPU harness (tools/research/flow_trace.py).
//
// The fight is the session's FightSimulation; its FightState holds the game's globals the fight
// code shares (the mode region, the progress block, the random generators). Screens with state of
// their own are separate objects (MainMenu, QuickSelect, CharacterSelect, VsScreen, the results,
// the ranking, the ending and Theater, the options). Loading takes no time: an overlay load costs
// the frames the loader state takes with an idle disc, as in the harness.
#include "core/game/game_flow.h"

#include "content/flow_data.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_reel.h"
#include "core/camera/camera_shake.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/pause_menu.h"
#include "core/fight/replay_state.h"
#include "core/fight/round_state.h"
#include "core/fight/vibration.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/character_select.h"
#include "core/game/ending_screen.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/main_menu.h"
#include "core/game/match_flow.h"
#include "core/game/mode_region.h"
#include "core/game/mode_rules.h"
#include "core/game/mode_start.h"
#include "core/game/options_screen.h"
#include "core/game/practice_mode.h"
#include "core/game/progress_rules.h"
#include "core/game/quick_select.h"
#include "core/game/ranking_screen.h"
#include "core/game/result_screens.h"
#include "core/game/theater_screen.h"
#include "core/game/vs_screen.h"
#include "core/math/c_rand.h"
#include "core/scripted/enbu_performance.h"
#include "core/sim_events.h"

namespace t3 {

GameFlow::GameFlow(const gd::Ref<FightContent>& fight_content, const gd::Ref<FlowData>& flow_data,
                   const gd::Ref<RuleSet>& fight_rules, gd::Int seed) {
    globals = gd::make<GameGlobals>();
    content = fight_content;
    data = flow_data;
    sim = gd::make<FightSimulation>(content, nullptr, fight_rules);
    fight = sim->fight;
    progress = fight->progress;
    region = fight->region;
    progress->bytes = GameProgress::defaults(data->progress)->bytes;
    // duplicate(true): a deep copy (gd::Arr::duplicate is shallow), each table copied.
    key_tables = gd::Arr<gd::PI32>();
    for (const gd::PI32& table : data->key_tables) key_tables.append(table.duplicate());
    rules = gd::make<ProgressRules>(progress, globals, region, data, fight_rules);
    modes = gd::make<ModeRules>(this);
    menu = gd::make<MainMenu>(this);
    quick = gd::make<QuickSelect>(this);
    select = gd::make<CharacterSelect>(this);
    ranking = gd::make<RankingScreen>(this);
    results = gd::make<ResultScreens>(this);
    ending = gd::make<EndingScreen>(this);
    theater = gd::make<TheaterScreen>(this);
    options = gd::make<OptionsScreen>(this);
    practice = gd::make<PracticeMode>(this);
    sim->practice = practice;
    // Callable practice.record_pad: a lambda over the PracticeMode.
    gd::Ref<PracticeMode> recorder = practice;
    fight->practice_pads = [recorder](gd::Int index, gd::Int word) { recorder->record_pad(index, word); };
    vs_screen = gd::make<VsScreen>(this);
    match_flow = gd::make<MatchFlow>(this);
    // The globals' values in the executable image (the fight simulation's own defaults are VS's).
    fight->round_time_option = 0;
    fight->human_mask = 0;
    fight->ai_difficulty = 0;
    fight->ai_level = 0;
    fight->tie_choice = 0;
    fight->stage = 0;
    fight->music = 0;
    // BootInit: the random generators (seeded from the scratchpad's contents by the game).
    fight->rng->set_seed(seed);
    fight->camera_rng = seed & 0xFFFFFFFFLL;
    fight->frame_rng = seed & 0xFFFFFFFFLL;
    fight->attract = 1;
    globals->display_on = 1;
}

// One frame of the main loop with the pads' physical buttons.
void GameFlow::step(const gd::PI32& buttons) {
    if (movie_blocking) {
        if (movie_result == 0) {
            // The ending's movie player reads the pads every frame it plays (FUN_8010D7E0 calls
            // FUN_8002A014): the button that skips it, still held after it, is no new press. The
            // step that finishes the frame reads none (a press there reaches the next frame).
            sim->pads->read(buttons, key_tables);
            return;
        }
        // The rest of the frame that started the movie: the movie it asked for has played, so the
        // owner must not start it again (the other outputs stay those of that frame).
        movie_blocking = false;
        movie_started = -1;
        ending->after_movie();
        return;
    }
    sim->begin_frame(buttons, key_tables);
    presents_mode = -1;
    picture = -1;
    fades = gd::PI32();
    movie_started = -1;
    performed = false;
    texts.clear();
    save_requested = false;
    _update_escape();
    switch (state) {
        case State::BOOT:
            _boot();
            break;
        case State::LOADER:
            _loader();
            break;
        case State::TRANSITION:
            _transition();
            break;
        case State::TITLE:
            _title();
            break;
        case State::MENU:
            menu->step();
            break;
        case State::OPTIONS:
            options->step();
            break;
        case State::ENBU:
            _enbu();
            break;
        case State::PREPARE:
            _fight_prepare();
            break;
        case State::FIGHT:
            match_flow->step();
            if (fight->save_request) {
                fight->save_request = false;
                rules->request_save();
            }
            break;
        case State::SELECT:
            select->step();
            break;
        case State::QUICK_SELECT:
            quick->step();
            break;
        case State::VS:
            vs_screen->step();
            break;
        case State::TEAM_RESULT:
            results->team_step();
            break;
        case State::TIME_RESULT:
            results->time_attack_step();
            break;
        case State::SURVIVAL_RESULT:
            results->survival_step();
            break;
        case State::FORCE_RESULT:
            results->force_step();
            break;
        case State::RANKING_LOAD:
            ranking->load_step();
            break;
        case State::RANKING:
            ranking->step();
            break;
        case State::SCREEN_LOADER:
            _screen_loader();
            break;
        case State::ENDING:
            ending->step();
            break;
        default:
            goto_transition(State::MENU);
            break;
    }
}

// The pads as the screens read them.
gd::Int GameFlow::pressed(gd::Int player) { return sim->pads->physical_pressed[player]; }

gd::Int GameFlow::held(gd::Int player) { return sim->pads->physical[player]; }

gd::Int GameFlow::repeat(gd::Int player) { return sim->pads->repeat[player]; }

gd::Int GameFlow::pressed_any() { return pressed(0) | pressed(1); }

// SoundPlayFighter(0, code, 0): the menus' sounds.
void GameFlow::sound(gd::Int code) { sim->events->add(SimEvents::SOUND, -1, code, 0); }

void GameFlow::system_sound(gd::Int code) { sim->events->add(SimEvents::SYSTEM_SOUND, -1, code & 0xFFF); }

// MusicPlay (the track, −1 stops).
void GameFlow::music(gd::Int track) { sim->events->add(SimEvents::Kind::MUSIC, -1, track); }

// FUN_8006C870: the music stops.
void GameFlow::music_stop() { sim->events->add(SimEvents::Kind::MUSIC_STOP, -1); }

// A disc read (BnsStartQueuedLoads: the overlay loader, the menu's overlay, the VS screen's
// pictures, Tekken Ball's and Force's stages; a movie, as the movie players start):
// FUN_8006B834(0) pauses the drive, and with it the XA music, which does not resume.
void GameFlow::disc_read() { music_stop(); }

void GameFlow::music_volume(gd::Int volume, gd::Int frames) {
    sim->events->add(SimEvents::Kind::MUSIC_VOLUME, -1, volume, frames);
}

// A movie played within the frame (FUN_8010D474): the frame finishes when it ends.
void GameFlow::start_blocking_movie(gd::Int id) {
    disc_read();
    fight->stage_cached = -1;           // FUN_8006C870(0x1F): the movie's buffers overwrite it
    movie_result = 0;
    movie_started = id;
    movie_blocking = true;
    if (!movies_available) {
        movie_result = 1;
    }
}

// FUN_8006BCC8: whether the music has started. The remake's music starts at once, as the game's
// does with no stream playing: the first call of the session only counts the stream's timer
// down, later calls run FUN_8006AE4C (the volume up).
bool GameFlow::music_ready() {
    if (music_countdown < 0) {
        music_volume(0x7F, -1);
    } else {
        music_countdown -= 1;
    }
    return true;
}

// FUN_80029860: the display switch (0: on).
void GameFlow::display(gd::Int off) { globals->display_on = off == 0 ? 1 : 0; }

// ---- state changes ----------------------------------------------------------------------------

// FUN_8004FBE0: go to a state through the transition screen. Going to the title plays the next
// attract demonstration at attract step 1 (or the one R1 / L1 / L1 + R1 choose).
void GameFlow::goto_transition(gd::Int target) {
    display(1);
    progress->enbu_target = 0;
    if (target == State::TITLE) {
        gd::Int demo = progress->demo_number;
        gd::Int buttons = held(0);
        if ((buttons & ~(PadState::L1 | PadState::R1) & 0xFFFF) != 0) {
            buttons = 0;
        }
        gd::Int top = progress->unlock_class;
        if (top == 0 || buttons == 0) {
            if (progress->attract_step == 1) {
                demo += 1;
            }
        } else {
            gd::Int choice = demo;
            switch (buttons) {
                case PadState::R1:
                    choice = 0;
                    break;
                case PadState::L1:
                    choice = 1;
                    break;
                case PadState::L1 | PadState::R1:
                    choice = 2;
                    break;
                default:
                    break;
            }
            if (top < choice) {
                choice = progress->demo_number;
            } else {
                progress->attract_step = 1;
            }
            demo = choice;
        }
        if (top < demo) {
            demo = 0;
        }
        progress->demo_number = demo;
        progress->enbu_target = 1;
        if (progress->attract_step == 1) {
            target = State::ENBU;
        } else {
            progress->enbu_target = 0;
        }
    }
    progress->transition_target = target;
    progress->previous_state = state;
    state = target_state(State::TRANSITION);
    sub = 0;
}

gd::Int GameFlow::target_state(gd::Int s) { return s; }

// FUN_80050208: go to a state through the screen-overlay loader.
void GameFlow::goto_screen_loader(gd::Int target) {
    display(1);
    progress->previous_state = state;
    state = State::SCREEN_LOADER;
    progress->transition_target = target;
    sub = 0;
}

// FUN_80050478.
void GameFlow::goto_ranking() {
    display(1);
    state = State::RANKING_LOAD;
    sub = 0;
}

// FUN_80052EC8: a load for the overlay loader.
void GameFlow::queue_overlay(gd::Int slot, gd::Int overlay) {
    if (loader_queue.size() < 8) {
        loader_queue.append(gd::PI32{slot, overlay});
    }
}

// FUN_80052F18: to `to` (and `to_sub`), through the loader when loads are queued.
void GameFlow::goto_loader(gd::Int to, gd::Int to_sub) {
    if (loader_queue.is_empty()) {
        state = target_state(to);
        sub = to_sub;
        return;
    }
    display(1);
    loader_target = gd::Vector2i(to, to_sub);
    state = State::LOADER;
    sub = 0;
}

// FUN_80052FAC (game state 1): one load per frame with an idle disc.
void GameFlow::_loader() {
    switch (sub) {
        case 0:
            display(0);
            sub = 1;
            break;
        case 1:
        case 2:
            if (sub == 1) {
                gd::PI32 load = loader_queue[0];
                slots[load[0]] = load[1];
                disc_read();
                sub = 2;
            }
            loader_queue.remove_at(0);
            if (loader_queue.is_empty()) {
                state = target_state(loader_target.x);
                sub = loader_target.y;
            } else {
                sub = 1;
            }
            break;
        case 3:
            if (loader_queue.is_empty()) {
                state = target_state(loader_target.x);
                sub = loader_target.y;
            } else {
                sub = 1;
            }
            break;
        case 4:
            state = target_state(loader_target.x);
            sub = loader_target.y;
            break;
        default:
            break;
    }
}

// ScreenOverlayLoad (0x8005025C, game state 18).
void GameFlow::_screen_loader() {
    display(0);
    fight->stage_cached = -1;           // FUN_8006C870(0x1F): the overlay goes where the stage was
    controllers_reset();
    gd::Int target = progress->transition_target;
    if (target == State::ENDING) {
        ending->loaded();
    }
    queue_overlay(SLOT_SCREEN, target == State::ENDING ? OVERLAY_ENDING : OVERLAY_RESULT);
    goto_loader(target, 0);
}

// FUN_800291B0: the vibration off and the controller settings cleared.
void GameFlow::controllers_reset() {
    for (gd::Int p = 0; p < 2; ++p) {
        globals->controller[p] = 0;
    }
}

// MenuExitCheck (FUN_80051304): Select + Start (Start alone in the demonstration fight) or the
// pause menu's RESET returns to the main menu.
// The remake's Escape on an open pause menu: leaves it as CANCEL (practice: OK) does; false
// while the menu is not taking input yet.
bool GameFlow::_escape() {
    if (region->mode == GameMode::PRACTICE && practice != nullptr) {
        return practice->escape();
    }
    return PauseMenu::escape(fight, sim->events);
}

// The remake's Escape on a pause menu, once per frame while it is pending: the menu takes it
// (`_escape`) as soon as it listens. It stays pending through the menu's inert frames and is
// dropped once taken, when no menu is open or when the menu has left its list (Tekken Ball's HOW TO
// page, a confirmed item): Escape does not wait for the page to close.
void GameFlow::_update_escape() {
    if (!escape_pressed) {
        return;
    }
    if (!pause_menu_open() || fight->pause_page != 0) {
        escape_pressed = false;
        return;
    }
    escape_pressed = !_escape();
}

// A pause menu is open and takes the remake's Escape (the move lists take it as Start).
bool GameFlow::pause_menu_open() {
    if (state != State::FIGHT) {
        return false;
    }
    if (region->mode == GameMode::PRACTICE && practice != nullptr) {
        return practice->s->u8(PracticeMode::PAUSED) != 0 && practice->s->u8(PracticeMode::COMMAND_LIST) == 0;
    }
    return fight->paused_player != 0 && fight->pause_page != PauseMenu::PAGE_COMMAND;
}

// The pad the fight's human plays on (practice: its player's): the one whose Start pauses, or −1
// with no human (the demonstration).
gd::Int GameFlow::fight_pad() {
    gd::Int humans = human_pads();
    if ((humans & 1) != 0) {
        return 0;
    }
    return (humans & 2) != 0 ? 1 : -1;
}

// The pads humans play the fight on, a bit per pad: practice its player's only, else the human
// mask's.
gd::Int GameFlow::human_pads() {
    if (region->mode == GameMode::PRACTICE) {
        return gd::shl(1, fight->tie_choice);
    }
    return fight->human_mask & 3;
}

// Whether a pad is played by a human in the fight.
bool GameFlow::plays_fight(gd::Int pad) { return (human_pads() & gd::shl(1, pad)) != 0; }

// The pad the remake's Escape presses for when the keyboard is `keyboard_pad`'s, or −1 for that
// pad itself: in a fight (and its preparation) the human's when the keyboard's player does not
// play there (Start of a pad nobody plays would join as a challenger).
gd::Int GameFlow::escape_pad(gd::Int keyboard_pad) {
    if (state != State::FIGHT && state != State::PREPARE) {
        return -1;
    }
    gd::Int human = fight_pad();
    if (human < 0 || plays_fight(keyboard_pad)) {
        return -1;
    }
    return human;
}

// The pad whose Start opens the pause now (the press FightSimulation._pause_check reads), or −1
// when none can: outside a fight or its round state FIGHT, in the demonstration and a practice
// replay (no human), or with a pause already open.
gd::Int GameFlow::pause_pad() {
    if (state != State::FIGHT || fight->pause_allowed == 0 || fight->round_state != RoundState::FIGHT
            || fight->pause_request != 0 || fight->paused_player != 0) {
        return -1;
    }
    if (region->mode == GameMode::PRACTICE && fight->replay_playing != 0) {
        return -1;
    }
    return fight_pad();
}

// The pad a tap or click presses Start on, or −1 for none: player 1's, but in a fight the human's
// (Start of a pad nobody plays there joins as a challenger), and not while a pause is open (the
// click that brings the window back would resume the fight).
gd::Int GameFlow::tap_pad() {
    if (state != State::FIGHT) {
        return 0;
    }
    if (fight->paused_player != 0 || fight->pause_request != 0) {
        return -1;
    }
    return gd::maxi(fight_pad(), 0);
}

bool GameFlow::menu_exit() {
    gd::Int p0 = pressed(0);
    gd::Int p1 = pressed(1);
    if (region->mode == GameMode::DEMO) {
        if (((p0 | p1) & PadState::START) == 0) {
            return false;
        }
    } else if (!((held(0) & PauseMenu::SOFT_RESET_MASK) == PauseMenu::SOFT_RESET && (p0 & PadState::SELECT) != 0)
            && !((held(1) & PauseMenu::SOFT_RESET_MASK) == PauseMenu::SOFT_RESET && (p1 & PadState::SELECT) != 0)
            && fight->pause_page != PauseMenu::PAGE_RESET) {
        return false;
    }
    fight->pause_page = 0;
    controllers_reset();
    sound(SOUND_START);
    goto_transition(State::MENU);
    return true;
}

void GameFlow::next_attract_step() {
    progress->attract_step = progress->attract_step + 1 > 3 ? 0 : progress->attract_step + 1;
}

// FUN_8004FB9C: the next attract step, skipping the title at step 3.
void GameFlow::attract_advance() {
    next_attract_step();
    if (progress->attract_step == 3) {
        next_attract_step();
    }
}

void GameFlow::_to_menu() {
    sound(SOUND_START);
    goto_transition(State::MENU);
}

// ---- boot (game state 0, BootState 0x800B0BD0) -------------------------------------------------

void GameFlow::_boot() {
    if (sub == 0) {
        sub = 1;
    } else {
        region->loaded_mode_overlay = 0xFF;
        goto_transition(State::TITLE);
    }
}

// ---- transition screen (TransitionScreen 0x8004FD48, menu_sim.transition_screen) ------------

void GameFlow::_transition() {
    if (progress->card_read != 0 && sub > 5 && sub < 11 && (pressed_any() & PadState::START) != 0) {
        goto_transition(State::MENU);
        sound(SOUND_START);
        return;
    }
    gd::Int mode = 3;
    gd::Int level = 0;
    switch (sub) {
        case 0: {
            display(0);
            fight->attract = 1;
            gd::Int previous = progress->previous_state;
            if (((previous - 2) & 0xFFFFFFFFLL) >= 4 || progress->screen_cached >= 4) {
                progress->screen_cached = 0;
            }
            if (progress->enbu_target != progress->enbu_cached) {
                progress->screen_cached = 0;
                progress->enbu_cached = progress->enbu_target;
            }
            sub = 5;
            gd::Int t = progress->transition_target;
            if (t != State::ENBU && (t != State::TITLE || progress->attract_step == 3)) {
                sub = 1;
            }
            break;
        }
        case 1:
        case 2:
        case 3:
        case 4:
            if (sub == 1) {
                _queue_menu_overlay();
            }
            if (sub <= 2) {
                auto_save();
            }
            _overlay_loaded();
            _overlay_ready();
            sub = 12;
            break;
        case 5:
        case 6:
            if (sub == 5) {
                _queue_menu_overlay();
                sim->state_timer = 0;
                sub = 6;
            }
            level = sim->state_timer << 3;
            sim->state_timer += 1;
            mode = 0;
            if (sim->state_timer > PRESENTS_FADE_FRAMES) {
                mode = 1;
                sub = 7;
                sim->state_timer = fight->vblank;
            }
            break;
        case 7:
        case 8:
        case 9:
            mode = 1;
            if (sub == 7) {
                if (progress->card_read == 0) {
                    // FUN_8004C658: the save is read at start-up.
                    if (card_loader) {
                        gd::PBytes saved = card_loader();
                        if (!saved.is_empty()) {
                            progress->load_save_data(saved);
                        }
                    }
                    progress->card_read = 1;
                } else {
                    auto_save();
                }
                sub = 8;
            }
            if (sub <= 8) {
                _overlay_loaded();
                sub = 9;
            }
            _overlay_ready();
            if (((fight->vblank - sim->state_timer) & 0xFFFFFFFFLL) > PRESENTS_HOLD) {
                sim->state_timer = PRESENTS_FADE_FRAMES;
                sub = 10;
            }
            break;
        case 10:
            level = sim->state_timer << 3;
            sim->state_timer -= 1;
            mode = 0;
            if (sim->state_timer < 1) {
                sim->state_timer = 0;
                sub = 11;
                mode = 2;
            }
            break;
        case 11:
        case 12:
            if (sub == 11) {
                mode = 2;
                sub = 12;
            }
            if (auto_save_error()) {
                return;
            }
            sub = 0;
            state = target_state(progress->transition_target);
            break;
        default:
            break;
    }
    presents_mode = mode;
    presents_level = mode == 0 ? level : 0;
}

void GameFlow::_queue_menu_overlay() {
    if (progress->screen_cached == 0) {
        slots[SLOT_SCREEN] = progress->enbu_target != 0 ? OVERLAY_ENBU : OVERLAY_TITLE;
        disc_read();
        progress->screen_cached = 0;
    }
}

void GameFlow::_overlay_loaded() {
    if (progress->screen_cached == 0) {
        progress->screen_cached = 2;
    }
}

void GameFlow::_overlay_ready() {
    if (progress->screen_cached < 3) {
        if (progress->enbu_target != 0) {
            // enbu.ovl FUN_800D3CA8: the demonstration's music is prepared.
            sim->events->add(SimEvents::Kind::MUSIC_PREPARE, -1, MUSIC_ENBU);
        }
        progress->screen_cached = 3;
    }
}

// AutoSave (FUN_8004C6A0): with AUTO SAVE on and a save pending, the owner writes the save file.
void GameFlow::auto_save() {
    if (progress->auto_save != 0 && globals->save_pending != 0) {
        save_requested = true;
        globals->save_error = 0;
    }
    globals->save_pending = 0;
}

// AutoSaveErrorShow (FUN_8004C758): 120 frames of AUTO SAVE ERROR! after a failed save.
bool GameFlow::auto_save_error() {
    if (globals->save_error == 0) {
        return false;
    }
    globals->save_error -= 1;
    return true;
}

// The owner reports a failed save: AUTO SAVE ERROR! shows for 120 frames.
void GameFlow::save_failed() { globals->save_error = 0x78; }

// ---- title sequence (title.ovl FUN_800DB7D4, menu_sim.title_sequence) ----------------------

void GameFlow::_title() {
    gd::Int fade = -1;
    switch (sub) {
        case 0:
            movie_result = 0;
            if (progress->attract_step == 3 || !movies_available) {
                sub = 3;
            } else {
                movie = progress->attract_step == 2 ? 1 : 0;
                sub = 1;
            }
            title_kind = PICTURE_FADE;
            sim->state_timer = 0;
            break;
        case 1:
            title_kind = PICTURE_MOVIE;
            movie_started = movie;
            disc_read();
            fight->stage_cached = -1;   // FUN_800E1614: FUN_8006C870(0x1F), as every movie player
            sub = 2;
            break;
        case 2:
            if (movie_result > 0) {
                sub = 6;
            }
            break;
        case 3:
        case 4:
        case 5:
            if (sub == 3) {
                title_kind = PICTURE_BLACK;
                sim->state_timer = 0;
                sub = 4;
            }
            if (sub != 5 && sim->state_timer + 10 < 0x100) {
                fade = sim->state_timer + 0x10A;
                sim->state_timer += 10;
            } else {
                if (sub != 5) {
                    sim->state_timer = 0;
                    sub = 5;
                }
                fades.append(0x200);
                sim->state_timer += 1;
                if (sim->state_timer >= WHITE_HOLD) {
                    sub = 6;
                }
            }
            break;
        case 6:
        case 7:
            if (sub == 6) {
                title_kind = PICTURE_TITLE;
                sim->state_timer = 0x100;
                sub = 7;
            }
            if (sim->state_timer <= 0) {
                sim->state_timer = TITLE_FRAMES;
                sub = 8;
            } else {
                fade = sim->state_timer + 0xFD;
                sim->state_timer -= 3;
            }
            break;
        case 8:
            title_kind = PICTURE_TITLE_PROMPT;
            sim->state_timer -= 1;
            if (sim->state_timer == 0) {
                sim->state_timer = 0x100;
                sub = 9;
            }
            break;
        case 9:
            if (sim->state_timer <= 0) {
                title_kind = PICTURE_FADE;
                sub = 10;
            } else {
                sim->state_timer -= 8;
                fade = sim->state_timer;
            }
            break;
        default:
            next_attract_step();
            title_kind = PICTURE_FADE;
            ModeStart::start(this, 6, 0);
            break;
    }
    if (fade >= 0) {
        fades.append(fade);
    }
    if ((pressed_any() & PadState::START) != 0 || movie_result == -1) {
        next_attract_step();
        controllers_reset();
        _to_menu();
        return;
    }
    _draw_picture(title_kind);
}

// ---- attract demonstration (enbu.ovl FUN_800D3844) -----------------------------------------

void GameFlow::_enbu() {
    if (sub > 0 && (pressed_any() & PadState::START) != 0) {
        next_attract_step();
        controllers_reset();
        music_stop();
        _to_menu();
        return;
    }
    switch (sub) {
        case 0:
            music_stop();
            sub = 1;
            enbu_kind = PICTURE_FADE;
            sim->state_timer = 0;
            break;
        case 1:
            enbu_kind = PICTURE_BLACK;
            _performance_count = 0;
            fight->stage_cached = -1;   // FUN_800D3844: FUN_8006C870(0x1F)
            _enbu_setup();
            performance = nullptr;
            if (performance_factory) {
                performance = performance_factory(progress->demo_number);
            }
            if (performance != nullptr) {
                performance->backdrop = fight->backdrop;
                performance->round_state = fight->round_state;
                performance->setup_view = sim->camera->view;
            }
            sub = 2;
            break;
        case 2: {
            enbu_kind = PICTURE_NONE;
            bool done = false;
            if (performance != nullptr) {
                gd::Int chooses = performance->reel->chooses;
                bool ready = true;
                if (performance->phase == EnbuPerformance::Phase::MUSIC_WAIT) {
                    ready = music_ready();
                }
                if (performance->phase == EnbuPerformance::Phase::SETUP) {
                    music(MUSIC_ENBU);
                    // FUN_800D3D64's set-up: the attract stage; CameraReset and the camera
                    // restart draw two random numbers before the reel's first choice.
                    fight->stage = ENBU_STAGE;
                    fight->stage_cached = ENBU_STAGE;   // the performance loads its stage itself
                    fight->rng->next();
                    fight->rng->next();
                }
                done = performance->step(ready);
                _performance_calls();
                // Each camera choice of the reel draws a random number (CameraChoose).
                gd::Int choices = performance->reel->chooses - chooses;
                for (gd::Int i = 0; i < choices; ++i) {
                    fight->rng->next();
                }
                performed = true;
                if (performance->fade_drawn >= 0) {
                    fades.append(performance->fade_drawn);
                }
            } else {
                _performance_count += 1;
                done = _performance_count == performance_frames;
            }
            if (done) {
                sub = 3;
            }
            break;
        }
        case 3:
            enbu_kind = PICTURE_NONE;
            sub = 4;
            break;
        case 4:
        case 5:
            if (sub == 4) {
                enbu_kind = PICTURE_TITLE;
                sim->state_timer = 0x100;
                sub = 5;
            }
            if (sim->state_timer < 1) {
                sim->state_timer = TITLE_FRAMES;
                sub = 6;
            } else {
                fades.append(sim->state_timer + 0xFD);
                sim->state_timer -= 3;
            }
            break;
        case 6:
            enbu_kind = PICTURE_TITLE_PROMPT;
            sim->state_timer -= 1;
            if (sim->state_timer == 0) {
                sim->state_timer = 0x100;
                sub = 7;
            }
            break;
        case 7:
            if (sim->state_timer < 1) {
                enbu_kind = PICTURE_FADE;
                sub = 8;
            } else {
                sim->state_timer -= 8;
                fades.append(sim->state_timer);
            }
            break;
        default:
            sim->state_timer = 0x100;
            next_attract_step();
            enbu_kind = PICTURE_FADE;
            performance = nullptr;
            music_stop();
            ModeStart::start(this, 6, 0);
            break;
    }
    _draw_picture(enbu_kind);
}

// The engine calls of the performance's move events (PadVibrate and CameraShakeStart run as in
// the fight; the rest is the demonstration's own presentation).
void GameFlow::_performance_calls() {
    for (const auto& e : performance->events->items) {
        switch (e->kind) {
            case SimEvents::Kind::FIGHTER_VIBRATE:
                Vibration::fighter(fight, e->fighter, e->a, sim->events);
                break;
            case SimEvents::Kind::SHAKE_REQUEST:
                CameraShake::start(fight, e->a, sim->events);
                break;
            default:
                break;
        }
    }
}

// enbu.ovl FUN_800D3CCC: the performance's globals: a scripted scene (0x800AFF69), the attract
// flag, no freeze or pause, and fighters without a motion bank until the script sets them.
void GameFlow::_enbu_setup() {
    fight->attract = 1;
    fight->freeze = 0;
    fight->pause_request = 0;
    fight->paused_player = 0;
    fight->pause_shown = 0;
    for (gd::Int i = 0; i < 2; ++i) {
        fight->fighters[i]->bank_type = -2;
    }
    region->put8(0x69, 1);
}

// FUN_800DAAD8 / FUN_800D2C30: kind 0 black, 1 and 7 nothing, 2–4 the title with the start
// prompt, the logo or the copyright, 5 the title, 6 the options backdrop, other kinds a black
// fade (level 0).
void GameFlow::_draw_picture(gd::Int kind) {
    picture = kind;
    if (!PICTURES_DRAWN.has(kind)) {
        fades.append(0);
    }
}

// ---- fight preparation (FightPrepare 0x80050600, game state 7) ------------------------------

void GameFlow::_fight_prepare() {
    switch (sub) {
        case 0:
            display(1);
            sub = 1;
            break;
        case 1:
            display(0);
            sub = 2;
            break;
        case 2:
            if (region->loaded_mode_overlay != region->mode_overlay) {
                region->loaded_mode_overlay = region->mode_overlay;
                queue_overlay(SLOT_MODE, region->mode_overlay);
                sim->mode_overlay_loaded();
                goto_loader(State::PREPARE, 2);
            }
            sub = 3;
            break;
        case 3:
            sim->prepare_mode();
            state = State::FIGHT;
            sub = 0;
            break;
        default:
            break;
    }
}

}  // namespace t3
