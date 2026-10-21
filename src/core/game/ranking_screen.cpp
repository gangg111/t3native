// remake/core/game/ranking_screen.gd
// The ranking (modes.md#ranking-screen): its loading (RankingLoad 0x800504A8, game state 16)
// and the pages (ranking.ovl FUN_800C2A24, game state 17), written from the verified port
// `tools/research/screens_sim.py` (ranking_frame, name_entry, the page tables, the backdrop camera).
// The pages cycle time attack, survivors and usage; after a new time attack or survival record
// the name is entered on its page. Behind the table a stage turns (a stage without fighters).
#include "core/game/ranking_screen.h"

#include "content/fight_tables.h"
#include "content/flow_data.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/game/opponents.h"
#include "core/game/progress_rules.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

namespace {
// GDScript String.to_ascii_buffer() / char() over the ASCII names and alphabet of the ranking.
gd::PBytes ascii_buffer(const std::string& text) {
    gd::PBytes raw;
    for (char ch : text) raw.append(static_cast<uint8_t>(ch));
    return raw;
}
}  // namespace

RankingScreen::RankingScreen(GameFlow* flow) : FlowPart(flow) {
    table = gd::make<ByteBlock>(TABLE_SIZE);
    name = gd::make<ByteBlock>(NAME_SIZE);
    backdrop = gd::make<ByteBlock>(BACKDROP_SIZE);
}

// ---- loading (game state 16)

void RankingScreen::load_step() {
    switch (g->sub) {
        case 0:
            g->display(0);
            g->music_stop();
            g->sub = 1;
            break;
        case 1:
            g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
            g->controllers_reset();
            if (g->progress->name_entry == 0) {
                g->auto_save();
            }
            g->queue_overlay(GameFlow::SLOT_SCREEN, GameFlow::OVERLAY_RANKING);
            g->goto_loader(GameFlow::State::RANKING_LOAD, 2);
            break;
        case 2:
        case 3:
            g->sub += 1;
            break;
        case 4:
        case 5:
            if (g->sub == 4) {
                _backdrop_setup();
                g->sub = 5;
            }
            if (g->auto_save_error()) {
                return;
            }
            g->state = GameFlow::State::RANKING;
            g->sub = 0;
            break;
        default:
            break;
    }
}

// FUN_800C390C: the stage behind the ranking (the next of the cycle, skipping four) and its camera.
void RankingScreen::_backdrop_setup() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Ref<FightState> fight = g->fight;
    g->region->mode = 6;
    fight->true_ogre_mask = 0;
    fight->attract = 1;
    gd::Int stage = 0;
    while (true) {
        stage = progress->ranking_stage % STAGES;
        progress->ranking_stage = stage;
        if (!g->data->ranking_skipped.has(stage)) {
            break;
        }
        progress->ranking_stage = stage + 1;
    }
    progress->ranking_stage = stage + 1;
    fight->stage = stage;
    fight->stage_cached = stage;        // FUN_80036BC8 empties the cache and reads the stage,
    fight->backdrop->setup(g->sim->camera->view);   // and sets its panorama up (FUN_8006CC44)
    backdrop->put16(0x28, ORBIT_START);
    gd::Ref<FightTables> t = fight->tables;
    backdrop->put16(0x24, _round12(FightMath::cos12(ORBIT_START, t) * ORBIT_RADIUS));
    backdrop->put16(0x26, _round12(FightMath::cos12(0x800, t) * ORBIT_RADIUS));
    fight->fighters[0]->root_x = -1000;
    fight->fighters[1]->root_x = 1000;
    fight->fighters[0]->root_z = 0;
    fight->fighters[1]->root_z = 0;
    backdrop->put32(0x14, backdrop->s16(0x24));
    backdrop->put32(0x18, EYE_HEIGHT);
    backdrop->put32(0x1C, backdrop->s16(0x26));
    for (gd::Int at : gd::Arr<gd::Int>{0, 4, 8}) {
        backdrop->put32(at, 0);
    }
}

