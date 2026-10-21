// remake/core/game/quick_select.gd
#include "core/game/quick_select.h"

#include "content/flow_data.h"
#include "core/ball/tekken_ball.h"
#include "core/fight/fight_simulation.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/game/match_flow.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

QuickSelect::QuickSelect(GameFlow* flow) : FlowPart(flow) {
    ctx = gd::make<ByteBlock>(SIZE);
}

gd::Int QuickSelect::mode() {
    return ctx->u32(0);
}

bool QuickSelect::fading() {
    return ctx->u32(8) != 0;
}

gd::Int QuickSelect::fade_level() {
    return ctx->s32(0xC);
}

gd::Int QuickSelect::rec(gd::Int player) {
    return RECORDS + RECORD_SIZE * player;
}

gd::Int QuickSelect::r32(gd::Int player, gd::Int at) {
    return ctx->u32(rec(player) + at);
}

gd::Int QuickSelect::rs32(gd::Int player, gd::Int at) {
    return ctx->s32(rec(player) + at);
}

gd::Int QuickSelect::r16(gd::Int player, gd::Int at) {
    return ctx->u16(rec(player) + at);
}

void QuickSelect::w32(gd::Int player, gd::Int at, gd::Int v) {
    ctx->put32(rec(player) + at, v);
}

// The costume key in a grid cell (s16 x, s16 y, u8 key).
gd::Int QuickSelect::cell_key(gd::Int column, gd::Int row) {
    return g->data->quick_grid[6 * ((column + row * COLUMNS) & 0xFFFFFFFFLL) + 4];
}

void QuickSelect::step() {
    if (g->sub != 0 && ctx->u32(8) == 0 && g->menu_exit()) {
        return;
    }
    switch (g->sub) {
        case 0:
            g->display(0);
            g->sub = 1;
            return;
        case 1:
            ctx->put32(0xC, 0);
            ctx->put32(4, 0);
            ctx->put32(0x10, 0);
            ctx->put32(0x14, 0);
            ctx->put32(0, g->region->mode);
            ctx->put32(8, 1);
            for (gd::Int p = 0; p < 2; ++p) {
                w32(p, R_STATE, SET_UP);
                w32(p, R_PLAYER, p);
                w32(p, R_LAST, g->progress->last_character(p));
                player_step(p);
            }
            if (g->region->mode == GameMode::BALL) {
                g->sim->ball()->preview();
            }
            g->music(MUSIC_SELECT);
            g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
            g->sub = 2;
            break;
        case 2: {
            gd::Int a = player_step(0);
            gd::Int b = player_step(1);
            if ((a & b) != 0) {
                g->sub = 3;
            }
            break;
        }
        case 3:
            commit();
            g->state = GameFlow::target_state(g->region->vs_next_state);
            g->sub = g->region->vs_next_sub;
            break;
        default:
            break;
    }
    if (ctx->u32(8) == 1) {
        gd::Int level = ctx->s32(0xC) + 0x10;
        ctx->put32(0xC, level);
        if (level > 0x100) {
            ctx->put32(8, 0);
        }
    }
}

// FUN_800513FC: 2 for an active human, +1 when the pick is kept.
gd::Int QuickSelect::_kind(gd::Int player) {
    return (g->globals->player_active[player] != 0 ? 2 : 0) | (g->globals->player_keep[player] != 0 ? 1 : 0);
}

// The cursor on the cell of `key` (else of the record's default key).
void QuickSelect::_find(gd::Int player, gd::Int key) {
    gd::Int target = key < LOCKED ? key & ~3 : r32(player, 0x84);
    for (gd::Int row = 0; row < ROWS; ++row) {
        for (gd::Int column = 0; column < COLUMNS; ++column) {
            if (cell_key(column, row) == (target & 0xFFFFFFFFLL)) {
                w32(player, R_COLUMN, column);
                w32(player, R_ROW, row);
                return;
            }
        }
    }
}

// The common set-up: one member, the kept character (or the last pick) under the cursor.
void QuickSelect::_start_cursor(gd::Int player) {
    w32(player, R_COUNT, 0);
    w32(player, R_WANTED, 1);
    w32(player, 0x30, 1);
    gd::Int key = 0;
    if (g->globals->player_keep[player] != 0) {
        w32(player, R_COUNT, 1);
        key = gd::Int(g->globals->player_char[player]) * 4 + g->globals->player_costume[player];
        w32(player, R_MEMBERS, key);
    } else {
        key = rs32(player, R_LAST);
        w32(player, R_MEMBERS, LOCKED);
    }
    _find(player, key);
}

