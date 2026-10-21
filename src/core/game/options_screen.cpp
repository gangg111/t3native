// remake/core/game/options_screen.gd
// The options (title.ovl FUN_800DE480, game state 5; modes.md#options), written from the
// verified port `tools/research/menu_sim.py` and the decompiled overlay (its sounds). Pages: 0 OPTION
// MODE, 1 GAME OPTION, 2 RECORDS, 3 MEMORY CARD, 4 KEY CONFIGURATION, 5 DISPLAY ADJUST (hidden in
// the remake: its item is skipped), 6 leaves. MEMORY CARD saves and loads the save file through
// the owner's `save_writer` and `card_loader` with the memory card's results.
#include "core/game/options_screen.h"

#include "content/flow_data.h"
#include "content/json_file.h"
#include "core/fight/fight_simulation.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/main_menu.h"
#include "core/game/mode_region.h"
#include "core/game/opponents.h"
#include "core/game/progress_rules.h"
#include "core/game/ranking_screen.h"
#include "core/sim_events.h"

namespace t3 {

OptionsScreen::OptionsScreen(GameFlow* flow) : FlowPart(flow) {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Ref<KeyRecord> rec = gd::make<KeyRecord>();
        for (gd::Int i = 0; i < KEY_ROWS; ++i) {
            rec->rows.append(gd::make<KeyRow>());
        }
        keys.append(rec);
    }
}

gd::Json OptionsScreen::items(gd::Int p) {
    return g->data->option_pages[p]["items"];
}

gd::Json OptionsScreen::_item(gd::Int p, gd::Int i) {
    return items(p)[i];
}

bool OptionsScreen::item_hidden(gd::Int p, gd::Int i) {
    std::string label = _item(p, i)["label"].str();   // str() of the JSON string label
    if (system_option && label == "DISPLAY ADJUST") return false;
    return HIDDEN_ITEMS.has(label);
}

std::string OptionsScreen::item_label(gd::Int p, gd::Int i) {
    std::string label = _item(p, i)["label"].str();
    return system_option && label == "DISPLAY ADJUST" ? SYSTEM_LABEL : label;
}

// The value byte of an item (a progress byte, or the page byte).
gd::Int OptionsScreen::item_value(gd::Int p, gd::Int i) {
    gd::Json v = _item(p, i)["variable"];
    if (v.has("page")) {
        return page;
    }
    if (v.has("progress")) {
        return g->progress->u8(JsonFile::number(v["progress"]));
    }
    return 0;
}

void OptionsScreen::_set_item_value(gd::Int p, gd::Int i, gd::Int value) {
    gd::Json v = _item(p, i)["variable"];
    if (v.has("page")) {
        page = value & 0xFF;
    } else if (v.has("progress")) {
        g->progress->put8(JsonFile::number(v["progress"]), value);
    }
}

void OptionsScreen::step() {
    if (_exit_allowed()) {
        // Select + Start as outside the demonstration fight.
        gd::Int mode = g->region->mode;
        g->region->mode = 0;
        bool left = g->menu_exit();
        g->region->mode = mode;
        if (left) {
            return;
        }
    }
    switch (g->sub) {
        case 2:
            g->goto_transition(GameFlow::State::MENU);
            return;
        case 0:
            g->progress->menu_to_options = 1;
            _option_modes();
            key_config_init();
            for (gd::Int i = 0; i < 6; ++i) {
                cursors[i] = 0;
                subs[i] = 0;
            }
            page = 0;
            g->sub = 1;
            break;
        default:
            break;
    }
    if (g->sub != 1) {
        return;
    }
    switch (page) {
        case 0:
        case 1:
            _list_input(page);
            break;
        case PAGE_RECORDS:
            if (subs[page] == 0) {
                records_init();
                subs[page] += 1;
            }
            if (records_step()) {
                subs[page] = 0;
                page = JsonFile::number(g->data->option_pages[PAGE_RECORDS]["back_value"]);
            }
            break;
        case PAGE_CARD:
            _card_page();
            break;
        case PAGE_KEYS:
            if (key_config_step()) {
                page = 0;
            }
            break;
        case PAGE_DISPLAY_ADJUST:
            // Native: SYSTEM OPTION in its place (see system_option).
            if (system_option && system_step) {
                bool left = system_step();
                subs[page] = 1;
                if (!left) break;
                subs[page] = 0;
            }
            page = 0;
            break;
        default:
            g->sub = 2;
            break;
    }
}