gd::Int RankingScreen::_round12(gd::Int v) {
    return (v < 0 ? v + 0xFFF : v) >> 12;
}

// FUN_800C3AAC: the camera turns around the stage (its eye at +0x14 … +0x1C); FUN_80046458 makes
// it the game's camera, and the background (FUN_8006DAB4) turns the panorama with it.
void RankingScreen::_backdrop_camera() {
    backdrop->put32(4, backdrop->s32(4) - ORBIT_STEP);
    gd::Int a = (backdrop->u16(0x28) - ORBIT_STEP) & 0xFFF;
    backdrop->put16(0x28, a);
    gd::Ref<FightTables> t = g->fight->tables;
    backdrop->put16(0x24, _round12(FightMath::cos12(a, t) * ORBIT_RADIUS));
    backdrop->put16(0x26, _round12(FightMath::sin12(a, t) * ORBIT_RADIUS));
    backdrop->put32(0x14, backdrop->s16(0x24));
    backdrop->put32(0x1C, backdrop->s16(0x26));
    gd::Ref<CameraView> view = g->sim->camera->view;
    view->pitch = backdrop->s32(0);
    view->yaw = backdrop->s32(4);
    view->roll = backdrop->s32(8);
    view->x = backdrop->s32(0x14);
    view->y = backdrop->s32(0x18);
    view->z = backdrop->s32(0x1C);
    g->sim->background();
}

// ---- the pages (game state 17)

void RankingScreen::step() {
    fade = -1;
    gd::Int sub = g->sub;
    if (sub > Sub::SETUP && (g->pressed_any() & PadState::START) != 0) {
        _skip_to_menu();
        return;
    }
    if (sub == Sub::START) {
        _page_from_pad();
        sub = Sub::SETUP;
        g->sub = Sub::SETUP;
    }
    if (sub == Sub::SETUP) {
        _set_up_page();
        _draw();
        return;
    }
    switch (sub) {
        case Sub::FADE_IN_START:
        case Sub::FADE_IN:
            _fade_in(sub == Sub::FADE_IN_START);
            break;
        case Sub::GROUP_START:
            _group_start();
            break;
        case Sub::SLIDE:
            _slide();
            break;
        case Sub::HOLD:
            _hold();
            break;
        case Sub::SHOW:
            _wait(0x3B);
            break;
        case Sub::FADE_OUT_START:
        case Sub::FADE_OUT:
            _fade_out(sub == Sub::FADE_OUT_START);
            break;
        case Sub::NAME_SCROLL_START:
            _name_scroll_start();
            break;
        case Sub::NAME_SCROLL:
            _name_scroll();
            break;
        case Sub::NAME_ENTRY:
            _name_entry_frame();
            break;
        case Sub::NAME_ENTERED:
            _wait(0x77);
            break;
        case Sub::LEAVE:
            table->put16(T_MODE, TableMode::HIDDEN);
            g->progress->ranking_page = (g->progress->ranking_page + 1) % 3;
            g->goto_transition(GameFlow::State::TITLE);
            return;
        default:
            break;
    }
    _draw();
}

// Start leaves for the main menu, taking a pending name as it stands.
void RankingScreen::_skip_to_menu() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int pending = progress->name_entry;
    if (pending != 0) {
        name_entry(NameInput::SKIP);
        _store_name(pending);
        progress->name_entry = 0;
        g->rules->request_save();
    }
    g->goto_transition(GameFlow::State::MENU);
}

// Without a pending name, player 2's stick picks the page: right time attack, left survivors,
// up usage.
void RankingScreen::_page_from_pad() {
    gd::Ref<GameProgress> progress = g->progress;
    if (progress->name_entry != 0) {
        return;
    }
    gd::Int held = g->held(1);
    if ((held & PadState::RIGHT) != 0) {
        progress->ranking_page = 0;
    } else if ((held & PadState::LEFT) != 0) {
        progress->ranking_page = 1;
    } else if ((held & PadState::UP) != 0) {
        progress->ranking_page = 2;
    }
}

