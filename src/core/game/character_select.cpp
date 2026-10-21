// remake/core/game/character_select.gd
#include "core/game/character_select.h"

#include "content/fight_tables.h"
#include "content/flow_data.h"
#include "content/json_file.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/match_flow.h"
#include "core/game/mode_region.h"
#include "core/game/mode_rules.h"
#include "core/game/opponents.h"
#include "core/game/progress_rules.h"
#include "core/game/quick_select.h"
#include "core/game/vs_screen.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

CharacterSelect::Row::Row(gd::Int first_cell, gd::Int row_index, gd::Int row_y) {
    first = first_cell;
    cur = first_cell;
    last = first_cell;
    row = row_index;
    y = row_y;
}

CharacterSelect::CharacterSelect(GameFlow* flow) : FlowPart(flow) {
    ctx = gd::make<ByteBlock>(SIZE);
    cells = gd::make<ByteBlock>(CELL_COUNT * CELL_SIZE);
    // _init(flow)
    ctx->bytes = flow->data->select_context.duplicate();
    for (gd::Int i = 0; i < SPARK_COUNT; ++i) {
        sparks.append(gd::make<Spark>());
    }
}

gd::Int CharacterSelect::rec(gd::Int player) {
    return RECORDS + RECORD_SIZE * player;
}

gd::Int CharacterSelect::r32(gd::Int player, gd::Int at) {
    return ctx->u32(rec(player) + at);
}

gd::Int CharacterSelect::rs32(gd::Int player, gd::Int at) {
    return ctx->s32(rec(player) + at);
}

void CharacterSelect::w32(gd::Int player, gd::Int at, gd::Int v) {
    ctx->put32(rec(player) + at, v);
}

gd::Int CharacterSelect::frames() {
    return ctx->s32(8);
}

// A cell's byte (the dummy cell reads 0x16 everywhere).
gd::Int CharacterSelect::cell8(gd::Int cell, gd::Int at) {
    return cell == DUMMY_CELL ? NO_CELL : cells->u8(CELL_SIZE * cell + at);
}

gd::Int CharacterSelect::cell_key(gd::Int cell) {
    return cells->s16(CELL_SIZE * cell + 6);
}

void CharacterSelect::_cell_put8(gd::Int cell, gd::Int at, gd::Int v) {
    cells->put8(CELL_SIZE * cell + at, v);
}

void CharacterSelect::step() {
    if (g->sub > 0 && frames() >= DRAW_FROM && g->menu_exit()) {
        return;
    }
    switch (g->sub) {
        case 0: {
            g->music_stop();
            ctx->put32(0, g->region->mode);
            grid_build();
            gd::Int at = ctx->u32(4) * LAYOUT_SIZE;
            for (gd::Int k = 0; k < LAYOUT_SIZE; ++k) {
                ctx->put8(LAYOUT + k, g->data->select_layouts[at + k]);
            }
            g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
            g->music(MUSIC_SELECT);
            ctx->put32(8, 0);
            ctx->put32(0xC, 2);
            for (gd::Int p = 0; p < 2; ++p) {
                w32(p, R_STATE, SET_UP);
                gd::Int b = g->progress->last_character(p);
                w32(p, R_CHOSEN_KEY, b >> 2);
                w32(p, R_CHOSEN_COSTUME, b & 3);
                player_step(p);
            }
            for (const gd::Ref<Spark>& spark : sparks) {
                spark->active = 0;
                spark->started = 0;
            }
            g->sub = 1;
            break;
        }
        case 1: {
            gd::Int t = ctx->s32(0x14);
            if (t > 0) {
                ctx->put32(0x14, t - 1);
            }
            bool a = player_step(0);
            bool b = player_step(1);
            if (a && b) {
                g->sub = 2;
            }
            break;
        }
        case 2:
            commit();
            g->state = GameFlow::target_state(g->region->vs_next_state);
            g->sub = g->region->vs_next_sub;
            break;
        default:
            break;
    }
    portrait_update();
    if (frames() >= DRAW_FROM) {
        // The drawing's own state: the sparks, the portraits' slide-in (FUN_8010F760) and the NEW
        // arrows' blink (FUN_8010EC9C).
        _sparks();
        for (gd::Int p = 0; p < 2; ++p) {
            gd::Int slide = rs32(p, R_CHOSEN_ANIM);
            if (slide != 0) {
                w32(p, R_CHOSEN_ANIM, slide - 1);
            }
        }
        if (ctx->u32(0x10) != 0) {
            ctx->put32(0x10, ctx->u32(0x10) - 1);
        }
        gd::Int t = ctx->s32(0xC);
        if (t > 0) {
            ctx->put32(0xC, t - 1);
        }
    }
}