// Select + Start is ignored while a memory card screen is up.
bool OptionsScreen::_exit_allowed() {
    return page != PAGE_CARD || gd::Arr<gd::Int>{0, 3}.has(subs[PAGE_CARD]);
}

// The mode pictures of GAME OPTION; Tekken Ball's only once it is unlocked.
void OptionsScreen::_option_modes() {
    modes = gd::PI32();
    for (gd::Int m : g->data->option_modes) {
        if (m != GameMode::BALL || g->progress->ball_new != 0) {
            modes.append(m);
        }
    }
}

// ---- list pages (FUN_800DCE20)

gd::Int OptionsScreen::_cycle(gd::Int v, gd::Int step, gd::Int n) {
    gd::Int t = (v + step) & 0xFFFFFFFFLL;
    return t < n ? t : (n - (v + 1)) & 0xFFFFFFFFLL;
}

void OptionsScreen::_list_input(gd::Int p) {
    gd::Int rep = g->repeat(0) | g->repeat(1);
    gd::Int pressed = g->pressed_any();
    gd::Int step = ((rep >> 14) & 1) - ((rep >> 12) & 1);
    gd::Int lr = ((rep >> 13) & 1) - (rep >> 15);
    gd::Int count = items(p).size();
    gd::Int cur = _cycle(cursors[p], step, count);
    // The remake's hidden items are passed over.
    while (step != 0 && item_hidden(p, cur)) {
        cur = _cycle(cur, step, count);
    }
    if (step != 0) {
        g->sound(SOUND_MOVE);
    }
    cursors[p] = cur;
    gd::Json e = _item(p, cur);
    gd::Int kind = JsonFile::number(e["kind"]);
    if (kind == 1 || kind == 2) {
        gd::Int v = _cycle(item_value(p, cur), lr, JsonFile::number(e["count"]));
        if (lr != 0) {
            g->sound(SOUND_MOVE);
        }
        if (kind == 2 && lr != 0 && (v & 0xFF) != 0) {
            subs[p] = 3;                  // auto save on: the save test first
        } else {
            _set_item_value(p, cur, v);
        }
    }
    e = _item(p, cursors[p]);
    switch (JsonFile::number(e["action"])) {
        case 1:
            if ((pressed & CHOOSE) != 0) {
                _set_item_value(p, cursors[p], JsonFile::number(e["value"]));
                g->sound(SOUND_OK);
            }
            break;
        case 2:
            if ((pressed & CHOOSE) != 0) {
                _set_item_value(p, cursors[p], JsonFile::number(e["value"]));
                g->sound(SOUND_OK);
            }
            if ((g->held(0) | g->held(1)) == COLOUR_TOGGLE_HELD && pressed == PadState::SELECT) {
                g->progress->options_colour = (g->progress->options_colour + 1) & 1;
            }
            break;
        case 3:
            if ((pressed & CHOOSE) != 0) {
                subs[p] = cursors[p] + 1;
                g->sound(SOUND_MOVE);
            }
            break;
        default:
            break;
    }
    gd::Json pg = g->data->option_pages[p];
    if (JsonFile::number(pg["back"]) == 1 && (pressed & PadState::SELECT) != 0) {
        page = JsonFile::number(pg["back_value"]);
        g->sound(SOUND_OK);
    }
}

// ---- records (FUN_800E0E2C, FUN_800E10E8)