// The page's table; a pending name (bit 0 time attack, bit 1 survival) opens its page with the
// name entry, a name for neither only shows the usage page.
void RankingScreen::_set_up_page() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int pending = progress->name_entry;
    if (pending != 0) {
        if ((pending & 1) == 0) {
            if ((pending & 2) == 0) {
                progress->ranking_page = 2;
                progress->name_entry = 0;
                return;
            }
            progress->ranking_page = 1;
            table->put16(T_NAME_ROW, _survivor_page());
        } else {
            progress->ranking_page = 0;
            table->put16(T_NAME_ROW, _time_page());
        }
        _name_init(progress->entry_player, progress->entry_character);
    } else {
        switch (gd::Int(progress->ranking_page)) {
            case 0: _time_page(); break;
            case 1: _survivor_page(); break;
            case 2: _usage_page(); break;
            default: break;
        }
        table->put16(T_NAME_ROW, NO_ROW);
    }
    g->sub = Sub::FADE_IN_START;
    table->put16(T_HOLD, 0);
    table->put16(T_MODE, TableMode::HIDDEN);
}

void RankingScreen::_fade_in(bool first) {
    gd::Int timer = g->sim->state_timer;
    if (first) {
        table->put16(T_MODE, TableMode::FADING_IN);
        timer = 0;
        g->sub = Sub::FADE_IN;
    }
    fade = timer;
    timer += 8;
    g->sim->state_timer = timer;
    if (timer > 0xFF) {
        g->sub = g->progress->name_entry == 0 ? Sub::GROUP_START : Sub::NAME_SCROLL_START;
    }
}

void RankingScreen::_fade_out(bool first) {
    gd::Int timer = g->sim->state_timer;
    if (first) {
        timer = 0x100;
        g->sub = Sub::FADE_OUT;
    }
    fade = timer;
    timer -= 8;
    g->sim->state_timer = timer;
    if (timer <= 0) {
        g->sub = Sub::LEAVE;
        table->put16(T_MODE, TableMode::HIDDEN);
    }
}

// Holds the finished table for `frames` frames, then fades out.
void RankingScreen::_wait(gd::Int frames) {
    gd::Int timer = g->sim->state_timer + 1;
    g->sim->state_timer = timer;
    if (timer > frames) {
        g->sub = Sub::FADE_OUT_START;
    }
}

// The rows slide in from below, the last group first.
void RankingScreen::_group_start() {
    gd::Int count = table->u16(T_ROWS);
    table->put32(T_SHOWN, count);
    gd::Int r = count % GROUP;
    if (r == 0) {
        r = GROUP;
    }
    table->put32(T_FIRST_ROW, count - r);
    g->sub = Sub::SLIDE;
    table->put16(T_MODE, TableMode::SLIDING);
    table->put16(T_Y, count * -ROW_HEIGHT + ROWS_TOP);
}

void RankingScreen::_slide() {
    gd::Int group = table->s32(T_FIRST_ROW);
    table->put16(T_TARGET_Y, group * -ROW_HEIGHT + ROWS_TOP);
    table->put16(T_Y, table->u16(T_Y) + 12);
    if (table->s16(T_TARGET_Y) <= table->s16(T_Y)) {
        table->put16(T_HOLD, 0);
        table->put16(T_ALPHA, 0);
        g->sub = Sub::HOLD;
        table->put32(T_SHOWN, group);
        table->put16(T_Y, table->u16(T_TARGET_Y));
    }
}

// A group's rows fade in and hold, then the next group slides (or the table shows).
void RankingScreen::_hold() {
    gd::Int hold = table->u16(T_HOLD);
    table->put16(T_ALPHA, gd::mini(hold, 0x40));
    table->put16(T_HOLD, hold + 1);
    if (table->u16(T_GROUP_FRAMES) > table->u16(T_HOLD)) {
        return;
    }
    if (table->s32(T_FIRST_ROW) < 1) {
        g->sim->state_timer = 0;
        g->sub = Sub::SHOW;
    } else {
        g->sub = Sub::SLIDE;
        table->put32(T_FIRST_ROW, table->s32(T_FIRST_ROW) - GROUP);
    }
}