bool QuickSelect::_undo(gd::Int player) {
    gd::Int count = rs32(player, R_COUNT);
    if (count <= 0) {
        return false;
    }
    gd::Int n = count - 1;
    gd::Int key = rs32(player, R_MEMBERS + 4 * n);
    w32(player, R_LAST, key);
    w32(player, R_MEMBERS + 4 * n, LOCKED);
    w32(player, R_COUNT, n);
    w32(player, R_AVAILABLE, r32(player, R_AVAILABLE) | gd::shl(1, (key >> 2) & 31));
    g->sound(SOUND_PICK);
    return true;
}

gd::Int QuickSelect::player_step(gd::Int player) {
    gd::Int other = (player + 1) & 1;
    gd::Int done = 0;
    gd::Int state = r32(player, R_STATE);
    switch (state) {
        case SET_UP:
            _setup(player);
            break;
        case TEAM_SIZE:
            _team_size_step(player);
            break;
        case TEAM_CHOOSING:
        case CHOOSING:
            _choose_step(player, state);
            break;
        case HANDICAP:
            _handicap_step(player);
            break;
        case WAITING:
            if (r32(other, R_STATE) == CHOSEN) {
                w32(player, R_COLUMN, r32(other, R_COLUMN));
                w32(player, R_STATE, CHOOSING_OTHER);
                w32(player, R_ROW, r32(other, R_ROW));
            }
            break;
        case CHOOSING_OTHER: {
            _move(player, other);
            gd::Int pressed = g->pressed(other);
            if ((pressed & PadState::SELECT) != 0) {
                _undo(other);
                w32(player, R_STATE, WAITING);
                w32(other, R_STATE, CHOOSING);
                for (gd::Int k = 0; k < 3; ++k) {
                    w32(other, 0x18 + 4 * k, k + 1);
                }
            } else if (pick(player, pressed) == 0) {
                w32(player, R_STATE, CHOSEN_BY_OTHER);
            }
            break;
        }
        case CPU:
        case CPU_TEAM:
            if (state == CPU_TEAM) {
                w32(player, R_WANTED, r32(other, R_WANTED));
            }
            if (g->region->challengers != 0 && g->match_flow->challenger_join(player)) {
                w32(player, R_STATE, SET_UP);
                g->sound(SOUND_JOIN);
            } else {
                done = 1;
            }
            break;
        case NONE:
        case CHOSEN:
        case HANDICAP_SET:
        case CHOSEN_BY_OTHER:
            done = 1;
            break;
        case BALL_WAITING:
            if ((g->pressed(player) & PadState::SELECT) != 0) {
                _undo(player);
                w32(player, R_STATE, CHOOSING);
                if (g->region->ctx8(TekkenBall::CHOOSER) == gd::shl(1, player)) {
                    g->region->ctx_put8(TekkenBall::CHOOSER, 0);
                }
            } else {
                done = 1;
            }
            break;
        case BALL_TYPE: {
            gd::Int chosen = g->sim->ball()->side_select(g->pressed(g->region->ctx8(TekkenBall::SERVE_SIDE)), g->sim->events);
            if (chosen == -1) {
                _undo(player);
                w32(player, R_STATE, CHOOSING);
                g->region->ctx_put8(TekkenBall::CHOOSER, 0);
            } else if (chosen == 1) {
                w32(player, R_STATE, BALL_WAITING);
            }
            break;
        }
        default:
            break;
    }
    gd::Int s = r32(player, R_STATE);
    for (gd::Int k = 0; k < 3; ++k) {
        w32(player, 0x18 + 4 * k, g->data->quick_state_anim[3 * s + k]);
    }
    return done;
}

void QuickSelect::_team_size_step(gd::Int player) {
    gd::Int n = r32(player, R_WANTED);
    gd::Int rep = g->repeat(player);
    gd::Int lr = ((rep >> 13) & 1) - (rep >> 15);
    gd::Int v = n;
    if (lr != 0 && ((n + lr) & 0xFFFFFFFFLL) < 9) {
        v = n + lr;
    }
    if (v > 8) {
        v = 8;
    }
    if (v == 0) {
        v = 1;
    }
    if (n != v) {
        g->sound(SOUND_MOVE);
    }
    w32(player, R_WANTED, v);
    gd::Int pressed = g->pressed(player);
    if ((pressed & CHOOSE) != 0) {
        w32(player, R_ORDER, (pressed & (PadState::CIRCLE | PadState::CROSS)) != 0 ? 1 : 0);
        g->sound(SOUND_PICK);
        w32(player, R_STATE, TEAM_CHOOSING);
    }
}