void OptionsScreen::records_init() {
    gd::Ref<GameProgress> progress = g->progress;
    gd::Int mask = progress->unlocked & 0xFFDFFFFFLL;
    record_total = 0;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        record_total += progress->usage(c);
    }
    for (gd::Int i = 0; i < 4; ++i) {
        record_tops[i] = 0;
        gd::Arr<gd::Vector2i> pairs;
        bool ascending = false;
        switch (i) {
            case 0: {
                gd::Int m = 0x3FF;
                for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
                    if (progress->time_record(c) <= TIME_LIMIT) {
                        m |= gd::shl(1, c);
                    }
                }
                m &= 0xFFDFFFFFLL;
                for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
                    if ((gd::shr(m, c) & 1) != 0) {
                        pairs.append(gd::Vector2i(c, progress->time_record(c)));
                    }
                }
                ascending = true;
                break;
            }
            case 1:
                record_lists[i] = gd::PI32{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};   // PackedInt32Array(range(10))
                continue;
            case 2:
                for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
                    if ((gd::shr(mask, c) & 1) != 0) {
                        pairs.append(gd::Vector2i(c, progress->usage(c)));
                    }
                }
                break;
            case 3:
                for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
                    if ((gd::shr(mask, c) & 1) != 0) {
                        gd::Int w = progress->stat(c, 1);
                        gd::Int l = progress->stat(c, 2);
                        pairs.append(gd::Vector2i(c, (RankingScreen::permille(w, w + l) * 0x100000 + w + l) & 0xFFFFFFFFLL));
                    }
                }
                break;
            default:
                break;
        }
        Opponents::sort_pairs(pairs, ascending);
        gd::PI32 ids;
        for (const gd::Vector2i& pr : pairs) {
            ids.append(pr.x);
        }
        record_lists[i] = ids;
    }
    record_page = 0;
}

// The records: left/right the sub-page, up/down scroll; true when left (any face button or Select).
bool OptionsScreen::records_step() {
    gd::Int pressed = g->pressed_any();
    if ((pressed & (PadState::SELECT | PadState::FACE_BUTTONS)) != 0) {
        g->sound(SOUND_OK);
        return true;
    }
    gd::Int rep = g->repeat(0) | g->repeat(1);
    gd::Int lr = ((rep >> 13) & 1) - (rep >> 15);
    if (lr != 0) {
        g->sound(SOUND_MOVE);
    }
    gd::Int sub = record_page + lr;
    if (sub < 0) {
        sub = 3;
    } else if (sub > 3) {
        sub = 0;
    }
    record_page = sub;
    gd::Int n = record_lists[sub].size();
    gd::Int top = record_tops[sub];
    if (n < 11) {
        top = 0;
    } else {
        gd::Int t = (top + ((rep >> 14) & 1) - ((rep >> 12) & 1)) & 0xFFFFFFFFLL;
        if (t < ((n - 9) & 0xFFFFFFFFLL)) {
            if (t != top) {
                g->sound(SOUND_MOVE);
            }
            top = t;
        }
    }
    record_tops[sub] = top;
    return false;
}

// ---- memory card (FUN_800DDED0 and its screens)

void OptionsScreen::_card_page() {
    switch (gd::Int(subs[PAGE_CARD])) {
        case 0:
            card_state = 0;
            _list_input(PAGE_CARD);
            break;
        case 1:
            if (_card_save_screen()) {
                subs[PAGE_CARD] = 0;
            }
            break;
        case 2:
            if (_card_load_screen()) {
                subs[PAGE_CARD] = 0;
            }
            break;
        case 3: {
            g->progress->auto_save = 1;
            gd::Int r = _card_auto_save_screen();
            g->progress->auto_save = 0;
            if (r == 2) {
                g->progress->auto_save = 1;
            }
            if (r == 1 || r == 2) {
                subs[PAGE_CARD] = 0;
            }
            break;
        }
        default:
            break;
    }
    _option_modes();
}