// The table scrolls so the new record's row shows (as the third of five where it can).
void RankingScreen::_name_scroll_start() {
    gd::Int row = table->u16(T_NAME_ROW);
    gd::Int count = table->u16(T_ROWS);
    if (row < 2) {
        table->put32(T_FIRST_ROW, 0);
        table->put16(T_NAME_SCREEN_ROW, row);
    } else {
        table->put32(T_FIRST_ROW, count - GROUP);
        if (count - 3 < row) {
            table->put16(T_NAME_SCREEN_ROW, GROUP - (count - row));
        } else {
            table->put32(T_FIRST_ROW, row - 2);
            table->put16(T_NAME_SCREEN_ROW, 2);
        }
    }
    table->put16(T_ALPHA, 0x40);
    g->sub = Sub::NAME_SCROLL;
    table->put32(T_SHOWN, 0);
    table->put16(T_MODE, TableMode::SLIDING);
    table->put16(T_Y, count * -ROW_HEIGHT + ROWS_TOP);
}

void RankingScreen::_name_scroll() {
    gd::Int target = (table->s32(T_FIRST_ROW) * -ROW_HEIGHT + ROWS_TOP) & 0xFFFF;
    table->put16(T_TARGET_Y, target);
    gd::Int step = (Fx::s16(target) - table->s16(T_Y) + 0xF) >> 4;
    step = step == 0 ? 1 : gd::mini(step, 0x10);
    gd::Int y = (table->u16(T_Y) + step) & 0xFFFF;
    table->put16(T_Y, y);
    if (table->s16(T_TARGET_Y) <= Fx::s16(y)) {
        table->put16(T_HOLD, NAME_FRAMES);
        table->put16(T_MODE, TableMode::NAME_ENTRY);
        g->sub = Sub::NAME_ENTRY;
        table->put16(T_Y, table->u16(T_TARGET_Y));
    }
}

// One frame of the name entry, which ends when the name is complete or the time runs out.
void RankingScreen::_name_entry_frame() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int left = (table->u16(T_HOLD) - 1) & 0xFFFF;
    table->put16(T_HOLD, left);
    bool done = name_entry(left == 0 ? NameInput::TIMEOUT : NameInput::PAD);
    _store_name(progress->name_entry);
    if (done) {
        progress->name_entry = 0;
        g->rules->request_save();
        table->put16(T_MODE, TableMode::NAME_ENTERED);
        g->sim->state_timer = 0;
        g->sub = Sub::NAME_ENTERED;
    }
}

// The drawing half: the backdrop camera turns while the table shows.
void RankingScreen::_draw() {
    if (table->u16(T_MODE) != TableMode::HIDDEN) {
        _backdrop_camera();
    }
}

// The entered name into the new records (time attack, survivors).
void RankingScreen::_store_name(gd::Int pending) {
    gd::Ref<GameProgress> progress = g->progress;
    std::string text = entered_name();
    if ((pending & 1) != 0) {
        progress->put_name(progress->record_entry - GameProgress::BASE + 4, text);
    }
    if ((pending & 2) != 0) {
        progress->put_name(progress->survivor_entry - GameProgress::BASE + 4, text);
    }
}

// The name so far (up to its terminator).
std::string RankingScreen::entered_name() {
    std::string s = "";
    for (gd::Int k = 0; k < 4; ++k) {
        gd::Int c = name->u8(k);
        if (c == 0) {
            break;
        }
        s += static_cast<char>(c);   // char(c): the name's symbols are ASCII
    }
    return s;
}

// ---- the page tables