void QuickSelect::_choose_step(gd::Int player, gd::Int state) {
    _move(player, player);
    gd::Int pressed = g->pressed(player);
    bool finish = false;
    if (state == TEAM_CHOOSING && (pressed & PadState::START) != 0) {
        g->sound(SOUND_PICK);
        finish = true;
    } else if (state == TEAM_CHOOSING && (pressed & PadState::SELECT) != 0) {
        if (!_undo(player)) {
            w32(player, R_STATE, TEAM_SIZE);
        } else {
            finish = pick(player, pressed) == 0;
        }
    } else {
        finish = pick(player, pressed) == 0;
    }
    if (finish) {
        switch (mode()) {
            case GameMode::ARCADE:
            case GameMode::TIME_ATTACK:
            case GameMode::SURVIVAL:
            case GameMode::PRACTICE:
            case GameMode::DEMO:
            case GameMode::FORCE:
                w32(player, R_STATE, CHOSEN);
                break;
            case GameMode::VS:
                w32(player, R_STATE, HANDICAP);
                break;
            case GameMode::TEAM:
                w32(player, R_STATE, NONE);
                break;
            case GameMode::BALL:
                w32(player, R_STATE, BALL_WAITING);
                if (g->region->ctx8(TekkenBall::CHOOSER) == 0) {
                    g->region->ctx_put8(TekkenBall::CHOOSER, gd::shl(1, player));
                    g->region->ctx_put8(TekkenBall::SERVE_SIDE, player);
                    w32(player, R_STATE, BALL_TYPE);
                }
                break;
            default:
                break;
        }
    }
}

void QuickSelect::_handicap_step(gd::Int player) {
    w32(player, 0x5C, r16(player, 0x9A));
    gd::Int h = r32(player, R_HANDICAP);
    gd::Int rep = g->repeat(player);
    gd::Int lr = ((rep >> 13) & 1) - (rep >> 15);
    if (g->region->mode == GameMode::BALL) {
        lr *= 2;
    }
    gd::Int v = h;
    if (lr != 0) {
        if (Fx::s16(r16(player, 0x98)) == 0) {
            lr = -lr;
        }
        if (((h + lr) & 0xFFFFFFFFLL) < 8) {
            v = h + lr;
        }
    }
    if (h != v) {
        g->sound(SOUND_MOVE);
    }
    w32(player, R_HANDICAP, v);
    gd::Int pressed = g->pressed(player);
    if ((pressed & PadState::SELECT) != 0) {
        w32(player, 0x5C, r16(player, 0x9C));
        _undo(player);
        w32(player, R_STATE, CHOOSING);
    } else if ((pressed & GameFlow::FACE_BUTTONS) != 0) {
        g->sound(SOUND_PICK);
        w32(player, R_STATE, HANDICAP_SET);
    }
}