void CharacterSelect::_sparks() {
    gd::Int n = ctx->u32(8);
    if ((n & 3) == 0) {
        gd::Int k = (n >> 3) & 3;
        k = ((k << 1) | (k >> 1)) & 3;
        gd::Int p = (n >> 2) & 1;
        gd::Ref<Spark> free = nullptr;
        for (const gd::Ref<Spark>& spark : sparks) {
            if (spark->active == 0) {
                spark->started = 0;
                free = spark;
                break;
            }
        }
        if (free != nullptr) {
            gd::Int r = Opponents::frame_random(g->fight);
            gd::Int a = (k << 10) | ((Fx::w32(r) >> 2) & 0x3FF);
            free->active = 1;
            free->x = (ctx->u16(rec(p) + R_LAYOUT) + (a * 0x7E >> 12)) & 0xFFFF;
            gd::Int y = ctx->u16(LAYOUT_SPARK_Y);
            free->top = y;
            free->y = y;
        }
    }
    gd::Int now = g->fight->vblank & 0xFFFF;
    for (const gd::Ref<Spark>& spark : sparks) {
        if (spark->active != 1) {
            continue;
        }
        if (spark->started == 0) {
            spark->bright = SPARK_BRIGHT;
            spark->frame = now;
            spark->started = 1;
            spark->fade = (Opponents::frame_random(g->fight) & 3) + 4;
        } else if (spark->started != 1) {
            continue;
        }
        spark->bright -= spark->fade;
        spark->y = Fx::s16((spark->top - ((now - spark->frame) << 4)) & 0xFFFF);
        if (spark->y + SPARK_HEIGHT < 0 || spark->bright <= 0) {
            spark->active = 0;
        }
    }
}

// ---- the grid (FUN_8010DC04, FUN_8010DA30, FUN_8010D810)

void CharacterSelect::grid_build() {
    gd::Int mask = g->progress->unlocked & 0x1FFFFF;
    ctx->put32(0x10, 0);
    gd::Int shown = 0;
    for (gd::Int i = 0; i < CELL_COUNT; ++i) {
        gd::Int at = CELL_SIZE * i;
        cells->put8(at, i);
        gd::Int key = g->data->select_grid_keys[i];
        if ((gd::shr(mask, key) & 1) != 0) {
            cells->put16(at + 6, key);
            if (i % 11 < 10) {
                cells->put8(at + 5, 1);
            } else {
                cells->put8(at + 5, 0);
                ctx->put32(0x10, NEW_BLINK);
            }
            shown |= gd::shl(1, key);
        } else {
            cells->put16(at + 6, NO_CELL);
            cells->put8(at + 5, 0);
        }
        for (gd::Int k = 1; k < 5; ++k) {
            cells->put8(at + k, NO_CELL);
        }
    }
    ctx->put32(0x20, ProgressRules::popcount(shown));
    ctx->put32(0x1C, shown);
    gd::Arr<gd::Ref<Row>> rows{gd::make<Row>(0, 0, 0), gd::make<Row>(11, 1, 0x3E)};
    for (gd::Int r = 0; r < 2; ++r) {
        gd::Ref<Row> row = rows[r];
        bool found = false;
        gd::Int last = row->first;
        for (gd::Int k = 0; k < 11; ++k) {
            gd::Int c = 11 * r + k;
            if (cell_key(c) == NO_CELL) {
                continue;
            }
            if (!found) {
                row->cur = c;
                found = true;
            } else {
                _cell_put8(last, 4, c);
                _cell_put8(c, 3, last);
            }
            row->total = (row->total + 1) & 0xFF;
            if (k < 10) {
                row->remaining = (row->remaining + 1) & 0xFF;
            }
            last = c;
        }
        _cell_put8(last, 4, row->cur);          // close the ring
        _cell_put8(row->cur, 3, last);
        row->last = last;
    }
    ctx->put32(4, rows[1]->remaining != 0 ? 1 : 0);
    _pair_rows(rows[0], rows[1]);
}

// The cell's link toward the other row (up from the bottom row, down from the top).
gd::Int CharacterSelect::_vertical(const gd::Ref<Row>& row, gd::Int cell) {
    return row->row != 0 ? cell8(cell, 1) : cell8(cell, 2);
}