bool OptionsScreen::_card_gate(const gd::Arr<gd::Int>& quiet) {
    gd::Int pressed = g->pressed_any();
    return (pressed & (quiet.has(card_state) ? 0xF0 : 0x58F0)) != 0;
}

gd::Int OptionsScreen::_save() {
    if (save_writer) {
        return save_writer(g->progress->save_data());
    }
    return CARD_DONE;
}

gd::Int OptionsScreen::_load() {
    if (!g->card_loader) {
        return CARD_NO_FILE;
    }
    gd::PBytes saved = g->card_loader();
    if (saved.is_empty()) {
        return CARD_NO_FILE;
    }
    g->progress->load_save_data(saved);
    return CARD_DONE;
}

// FUN_800DD198: Start loads; any other button (with a message showing, also Start, up, down) leaves.
bool OptionsScreen::_card_load_screen() {
    if (_card_gate(gd::Arr<gd::Int>{0})) {
        g->sound(SOUND_MOVE);
        return true;
    }
    if (card_state == 0 && (g->pressed_any() & PadState::START) != 0) {
        card_state = (_load() + 1) & 0xFF;
        g->sound(SOUND_DONE);
    }
    return false;
}

// FUN_800DD544: Start saves.
bool OptionsScreen::_card_save_screen() {
    if (_card_gate(gd::Arr<gd::Int>{0, 4})) {
        g->sound(SOUND_MOVE);
        return true;
    }
    if ((card_state == 0 || card_state == 4) && (g->pressed_any() & PadState::START) != 0) {
        card_state = (_save() + 1) & 0xFF;
        g->sound(SOUND_DONE);
    }
    return false;
}

// FUN_800DD9E4: turning AUTO SAVE on saves first; 2 when that worked, 1 when left.
gd::Int OptionsScreen::_card_auto_save_screen() {
    if (_card_gate(gd::Arr<gd::Int>{0, 4, 6})) {
        g->sound(SOUND_MOVE);
        return 1;
    }
    switch (card_state) {
        case 0:
            card_state = (_save() + 1) & 0xFF;
            break;
        case 1:
            return 2;
        case 4:
        case 6:
            if ((g->pressed_any() & PadState::START) != 0) {
                card_state = (_save() + 1) & 0xFF;
            }
            break;
        default:
            break;
    }
    return 0;
}

// ---- key configuration (FUN_800DF9EC, FUN_800DF630)

void OptionsScreen::key_config_init() {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Ref<KeyRecord> rec = keys[p];
        rec->player = p;
        rec->selected = 0;
        rec->armed = 0;
        rec->x = p * 0xB8;
        rec->y = 0;
        rec->busy = 0;
        rec->pad_kind = 0;
        for (gd::Int i = 0; i < KEY_ROWS; ++i) {
            gd::Json src = g->data->key_rows[i];
            gd::Ref<KeyRow> row = rec->rows[i];
            row->state = 0;
            row->open = 0;
            row->mask = JsonFile::number(src["mask"]);
            row->button = JsonFile::number(src["index"]);
            row->layer = JsonFile::number(src["layer"]);
            row->layout = JsonFile::ints(src["layout"]);
            row->colour = JsonFile::number(src["colour"]);
        }
        g->menu->button_map(p);
    }
}