void QuickSelect::_setup(gd::Int player) {
    for (gd::Int k = 0; k < 0x34; ++k) {
        ctx->put8(rec(player) + R_PREFS + k, g->data->quick_prefs[0x34 * player + k]);
    }
    w32(player, R_AVAILABLE, g->unlocked_of(player));   // native: player 2's own in a NETWORK MODE session
    gd::Int m = mode();
    gd::Int kind = _kind(player);
    switch (m) {
        case GameMode::ARCADE:
        case GameMode::DEMO:
        case GameMode::BALL:
            if (kind == 2) {
                w32(player, R_STATE, CHOOSING);
            } else if (kind == 3) {
                w32(player, R_STATE, m != GameMode::BALL ? CHOSEN : BALL_WAITING);
            } else if (kind < 2) {
                w32(player, R_STATE, CPU);
            }
            _start_cursor(player);
            break;
        case GameMode::TIME_ATTACK:
        case GameMode::SURVIVAL:
        case GameMode::FORCE:
            w32(player, R_STATE, g->globals->player_active[player] != 0 ? CHOOSING : NONE);
            _start_cursor(player);
            break;
        case GameMode::PRACTICE:
            w32(player, R_STATE, g->globals->player_active[player] != 0 ? CHOOSING : WAITING);
            _start_cursor(player);
            break;
        case GameMode::VS:
            w32(player, R_STATE, CHOOSING);
            _start_cursor(player);
            break;
        case GameMode::TEAM:
            w32(player, R_STATE, g->globals->player_active[player] != 0 ? TEAM_SIZE : CPU_TEAM);
            w32(player, R_COUNT, 0);
            w32(player, R_WANTED, 4);
            w32(player, 0x30, 0);
            w32(player, R_ORDER, 0);
            for (gd::Int i = 0; i < 8; ++i) {
                w32(player, R_MEMBERS + 4 * i, LOCKED);
            }
            w32(player, R_WANTED, g->region->ctx8(0x65 + 13 * player));
            _find(player, LOCKED);
            w32(player, 0x10, r16(player, 0xA8));
            w32(player, 0x14, r16(player, 0xAA));
            break;
        default:
            break;
    }
    switch (m) {
        case GameMode::ARCADE:
        case GameMode::TIME_ATTACK:
        case GameMode::SURVIVAL:
        case GameMode::PRACTICE:
        case GameMode::DEMO:
        case GameMode::FORCE:
            w32(player, 0x10, r16(player, 0x90));
            w32(player, 0x14, r16(player, 0x92));
            break;
        case GameMode::VS:
        case GameMode::BALL: {
            gd::Int handicap = g->region->ctx16(0x3A + 4 * player);
            w32(player, 0x10, r16(player, 0x94));
            w32(player, 0x14, r16(player, 0x96));
            w32(player, 0x5C, r16(player, 0x9C));
            w32(player, 0x60, r16(player, 0x9C));
            w32(player, R_HANDICAP, handicap);
            if (m == GameMode::BALL) {
                // FUN_800B5C4C: the bar starts at 6 and slides in from the side.
                w32(player, R_HANDICAP, 6);
                w32(player, 0x10, r16(player, 0x94));
                w32(player, 0x14, r16(player, 0x96));
                w32(player, 0x5C, r16(player, 0x9C));
                w32(player, 0x60, r16(player, 0x9C));
                ctx->put16(rec(player) + 0x9A, r16(player, 0x9A) + (player != 0 ? 0x28 : -0x28));
            }
            break;
        }
        default:
            break;
    }
}

void QuickSelect::_move(gd::Int player, gd::Int pad) {
    gd::Int rep = g->repeat(pad);
    gd::Int column = r32(player, R_COLUMN);
    gd::Int row = r32(player, R_ROW);
    if (column >= COLUMNS) {
        column = 0;
    }
    if (row >= ROWS) {
        row = 0;
    }
    gd::Int lr = ((rep >> 13) & 1) - (rep >> 15);
    gd::Int new_column = column;
    gd::Int new_row = row;
    if (lr != 0) {
        gd::Int t = (column + lr) & 0xFFFFFFFFLL;
        new_column = t < COLUMNS ? t : 6 - column;
    } else {
        gd::Int t = (row + ((rep >> 14) & 1) - ((rep >> 12) & 1)) & 0xFFFFFFFFLL;
        if (t < ROWS) {
            new_row = t;
        }
    }
    if (r32(player, R_COLUMN) != (new_column & 0xFFFFFFFFLL) || r32(player, R_ROW) != (new_row & 0xFFFFFFFFLL)) {
        g->sound(SOUND_MOVE);
    }
    w32(player, R_COLUMN, new_column);
    w32(player, R_ROW, new_row);
}