gd::Int CharacterSelect::_row_step(const gd::Ref<Row>& row, gd::Int cell) {
    if (row->phase >= 3) {
        return cell;
    }
    if (row->phase == 0) {
        if (row->skip != 0) {
            _cell_put8(cell, 3, _vertical(row, cell));
            row->skip = (row->skip - 1) & 0xFF;
            return cell;
        }
        if (row->odd != 0) {
            _cell_put8(cell, 3, _vertical(row, cell));
        }
        row->phase += 1;
    }
    if (row->phase == 1) {
        if (row->remaining != 0) {
            if (row->odd != 0 && row->remaining == row->middle) {
                row->middle = 0xFF;
                return cell;
            }
            cells->put16(CELL_SIZE * cell + 8, row->x);
            cells->put16(CELL_SIZE * cell + 0xA, row->y);
            row->remaining = (row->remaining - 1) & 0xFF;
            row->x = (row->x + CELL_STEP) & 0xFFFF;
            if (row->remaining != 0) {
                return cell8(cell, 4);
            }
        }
        row->phase += 1;
    }
    if (row->odd != 0) {
        _cell_put8(cell, 4, _vertical(row, cell));
    } else if (row->half != 0) {
        _cell_put8(cell, 4, cell8(_vertical(row, cell), 4));
    }
    row->phase += 1;
    return cell;
}

void CharacterSelect::_pair_rows(const gd::Ref<Row>& a, const gd::Ref<Row>& b) {
    gd::Ref<Row> big = a->remaining < b->remaining ? b : a;
    gd::Ref<Row> small = a->remaining < b->remaining ? a : b;
    if (big->remaining == 0) {
        big->cur = DUMMY_CELL;
        big->phase = 3;
    }
    if (small->remaining != 0) {
        gd::Int diff = (big->remaining - small->remaining) & 0xFFFFFFFFLL;
        if ((diff & 1) != 0) {
            small->odd = 1;
        }
        small->half = (diff >> 1) & 0xFF;
        small->skip = small->half;
        small->middle = (small->remaining >> 1) + 1;
        small->x = (small->x + ((10 - small->remaining) * CELL_STEP >> 1)) & 0xFFFF;
    } else {
        small->cur = DUMMY_CELL;
        small->phase = 3;
    }
    gd::Int c1 = big->cur;
    gd::Int c0 = small->cur;
    // `for i in big.remaining` takes the count once; _row_step counts big.remaining down.
    gd::Int count = big->remaining;
    for (gd::Int i = 0; i < count; ++i) {
        if (big->phase < 3) {
            _cell_put8(c1, big->row != 0 ? 1 : 2, cell8(c0, 0));
        }
        if (small->phase < 3) {
            _cell_put8(c0, small->row != 0 ? 1 : 2, cell8(c1, 0));
        }
        c1 = _row_step(big, c1);
        c0 = _row_step(small, c0);
    }
}

// ---- the players (FUN_8010E250)