// FUN_800C1D1C: time attack: the ten original characters and every other with a record, fastest
// first; the row of the record just set (−1 none).
gd::Int RankingScreen::_time_page() {
    gd::Ref<GameProgress> progress = g->progress;
    table->put16(T_GROUP_FRAMES, GROUP_FRAMES);
    gd::Int mask = 0x3FF;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        if (progress->time_record(c) < TIME_CAP) {
            mask |= gd::shl(1, c);
        }
    }
    gd::Int n = ProgressRules::popcount(mask & 0x1FFFFF);
    table->put16(T_ROWS, n);
    gd::Arr<gd::Vector2i> pairs;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        if ((gd::shr(mask, c) & 1) != 0) {
            pairs.append(gd::Vector2i(c, progress->time_record(c)));
        }
    }
    Opponents::sort_pairs(pairs, true);
    for (gd::Int i = 0; i < pairs.size(); ++i) {
        table->put16(T_CHARS + 2 * i, pairs[i].x);
    }
    for (gd::Int i = 0; i < n; ++i) {
        gd::Int at = GameProgress::BASE + GameProgress::TIME_RECORDS + 8 * table->u16(T_CHARS + 2 * i);
        if (at == progress->record_entry) {
            return i;
        }
    }
    return -1;
}

// FUN_800C1E8C and the page set-up: the survivors re-sorted by wins; the row of the entry just
// made (−1 none).
gd::Int RankingScreen::_survivor_page() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int entry = progress->survivor_entry;
    gd::Arr<gd::PBytes> rows;
    gd::Arr<gd::Vector2i> pairs;
    gd::Int found = -1;
    for (gd::Int i = 0; i < SURVIVORS; ++i) {
        gd::Int at = GameProgress::SURVIVORS + 8 * i;
        rows.append(progress->bytes.slice(at, at + 8));
        pairs.append(gd::Vector2i(i, progress->u16(at + 2)));
        if (GameProgress::BASE + at == entry) {
            found = i;
        }
    }
    Opponents::sort_pairs(pairs, false);
    for (gd::Int k = 0; k < SURVIVORS; ++k) {
        gd::Int at = GameProgress::SURVIVORS + 8 * k;
        gd::PBytes r = rows[pairs[k].x];
        progress->put16(at, r.decode_u16(0));
        progress->put16(at + 2, r.decode_u16(2));
        gd::Int end = 4;
        while (end < 8 && r[end] != 0) {
            end += 1;
        }
        progress->put_name(at + 4, r.slice(4, end).get_string_from_ascii());
    }
    if (found != -1) {
        progress->survivor_entry = GameProgress::BASE + GameProgress::SURVIVORS + 8 * found;
    }
    table->put16(T_ROWS, SURVIVORS);
    for (gd::Int k = 0; k < SURVIVORS; ++k) {
        table->put16(T_CHARS + 2 * k, k);
    }
    table->put16(T_GROUP_FRAMES, GROUP_FRAMES);
    return found;
}

// FUN_800C1FA8: usage: the ten original characters and every other played, most used first,
// with their shares.
void RankingScreen::_usage_page() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int mask = 0x3FF;
    gd::Int total = 0;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        gd::Int u = progress->usage(c);
        if (u != 0) {
            mask |= gd::shl(1, c);
            total += u;
        }
    }
    gd::Int n = ProgressRules::popcount(mask & 0x1FFFFF);
    table->put16(T_ROWS, n);
    gd::Arr<gd::Vector2i> pairs;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        if ((gd::shr(mask, c) & 1) != 0) {
            gd::Int u = progress->usage(c);
            pairs.append(gd::Vector2i(c, u));
            table->put16(T_USAGE + 2 * c, permille(u, total & 0xFFFFFFFFLL));
        }
    }
    Opponents::sort_pairs(pairs, false);
    for (gd::Int i = 0; i < pairs.size(); ++i) {
        table->put16(T_CHARS + 2 * i, pairs[i].x);
    }
    table->put16(T_GROUP_FRAMES, GROUP_FRAMES);
    table->put16(T_TOP_SHARE, table->u16(T_USAGE + 2 * gd::Int(pairs[0].x)));
}

// FUN_8004D008: part · 1000 / total, scaled down to avoid overflow.
gd::Int RankingScreen::permille(gd::Int part, gd::Int total) {
    if (part > total) {
        return 1000;
    }
    while (total > 0x3FFFFF) {
        total >>= 1;
        part >>= 1;
    }
    return total == 0 ? 0 : gd::div(part * 1000, total);
}