gd::Int QuickSelect::pick(gd::Int player, gd::Int pressed) {
    gd::Int key = cell_key(r32(player, R_COLUMN), r32(player, R_ROW));
    gd::Int c = key >> 2;
    if ((gd::shr(r32(player, R_AVAILABLE), c & 31) & 1) == 0) {
        key = (gd::shr(g->unlocked_of(player), c & 31) & 1) != 0 ? TAKEN : LOCKED;
        c = key >> 2;
    }
    gd::Int extra = (gd::shr(g->costumes_of(player), c & 31) & 1) != 0 ? START_COSTUME_BUTTONS : 0;
    if (((extra | 0xF0) & pressed) == 0) {
        return 1;
    }
    gd::Int costume = (extra & pressed) != 0 ? 2 : ((pressed & (PadState::CIRCLE | PadState::CROSS)) != 0 ? 1 : 0);
    gd::Int k = key + costume;
    if (key < LOCKED) {
        gd::Int n = r32(player, R_COUNT);
        w32(player, R_MEMBERS + 4 * n, k);
        if (mode() != GameMode::TEAM) {
            for (gd::Int q = 0; q < 2; ++q) {
                gd::Int m = r32(q, R_MEMBERS);
                w32(q, R_CLASH, m >> 2);
                w32(q, R_CLASH + 4, m & 3);
                gd::Int s = r32(q, R_STATE);
                w32(q, R_CLASH + 8, g->data->quick_decided[2 * s]);
                w32(q, R_CLASH + 12, g->data->quick_decided[2 * s + 1]);
            }
            costume_clash(ctx, rec(player) + R_CLASH, rec((player + 1) & 1) + R_CLASH);
            for (gd::Int q = 0; q < 2; ++q) {
                w32(q, R_MEMBERS, ((r32(q, R_CLASH) << 2) | (r32(q, R_CLASH + 4) & 3)) & 0xFFFFFFFFLL);
            }
            k = r32(player, R_MEMBERS + 4 * n);
        }
        w32(player, R_LAST, k);
        w32(player, R_COUNT, r32(player, R_COUNT) + 1);
        w32(player, R_AVAILABLE, r32(player, R_AVAILABLE) & ~gd::shl(1, (Fx::w32(k) >> 2) & 31) & 0xFFFFFFFFLL);
        g->sound(SOUND_PICK);
    }
    return r32(player, R_COUNT) < r32(player, R_WANTED) ? 1 : 0;
}

void QuickSelect::costume_clash(const gd::Ref<ByteBlock>& block, gd::Int a, gd::Int b) {
    if (block->u32(b + 8) != 0 && block->u32(a) == block->u32(b) && block->u32(a + 4) == block->u32(b + 4)) {
        gd::Int c = block->s32(a + 4);
        if (block->u32(b + 12) == 0 && block->u32(a + 12) != 0) {
            block->put32(b + 4, c < 2 ? c ^ 1 : 0);
        } else {
            gd::Int cb = block->s32(b + 4);
            block->put32(a + 4, cb < 2 ? cb ^ 1 : 0);
        }
    }
}

void QuickSelect::commit() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Ref<GameGlobals> gl = g->globals;
    for (gd::Int q = 0; q < 2; ++q) {
        gd::Int active = gl->player_active[q];
        if (region->mode != GameMode::PRACTICE || active != 0) {
            g->progress->set_last_character(q, ctx->u8(rec(q) + R_LAST));
        }
        gd::Int key = rs32(q, R_MEMBERS);
        gd::Int m = mode();
        switch (m) {
            case GameMode::PRACTICE:
                gl->player_char[q] = (key >> 2) & 0xFFFF;
                gl->player_costume[q] = key & 3;
                gl->player_cpu[q] = 0;
                gl->player_keep[q] = 1;
                break;
            case GameMode::TEAM: {
                gd::Int t = 13 * q;
                region->ctx_put8(t + 0x62, 0);
                region->ctx_put8(t + 0x63, ctx->u8(rec(q) + R_COUNT));
                region->ctx_put8(t + 0x65, ctx->u8(rec(q) + R_WANTED));
                region->ctx_put8(t + 0x64, ctx->u8(rec(q) + R_ORDER));
                for (gd::Int i = 0; i < 8; ++i) {
                    region->ctx_put8(t + 0x59 + i, ctx->u8(rec(q) + R_MEMBERS + 4 * i));
                }
                if (active != 0) {
                    gl->player_keep[q] = 1;
                    gl->player_cpu[q] = 0;
                } else {
                    gl->player_cpu[q] = 1;
                }
                break;
            }
            default:
                if (m < 9) {
                    if (m == GameMode::VS) {
                        region->ctx_put16(0x3A + 4 * q, r16(q, R_HANDICAP));
                    }
                    if (active != 0) {
                        gl->player_char[q] = (key >> 2) & 0xFFFF;
                        gl->player_costume[q] = key & 3;
                        gl->player_keep[q] = 1;
                        gl->player_cpu[q] = 0;
                    }
                }
                break;
        }
    }
}

}  // namespace t3