bool CharacterSelect::player_step(gd::Int player) {
    gd::Int other = (player + 1) & 1;
    gd::Ref<GameGlobals> gl = g->globals;
    bool done = false;
    switch (r32(player, R_STATE)) {
        case SET_UP: {
            for (gd::Int k = 0; k < 0x28; ++k) {
                ctx->put8(rec(player) + R_LAYOUT + k, g->data->select_player_layout[0x28 * player + k]);
            }
            w32(player, R_RECORD5, 0);
            w32(player, R_BASE, 0);
            gd::Int n = ctx->s32(0x20);
            gd::Int t = n < 11 ? 0x3C : (n - 10) * 3 + 0x3C;
            ctx->put32(0x18, t);
            ctx->put32(0x14, t * 20);
            w32(player, R_TIMER, 0);
            w32(player, R_CHOSEN_ANIM, 0);
            if (gl->player_keep[player] != 0) {
                w32(player, R_TIMER, CHOSEN_WAIT);
                w32(player, R_CHOSEN_ANIM, CHOSEN_ANIM);
            }
            _start(player);
            gd::Int mode = ctx->u32(0);
            gd::Int active = gl->player_active[player];
            if (gd::Arr<gd::Int>{GameMode::TIME_ATTACK, GameMode::SURVIVAL, GameMode::FORCE}.has(mode)) {
                w32(player, R_STATE, active != 0 ? CHOOSING : NONE);
            } else if (mode == GameMode::PRACTICE) {
                w32(player, R_STATE, active != 0 ? CHOOSING : WAITING);
            } else {
                gd::Int kind = (active != 0 ? 2 : 0) | (gl->player_keep[player] != 0 ? 1 : 0);
                gd::Arr<gd::Int> states{CPU, CPU, CHOOSING, CHOSEN};
                w32(player, R_STATE, states[kind]);
            }
            break;
        }
        case CHOOSING:
            _move(player, player);
            if (!_choose(player, g->pressed(player))) {
                w32(player, R_TIMER, CHOSEN_WAIT);
                w32(player, R_CHOSEN_ANIM, CHOSEN_ANIM);
                w32(player, R_STATE, CHOSEN);
            }
            break;
        case WAITING:
            if (r32(other, R_STATE) == CHOSEN) {
                w32(player, R_STATE, CHOOSING_OTHER);
                w32(player, R_CELL, r32(other, R_CELL));
            }
            break;
        case CHOOSING_OTHER:
            _move(player, other);
            if ((g->pressed(other) & PadState::SELECT) != 0) {
                // Select: back to the other side's choice.
                w32(other, R_STATE, CHOOSING);
                w32(player, R_STATE, WAITING);
                ctx->put32(0xC, 2);
                w32(player, R_KEY, NO_CELL);
                w32(player, R_COSTUME, 0);
                g->sound(SOUND_CHOOSE);
            } else if (!_choose(player, g->pressed(other))) {
                w32(player, R_TIMER, CHOSEN_WAIT);
                w32(player, R_CHOSEN_ANIM, CHOSEN_ANIM);
                w32(player, R_STATE, CHOSEN_BY_OTHER);
            }
            break;
        case CPU:
            if (g->region->challengers != 0 && g->match_flow->challenger_join(player)) {
                w32(player, R_STATE, SET_UP);
                g->sound(SOUND_JOIN);
            } else {
                done = true;
            }
            break;
        case NONE:
            done = true;
            break;
        case CHOSEN:
        case CHOSEN_BY_OTHER: {
            gd::Int pad = r32(player, R_STATE) == CHOSEN ? player : other;
            if ((g->pressed(pad) & PadState::START) != 0) {
                w32(player, R_TIMER, 0);
            }
            gd::Int t = rs32(player, R_TIMER);
            if (t > 0) {
                w32(player, R_TIMER, t - 1);
            } else {
                done = true;
            }
            break;
        }
        default:
            break;
    }
    gd::Int s = r32(player, R_STATE);
    w32(player, R_ANIM, g->data->select_state_anim[2 * s]);
    w32(player, R_ANIM + 4, g->data->select_state_anim[2 * s + 1]);
    return done;
}

void CharacterSelect::_start(gd::Int player) {
    gd::Ref<GameGlobals> gl = g->globals;
    gd::Int kind = (gl->player_active[player] != 0 ? 2 : 0) | (gl->player_keep[player] != 0 ? 1 : 0);
    gd::Int key = NO_CELL;
    gd::Int costume = 0;
    if (kind == 1) {
        key = gl->player_char[player];
        costume = gl->player_costume[player];
    } else if (kind != 0) {
        if (kind == 2) {
            key = r32(player, R_CHOSEN_KEY);
            costume = r32(player, R_CHOSEN_COSTUME);
        } else {
            key = gl->player_char[player];
            costume = gl->player_costume[player];
        }
        gd::Int i = _find_cell(Fx::w32(key));
        if (i >= 0) {
            w32(player, R_CELL, i);
        } else {
            i = ctx->u16(rec(player) + R_DEFAULT_CELL);
            w32(player, R_CELL, i);
            key = cell_key(i);
        }
    }
    w32(player, R_SHOWN_KEY, NO_CELL);
    w32(player, R_KEY, key);
    w32(player, R_COSTUME, costume);
    w32(player, R_SHOWN_COSTUME, 1);
}

gd::Int CharacterSelect::_find_cell(gd::Int key) {
    if (key < NO_CELL) {
        for (gd::Int i = 0; i < CELL_COUNT; ++i) {
            if (cell_key(i) == key) {
                return i;
            }
        }
    }
    return -1;
}

void CharacterSelect::_move(gd::Int player, gd::Int pad) {
    gd::Int rep = g->repeat(pad);
    gd::Int v = (rep & ~PadState::VERTICAL & 0xFFFF) | (g->pressed(pad) & PadState::VERTICAL);
    if ((rep & PadState::HORIZONTAL) != 0) {
        v = rep & PadState::HORIZONTAL;
    }
    gd::Int link = 0;
    switch (v) {
        case 0x1000: link = 1; break;
        case 0x4000: link = 2; break;
        case 0x8000: link = 3; break;
        case 0x2000: link = 4; break;
        default: break;
    }
    gd::Int cell = r32(player, R_CELL);
    gd::Int next = link != 0 ? cell8(cell, link) : NO_CELL;
    if (next != NO_CELL) {
        w32(player, R_CELL, next);
        w32(player, R_COSTUME, 0);
        ctx->put32(0xC, 2);
        g->sound(SOUND_MOVE);
    }
    w32(player, R_KEY, cell_key(r32(player, R_CELL)));
}

