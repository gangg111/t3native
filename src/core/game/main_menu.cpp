// remake/core/game/main_menu.gd
// The main menu (title.ovl FUN_800DBBD0, game state 4; menu_sim.main_menu, verified): the list of
// modes, the cursor (up / down with repeat, wrapping; VS MODE is skipped with one controller),
// the choice (Start or a face button) and the time-out back to the title after 480 frames.
#include "core/game/main_menu.h"

#include "content/flow_data.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_input.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_progress.h"
#include "core/game/mode_start.h"

namespace t3 {

MainMenu::MainMenu(GameFlow* flow) : FlowPart(flow) {}

gd::Int MainMenu::cursor() {
    return g->progress->menu_cursor;
}

// The drawing's own state (menu_sim._main_menu_draw): the slide decays by a quarter each frame
// (rounded up), and the choice bars glow in once the list rests.
void MainMenu::draw_step() {
    gd::Int v = scroll;
    gd::Int n = 0;
    if (v != 0) {
        gd::Int a = gd::absi(v);
        n = gd::maxi(a - ((a + 3) >> 2), 0);
        if (v < 0) {
            n = -n;
        }
    }
    scroll = n;
    if (scroll == 0) {
        glow = gd::mini(glow + 0x18, 0x100);
    } else {
        glow = 0;
    }
}

void MainMenu::step() {
    _step();
    draw_step();
}

void MainMenu::_step() {
    gd::Int pressed = g->pressed_any();
    gd::Int rep0 = g->repeat(0);
    gd::Int rep1 = g->repeat(1);
    gd::Int rep = rep0 | rep1;
    bool one_pad = (g->sim->pads->connected & 1) == 0 || (g->sim->pads->connected & 2) == 0;
    switch (g->sub) {
        case 0:
            _open();
            break;
        case 1:
            g->sim->state_timer += 1;
            if (g->sim->state_timer < TIMEOUT) {
                gd::Int cur = g->progress->menu_cursor;
                gd::Int step_dir = ((rep >> 14) & 1) - ((rep >> 12) & 1);
                gd::Int count = entries.size();
                if (count <= cur) {
                    cur = 0;
                }
                gd::Int nxt = 0;
                while (true) {
                    nxt = (cur + step_dir) & 0xFFFFFFFFLL;
                    if (count <= nxt) {
                        nxt = (count - (cur + 1)) & 0xFFFFFFFFLL;
                    }
                    scroll += step_dir * SCROLL_STEP;
                    if (entries[nxt]->entry->two_players != 1 || !one_pad) {
                        break;
                    }
                    cur = nxt;
                    if (step_dir == 0) {
                        step_dir = 1;
                    }
                }
                g->progress->menu_cursor = nxt;
                if (step_dir != 0) {
                    g->sim->state_timer = 8;
                    g->sound(SOUND_CURSOR);
                }
                if ((pressed & CHOOSE_BUTTONS) != 0) {
                    g->sound(SOUND_CHOOSE);
                    chosen = 1;
                    scroll = 0;
                    glow = 0x100;
                    players = (g->pressed(0) & CHOOSE_BUTTONS) != 0 ? 1 : 0;
                    if ((g->pressed(1) & CHOOSE_BUTTONS) != 0) {
                        players |= 2;
                    }
                    g->sub = 2;
                }
            } else {
                g->sub = 3;
                chosen = 1;
            }
            break;
        case 2: {
            gd::Int mode = entries[g->progress->menu_cursor]->entry->mode;
            if (mode == MODE_OPTIONS) {
                g->goto_transition(GameFlow::State::OPTIONS);
            } else if (mode == MODE_NETWORK) {
                g->sub = SUB_NETWORK;
            } else if (mode == MODE_THEATER) {
                if (g->progress->theater_new < 3) {
                    g->progress->theater_new += 1;
                }
                g->fight->human_mask = 0;
                g->goto_screen_loader(GameFlow::State::ENDING);
            } else {
                if (mode == MODE_BALL && g->progress->ball_new < 3) {
                    g->progress->ball_new += 1;
                }
                ModeStart::start(g, mode, players);
            }
            break;
        }
        case 3:
            g->goto_transition(GameFlow::State::TITLE);
            break;
        case SUB_NETWORK:
            // Native: the app's network screens until they leave (the list opens again on the entry).
            if (g->network_back) {
                g->network_back = false;
                g->sub = 0;
            }
            break;
    }
}

// Sub-state 0: the controllers reset, the players and the key configuration, the entries (Tekken
// Ball and Theater once unlocked, NEW while their counter is below 3).
void MainMenu::_open() {
    g->controllers_reset();
    g->globals->player_active[1] = 0;
    g->globals->player_active[0] = 0;
    button_map(0);
    button_map(1);
    entries.clear();
    chosen = 0;
    for (const gd::Ref<FlowData::MenuEntry>& e : g->data->menu_entries) {
        gd::Int v = 3;
        if (e->mode == MODE_BALL) {
            v = g->progress->ball_new;
        } else if (e->mode == MODE_THEATER) {
            v = g->progress->theater_new;
        }
        if (v != 0) {
            gd::Ref<Entry> shown = gd::make<Entry>();
            shown->entry = e;
            shown->new_ = v < 3;
            entries.append(shown);
        }
        if (e->mode == GameMode::VS && g->network_entry) {
            gd::Ref<FlowData::MenuEntry> net = gd::make<FlowData::MenuEntry>();
            net->text = g->network_session ? "DISCONNECT" : "NETWORK MODE";
            net->mode = MODE_NETWORK;
            net->index = NETWORK_COLOUR;
            gd::Ref<Entry> shown = gd::make<Entry>();
            shown->entry = net;
            entries.append(shown);
        }
    }
    if (g->progress->menu_to_options != 0) {
        g->progress->menu_to_options = 0;
        gd::Int i = 0;
        while (i < entries.size() && entries[i]->entry->mode != MODE_OPTIONS) {
            i += 1;
        }
        g->progress->menu_cursor = i > 10 ? 0 : i;
    }
    g->sim->state_timer = 0;
    scroll = 0;
    glow = 0;
    g->sub = 1;
}

// FUN_800DE7B4: a player's button layout (the save block's actions per pad button) into the
// key table the pads are read with.
void MainMenu::button_map(gd::Int player) {
    gd::PI32 table = g->key_tables[player];
    for (gd::Int k = 0; k < 8; ++k) {
        table[k] = g->data->button_actions[g->progress->button_action(player, k)];
    }
    for (gd::Int k = 0; k < 8; ++k) {
        table[8 + k] = g->data->button_tail[k];
    }
    g->key_tables[player] = table;
}

}  // namespace t3