// Both players' key configuration; true when left (Select, or EXIT with no row busy).
bool OptionsScreen::key_config_step() {
    bool leave = false;
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Ref<KeyRecord> rec = keys[p];
        if (rec->armed == 0 && g->pressed(p) != 0) {
            rec->armed = 1;
        }
        rec->busy = 0;
        for (gd::Int item = 0; item < KEY_ITEMS; ++item) {
            bool selected = item == rec->selected;
            switch (item) {
                case 0:
                    _key_setting(rec, selected);
                    break;
                case 1:
                    _key_vibration(rec, selected);
                    break;
                case 2:
                    if (selected && (g->pressed(p) & CHOOSE) != 0) {
                        g->progress->put8(0x1C + p, 0);
                        for (gd::Int k = 0; k < 8; ++k) {
                            g->progress->set_button_action(p, k, g->data->key_default[k]);
                        }
                        rec->busy = 1;
                        g->sound(SOUND_DONE);
                    }
                    break;
                case 3:
                    if (selected && (g->pressed(p) & CHOOSE) != 0) {
                        leave = true;
                    }
                    break;
                default:
                    break;
            }
        }
        if (rec->busy == 0) {
            gd::Int rep = g->repeat(p);
            gd::Int step = ((rep >> 14) & 1) - ((rep >> 12) & 1);
            if (step != 0) {
                g->sound(SOUND_MOVE);
            }
            gd::Int v = step + rec->selected;
            rec->selected = v < 0 ? 3 : (v > 3 ? 0 : v);
        }
    }
    if ((g->pressed_any() & PadState::SELECT) != 0) {
        leave = true;
    }
    if (leave && keys[0]->busy == 0 && keys[1]->busy == 0) {
        key_config_init();
        g->sound(SOUND_OK);
        return true;
    }
    return false;
}

// FUN_800DF14C: the eight button rows (the pad picture).
void OptionsScreen::_key_setting(const gd::Ref<KeyRecord>& rec, bool selected) {
    rec->pad_kind = 0;
    rec->used = 0;
    if (!selected) {
        return;
    }
    rec->held = g->held(rec->player);
    rec->repeat = g->repeat(rec->player);
    if (rec->armed == 0) {
        rec->held = 0;
    }
    if ((rec->held & 0xFF) != 0) {
        rec->busy = 1;
    }
    if (ProgressRules::popcount(rec->held & 0xFF) != 1) {
        rec->held = 0;
    }
    for (const gd::Ref<KeyRow>& row : rec->rows) {
        _key_row_input(rec, row);
    }
}

// FUN_800DEF98: holding a row's button opens its list over 8 frames; up/down picks the action.
void OptionsScreen::_key_row_input(const gd::Ref<KeyRecord>& rec, const gd::Ref<KeyRow>& row) {
    gd::Int before = row->state;
    if ((rec->held & row->mask) != 0) {
        row->open += 1;
        if (row->open < KEY_OPEN_FRAMES + 1) {
            row->state = 1;
        } else {
            row->open = KEY_OPEN_FRAMES;
            row->state = 2;
        }
    } else {
        row->open -= 1;
        if (row->open < 1) {
            row->open = 0;
            row->state = 0;
        } else {
            row->state = 3;
        }
    }
    if (row->open != 0) {
        rec->busy = 1;
    }
    if (before == 0 && row->state == 1) {
        g->sound(SOUND_KEY_OPEN);
    }
    if (row->state == 2) {
        gd::Int step = ((rec->repeat >> 14) & 1) - ((rec->repeat >> 12) & 1);
        if (step != 0) {
            g->sound(SOUND_MOVE);
        }
        gd::Int v = step + g->progress->button_action(rec->player, row->button);
        g->progress->set_button_action(rec->player, row->button, v < 0 ? KEY_ACTIONS - 1 : (v > KEY_ACTIONS - 1 ? 0 : v));
    }
    rec->used |= gd::shl(1, g->progress->button_action(rec->player, row->button) & 31);
}

// FUN_800DF324: VIBRATION (left/right toggles it, with a short buzz).
void OptionsScreen::_key_vibration(const gd::Ref<KeyRecord>& rec, bool selected) {
    gd::Int p = rec->player;
    if (selected && (g->pressed(p) & PadState::HORIZONTAL) != 0) {
        gd::Int v = (g->progress->u8(0x1C + p) + 1) & 1;
        g->progress->put8(0x1C + p, v);
        rec->busy = 1;
        g->globals->player_active[p] = 1;
        g->globals->controller[p] = v;
        g->sim->events->add(SimEvents::Kind::VIBRATE, -1, p, VIBRATION_PATTERN);
        g->sound(SOUND_MOVE);
    }
}

}  // namespace t3