bool CharacterSelect::_choose(gd::Int player, gd::Int pressed) {
    gd::Int extra = (gd::shr(g->progress->start_costumes, r32(player, R_KEY) & 31) & 1) != 0 ? START_COSTUME_BUTTONS : 0;
    if (((extra | GameFlow::FACE_BUTTONS) & pressed) == 0 && ctx->u32(0x14) != 0) {
        return true;
    }
    gd::Int costume = (extra & pressed) != 0 ? 2 : ((pressed & (PadState::CIRCLE | PadState::CROSS)) != 0 ? 1 : 0);
    gd::Int key = rs32(player, R_KEY);
    if (key < 0x17) {
        w32(player, R_COSTUME, costume);
        for (gd::Int q = 0; q < 2; ++q) {
            w32(q, R_CLASH, r32(q, R_KEY));
            w32(q, R_CLASH + 4, r32(q, R_COSTUME));
            gd::Int s = r32(q, R_STATE);
            w32(q, R_CLASH + 8, g->data->select_decided[2 * s]);
            w32(q, R_CLASH + 12, g->data->select_decided[2 * s + 1]);
        }
        QuickSelect::costume_clash(ctx, rec(player) + R_CLASH, rec((player + 1) & 1) + R_CLASH);
        for (gd::Int q = 0; q < 2; ++q) {
            w32(q, R_KEY, r32(q, R_CLASH));
            w32(q, R_COSTUME, r32(q, R_CLASH + 4));
        }
        w32(player, R_CHOSEN_KEY, r32(player, R_KEY));
        w32(player, R_CHOSEN_COSTUME, r32(player, R_COSTUME));
        g->sound(SOUND_CHOOSE);
    }
    return key >= NO_CELL;
}

void CharacterSelect::commit() {
    gd::Ref<GameGlobals> gl = g->globals;
    for (gd::Int q = 0; q < 2; ++q) {
        gd::Int key = r32(q, R_CHOSEN_KEY);
        gd::Int costume = r32(q, R_CHOSEN_COSTUME);
        gd::Int active = gl->player_active[q];
        if (g->region->mode != GameMode::PRACTICE || active != 0) {
            g->progress->set_last_character(q, ((key << 2) | (costume & 3)) & 0xFF);
        }
        if (active != 0 || ctx->u32(0) == 5) {
            gl->player_char[q] = key & 0xFFFF;
            gl->player_costume[q] = costume & 0xFFFF;
            gl->player_keep[q] = 1;
            gl->player_cpu[q] = 0;
        }
    }
}

void CharacterSelect::portrait_update() {
    gd::Int n = (ctx->u32(8) + 1) & 0xFFFFFFFFLL;
    ctx->put32(8, n);
    gd::Int p = n & 1;
    if (r32(p, R_SHOWN_KEY) == r32(p, R_KEY) && r32(p, R_SHOWN_COSTUME) == r32(p, R_COSTUME)) {
        return;
    }
    gd::Int key = rs32(p, R_KEY);
    gd::Int costume = rs32(p, R_COSTUME);
    w32(p, R_SHOWN_KEY, key);
    w32(p, R_SHOWN_COSTUME, costume);
    gd::Int attr = g->data->attribute(key, costume) & 0xFF;
    gd::Int k = (key << 2) | costume;
    gd::Json record = g->fight->tables->character(k > 0x5C || k < 0 ? ModeRules::KEY_NONE : k);
    // Untyped Array in GDScript: the JSON record's "unknown" list.
    const gd::Json& extra = record["unknown"];
    w32(p, R_RECORD5, JsonFile::number(extra[0]));
    w32(p, R_BASE, JsonFile::number(record["base"]));
    bool facing_right = ctx->u16(rec(p) + R_FACING) == 1;
    bool flip = (((attr & 0x20) != 0) != facing_right) && r32(p, R_SHOWN_KEY) != NO_CELL;
    w32(p, R_FLIP, flip ? 1 : 0);
    g->progress->put8(VsScreen::PROGRESS_PICTURES + p, attr & 0x1F);
}

}  // namespace t3