// ---- the name entry (FUN_800C3438, FUN_800C3480)

void RankingScreen::_name_init(gd::Int player, gd::Int character) {
    name->put16(N_PLAYER, player);
    name->put16(N_CHARACTER, character);
    _put_text(g->data->ranking_rejected[0]);
    name->put16(N_POS, 0);
    name->put16(N_CURSOR, 0);
    name->put8(N_PRESSED, 0);
    name->put8(N_MOVED, 0);
}

void RankingScreen::_put_text(const std::string& text) {
    gd::PBytes raw = ascii_buffer(text);
    for (gd::Int k = 0; k < raw.size(); ++k) {
        name->put8(k, raw[k]);
    }
    name->put8(raw.size(), 0);
}

// The character's name (its first three letters).
void RankingScreen::_default_name() {
    gd::Json record = g->fight->tables->character(name->u16(N_CHARACTER) * 4);
    gd::PBytes text = ascii_buffer(record["name"].str());
    for (gd::Int i = 0; i < 3; ++i) {
        name->put8(i, i < text.size() ? gd::Int(text[i]) : 0);
    }
    name->put8(3, 0);
}

void RankingScreen::_finish_name() {
    for (gd::Int i = name->u16(N_POS); i < 3; ++i) {
        name->put8(i, 0x20);
    }
    name->put8(3, 0);
    name->put16(N_POS, 3);
    if (g->data->ranking_rejected.has(entered_name())) {
        _default_name();
    }
}

// FUN_800C3480: one frame of input, the time ran out, or take the character's name (NameInput);
// true when the name is complete.
bool RankingScreen::name_entry(gd::Int mode) {
    name->put8(N_PRESSED, 0);
    name->put8(N_MOVED, 0);
    if (mode == NameInput::TIMEOUT) {
        _finish_name();
        return true;
    }
    if (mode == NameInput::SKIP) {
        _default_name();
        name->put16(N_POS, 3);
        return true;
    }
    gd::Int player = name->u16(N_PLAYER);
    gd::Int pressed = g->pressed(player);
    gd::Int rep = g->repeat(player);
    gd::Int step = ((rep >> 13) & 1) - (rep >> 15);
    if (step != 0) {
        name->put8(N_MOVED, 1);
    }
    gd::Int symbols = static_cast<gd::Int>(g->data->ranking_alphabet.length());
    gd::Int cursor = name->u16(N_CURSOR) + step;
    if (cursor < 0) {
        cursor = symbols - 1;
    } else if (cursor > symbols - 1) {
        cursor = 0;
    }
    name->put16(N_CURSOR, cursor);
    gd::Int pos = name->u16(N_POS);
    // unicode_at(cursor): the alphabet's symbols are ASCII.
    gd::Int ch = pos < 3 ? static_cast<gd::Int>(static_cast<unsigned char>(g->data->ranking_alphabet.at(static_cast<size_t>(cursor)))) : 0;
    name->put8(pos, ch);
    if ((pressed & GameFlow::FACE_BUTTONS) == 0) {
        return false;
    }
    name->put8(N_PRESSED, 1);
    if (ch == '<') {
        if (pos != 0) {
            name->put8(pos, 0x20);
            name->put16(N_POS, pos - 1);
        }
        return false;
    }
    if (ch == '=') {
        _finish_name();
        return true;
    }
    name->put8(pos, ch);
    pos = (pos + 1) & 0xFFFF;
    name->put16(N_POS, pos);
    if (pos < 3) {
        return false;
    }
    if ((g->progress->unlocked & (gd::Int(1) << Character::GON)) == 0) {
        std::string typed = "";
        for (gd::Int i = 0; i < 3; ++i) {
            if (name->u8(i) == 0) {
                break;
            }
            typed += static_cast<char>(name->u8(i));
        }
        if (typed == g->data->ranking_gon) {
            g->rules->unlock_character(Character::GON);
        }
    }
    _finish_name();
    return true;
}

}  // namespace t3
