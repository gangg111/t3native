// remake/core/game/result_screens.gd
// The result screens of result.ovl (modes.md#result-screens), written from the verified ports in
// `tools/research/screens_sim.py`: team battle (FUN_800EF4AC, game state 12), time attack
// (FUN_800EFF4C, state 13), survival (FUN_800F0A78, state 14) and Tekken Force (FUN_800F1F08,
// state 15, `force_result` in the same file). Their state is kept as the overlay's bytes
// (0x800FAC48 … 0x800FAE7C), as the flow traces record it.
#include "core/game/result_screens.h"

#include "content/fight_content.h"
#include "content/flow_data.h"
#include "content/game_ram.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_hud.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/game/mode_rules.h"
#include "core/game/opponents.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"

namespace t3 {

ResultScreens::ResultScreens(GameFlow* flow) : FlowPart(flow) {
    state = gd::make<ByteBlock>(SIZE);
    banner = gd::make<ByteBlock>(8);
}

gd::Int ResultScreens::brightness() {
    return state->s32(BRIGHT);
}

gd::Int ResultScreens::_get32(gd::Int at) {
    return state->s32(at);
}

void ResultScreens::_put(gd::Int at, gd::Int v) {
    state->put32(at, v);
}

gd::Int ResultScreens::_ctx8(gd::Int at) {
    return g->region->ctx8(at);
}

void ResultScreens::_fade_in(gd::Int next_sub) {
    gd::Int b = _get32(BRIGHT) + 8;
    if (b < 0x100) {
        _put(BRIGHT, b);
        return;
    }
    _put(BRIGHT, 0x100);
    g->sub = next_sub;
}

void ResultScreens::_fade_out(gd::Int next_sub) {
    gd::Int b = _get32(BRIGHT) - 4;
    if (b < 1) {
        b = 0;
        g->sub = next_sub;
    }
    _put(BRIGHT, b);
}

// FUN_800F18FC's state: the scrolling tiles and banner.
void ResultScreens::_banner_scroll() {
    banner->put32(0, (banner->u32(0) + 0x17F) % 0x180);
    banner->put32(4, (banner->u32(4) + 0x342) % 0x344);
}

// ---- the victory voice (FUN_80075A90, FUN_80075B1C, FUN_80075B4C)

void ResultScreens::_seq_reset() {
    seq_state = 0;
    seq_delay = SEQ_DELAY;
}

// ResultWinVoice: the winner's victory voice (none for Tiger), then the sequence goes on.
void ResultScreens::_result_sounds(gd::Int player) {
    if (RoundHud::win_voice(g->fight, player, g->sim->events)) {
        seq_state += 1;
    }
}

// After the voice (the SPU's voice 2 is never reported busy here) and 15 frames, sound 0x86DA.
void ResultScreens::_seq_tick() {
    switch (seq_state) {
        case 1:
            seq_state = 2;
            break;
        case 2:
            seq_state = 3;
            break;
        case 3:
            seq_delay = (seq_delay - 1) & 0xFF;
            if (seq_delay != 0 && seq_delay < 0x80) {
                return;
            }
            g->sound(SOUND_AFTER_VOICE);
            seq_state = 0;
            break;
        default:
            break;
    }
}

// ---- team battle (game state 12)

void ResultScreens::team_step() {
    if (g->menu_exit()) {
        return;
    }
    gd::Int sub = g->sub;
    if (sub > 1) {
        for (gd::Int p = 0; p < 2; ++p) {
            if ((g->pressed(p) & PadState::START) != 0) {
                g->globals->player_active[p] = 1;
                g->globals->set_tie_winner(g->fight, p);
                g->sub = 10;
            }
        }
        sub = g->sub;
    }
    gd::Int timer = g->sim->state_timer;
    switch (sub) {
        case 0:
            _team_setup();
            break;
        case 1:
            _fade_in(2);
            break;
        case 2:
            _team_line();
            break;
        case 3:
            timer += 1;
            g->sim->state_timer = timer;
            if (timer >= 2) {
                g->sound(SOUND_TICK);
                _put(T_LINE, 0);
                gd::Int fight = _get32(T_FIGHT) + 1;
                _put(T_FIGHT, fight);
                g->sub = _ctx8(ModeRules::TEAM_ROUND) <= fight ? 4 : 2;
            }
            break;
        case 4:
        case 5: {
            if (sub == 4) {
                _put(T_MSG, 1);
                _put(T_SLIDE, 0x1E);
                g->sub = 5;
            }
            gd::Int slide = _get32(T_SLIDE) - 1;
            if (slide < 1) {
                slide = 0;
                g->sound(SOUND_REVEAL);
                g->sub = 6;
            }
            _put(T_SLIDE, slide);
            break;
        }
        case 6:
        case 7:
            _team_wait(sub == 6);
            break;
        case 8:
            _fade_out(9);
            break;
        case 9:
            g->state = GameFlow::State::RANKING_LOAD;
            g->sub = 0;
            break;
        case 10:
            g->state = GameFlow::State::FIGHT;
            g->sub = 0;
            break;
        default:
            break;
    }
    _banner_scroll();
}

// Team battle's result: the members' outcomes and the teams' sizes.
void ResultScreens::_team_setup() {
    g->music(MUSIC_RESULT);
    g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 1);
    gd::Ref<ModeRegion> region = g->region;
    gd::Int a = 0;
    gd::Int b = 0;
    // `for k in _ctx8(...)`: GDScript evaluates the range once.
    gd::Int team_rounds = _ctx8(ModeRules::TEAM_ROUND);
    for (gd::Int k = 0; k < team_rounds; ++k) {
        _put(T_MEMBER1 + 4 * k, b);
        _put(T_MEMBER2 + 4 * k, a);
        gd::Int code = region->ctx16(ModeRules::TEAM_OUTCOMES + 2 * k);
        if (code == 1 || code == 3) {
            a += 1;
        }
        if (code == 2 || code == 3) {
            b += 1;
        }
    }
    for (gd::Int at : gd::Arr<gd::Int>{T_FIGHT, BRIGHT, T_LOST1, T_LOST2, T_LINE, T_MSG, T_SLIDE}) {
        _put(at, 0);
    }
    g->sub = 1;
    _put(T_LEFT1, _ctx8(ModeRules::TEAMS + 0xC));
    _put(T_LEFT2, _ctx8(ModeRules::TEAMS + ModeRules::TEAM_SIZE + 0xC));
}

// The joining line of the current fight grows; at its end the loser's member is crossed out.
void ResultScreens::_team_line() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int line = _get32(T_LINE) + 1;
    _put(T_LINE, line);
    if (line >= 9) {
        _put(T_LINE, 8);
        gd::Int code = region->ctx16(ModeRules::TEAM_OUTCOMES + 2 * _get32(T_FIGHT));
        if (code == 1 || code == 3) {
            _put(T_LOST2, _get32(T_LOST2) + 1);
            _put(T_LEFT2, _get32(T_LEFT2) - 1);
        }
        if (code == 2 || code == 3) {
            _put(T_LOST1, _get32(T_LOST1) + 1);
            _put(T_LEFT1, _get32(T_LEFT1) - 1);
        }
        g->sim->state_timer = 0;
        g->sub = 3;
    }
}

// The result holds: face buttons switch the message, Select shows it again, the wait ends it.
void ResultScreens::_team_wait(bool first) {
    gd::Int timer = g->sim->state_timer;
    if (first) {
        timer = 0;
        g->sim->state_timer = 0;
        g->sub = 7;
    }
    gd::Int pads = g->pressed_any();
    if (pads == PadState::SELECT) {
        g->sub = 0;
    } else {
        if ((pads & GameFlow::FACE_BUTTONS) != 0) {
            _put(T_MSG, (_get32(T_MSG) + 1) & 1);
        }
        timer += 1;
        g->sim->state_timer = timer;
        if (timer > TEAM_WAIT) {
            _put(T_MSG, 0);
            _put(BRIGHT, 0x100);
            g->sub = 8;
        }
    }
}

// ---- time attack (game state 13)

void ResultScreens::time_attack_step() {
    _put(A_FRAME, _get32(A_FRAME) + 1);
    gd::Int sub = g->sub;
    if ((g->pressed_any() & PadState::START) != 0 && ((sub - 2) & 0xFFFFFFFFLL) < 5) {
        sub = 7;
        g->sub = 7;
    }
    gd::Int timer = g->sim->state_timer;
    switch (sub) {
        case 0:
            g->attract_advance();
            g->music(MUSIC_RESULT);
            for (gd::Int at : gd::Arr<gd::Int>{A_FRAME, A_ROWS, A_ROW_ANIM, A_SHOW, BRIGHT, A_WAIT}) {
                _put(at, 0);
            }
            _put(A_TOTAL, _total_time());
            _put(A_RECORD, g->progress->name_entry & 1);
            g->sub = 1;
            _put(A_PLAYER, g->region->ctx32(0x38));
            _put(A_KEY, g->region->ctx32(0x3C));
            _seq_reset();
            break;
        case 1:
            _fade_in(2);
            break;
        case 2: {
            gd::Int anim = _get32(A_ROW_ANIM) + 1;
            _put(A_ROW_ANIM, anim);
            if (anim > 0x10) {
                g->sound(SOUND_TICK);
                g->sim->state_timer = 0;
                _put(A_ROW_ANIM, 0);
                g->sub = 3;
                _put(A_ROWS, _get32(A_ROWS) + 1);
            }
            break;
        }
        case 3:
            timer += 1;
            g->sim->state_timer = timer;
            if (timer >= 6) {
                g->sub = _get32(A_ROWS) > 9 ? 4 : 2;
            }
            break;
        case 4:
        case 5: {
            if (sub == 4) {
                _put(A_SHOW, 1);
                _put(A_WAIT, 0x1E);
                g->sub = 5;
            }
            gd::Int wait = _get32(A_WAIT) - 1;
            _put(A_WAIT, wait);
            if (wait < 1) {
                _put(A_WAIT, 0);
                g->sound(SOUND_TICK);
                _result_sounds(_get32(A_PLAYER));
                g->sub = 6;
            }
            break;
        }
        case 6:
        case 7: {
            bool stay = false;
            if (sub == 6) {
                if ((g->pressed(_get32(A_PLAYER)) & GameFlow::FACE_BUTTONS) != 0) {
                    _put(A_SHOW, (_get32(A_SHOW) + 1) & 1);
                }
                stay = _get32(A_FRAME) < RESULT_WAIT;
            }
            if (!stay) {
                _put(A_SHOW, 0);
                _put(BRIGHT, 0x100);
                g->sub = 8;
            }
            break;
        }
        case 8:
            _fade_out(9);
            break;
        case 9:
            g->state = GameFlow::State::RANKING_LOAD;
            g->sub = 0;
            break;
        default:
            break;
    }
    _seq_tick();
}

// FUN_800F28E8: the ten stage times of the run, capped.
gd::Int ResultScreens::_total_time() {
    gd::Int total = 0;
    for (gd::Int k = 0; k < 10; ++k) {
        total += g->region->ctx32(ModeRules::STAGE_RECORDS + 8 * k);
    }
    total = Fx::w32(total & 0xFFFFFFFFLL);
    return gd::mini(total, TIME_CAP);
}

// ---- survival (game state 14)

void ResultScreens::survival_step() {
    _put(S_FRAME, _get32(S_FRAME) + 1);
    gd::Int sub = g->sub;
    if ((g->pressed_any() & PadState::START) != 0 && ((sub - 2) & 0xFFFFFFFFLL) < 6) {
        sub = 8;
        g->sub = 8;
    }
    switch (sub) {
        case 0:
            _survival_setup();
            break;
        case 1: {
            gd::Int b = _get32(BRIGHT) + 8;
            _put(BRIGHT, b);
            if (b > 0xFF) {
                _put(BRIGHT, 0x100);
                g->sub = 2;
                _put(S_ROW, 0);
                _put(S_ANIM, 0);
                if (_get32(S_COUNT) < 1) {
                    g->sub = 4;
                    _put(S_ROW, _get32(S_COUNT));
                }
            }
            break;
        }
        case 2:
        case 3:
            _survival_count(sub == 2);
            break;
        case 4:
            g->sim->state_timer = 4;
            g->sub = 5;
            break;
        case 5:
        case 6: {
            if (sub == 5) {
                _put(S_FLAG, 2);
                _put(S_WAIT, 0x1E);
                g->sub = 6;
            }
            gd::Int wait = _get32(S_WAIT) - 1;
            _put(S_WAIT, wait);
            if (wait == 0) {
                g->sound(SOUND_REVEAL);
                if (state->u32(S_RANK) != 0) {
                    _result_sounds(_get32(S_PLAYER));
                }
                g->sub = 7;
            }
            break;
        }
        case 7:
        case 8:
            if (sub == 8 || _get32(S_FRAME) >= RESULT_WAIT) {
                // FUN_8006B9B0: the music fades out over 180 frames.
                g->music_volume(0, 0xB4);
                _put(S_FLAG, 0);
                _put(BRIGHT, 0x100);
                g->sub = 9;
            }
            break;
        case 9:
            _fade_out(10);
            break;
        case 10:
            g->state = GameFlow::State::RANKING_LOAD;
            g->sub = 0;
            break;
        case 11:
            g->state = GameFlow::State::FIGHT;
            g->sub = 0;
            break;
        default:
            break;
    }
    _seq_tick();
    _banner_scroll();
}

// Survival's result: the characters beaten, most wins first, and the rank.
void ResultScreens::_survival_setup() {
    gd::Ref<ModeRegion> region = g->region;
    g->attract_advance();
    g->music(MUSIC_RESULT);
    g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 1);
    _seq_reset();
    gd::Arr<gd::Vector2i> pairs;
    for (gd::Int c = 0; c < Opponents::CHARACTERS; ++c) {
        gd::Int wins = region->ctx16(Opponents::SURVIVAL_MET + 2 * c);
        if (wins != 0) {
            pairs.append(gd::Vector2i(c, wins));
        }
    }
    _put(S_COUNT, pairs.size());
    Opponents::sort_pairs(pairs, false);
    for (gd::Int i = 0; i < pairs.size(); ++i) {
        state->put8(S_CHARS + i, pairs[i].x);
    }
    for (gd::Int at : gd::Arr<gd::Int>{S_FRAME, BRIGHT, S_ROW, S_TOTAL}) {
        _put(at, 0);
    }
    g->sim->state_timer = 0;
    _put(S_FLAG, 2);
    _put(S_WAIT, 0x1E);
    g->sub = 1;
    _put(S_PLAYER, region->ctx32(0x38));
    _put(S_KEY, region->ctx32(0x3C));
    _put(S_RANK, region->ctx32(ModeRules::SURVIVAL_RANK));
}

// The rows count up one win every 4 frames, a tick each, row by row.
void ResultScreens::_survival_count(bool first) {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int timer = g->sim->state_timer;
    if (first) {
        _put(S_ANIM, 1);
        timer = 4;
        g->sub = 3;
        _put(S_TOTAL, _get32(S_TOTAL) + 1);
    }
    timer -= 1;
    g->sim->state_timer = timer;
    if (timer < 1) {
        g->sound(SOUND_TICK);
        gd::Int c = state->u8(S_CHARS + _get32(S_ROW));
        if (state->u32(S_ANIM) < region->ctx16(Opponents::SURVIVAL_MET + 2 * c)) {
            g->sim->state_timer = 4;
            _put(S_ANIM, state->u32(S_ANIM) + 1);
            _put(S_TOTAL, _get32(S_TOTAL) + 1);
        } else {
            gd::Int row = _get32(S_ROW) + 1;
            _put(S_ROW, row);
            g->sim->state_timer = 0;
            if (row < _get32(S_COUNT)) {
                g->sub = 2;
            } else {
                g->sub = 4;
                _put(S_ROW, _get32(S_COUNT));
            }
        }
    }
}

// ---- Tekken Force (game state 15)

// FUN_800F1F08: TEKKEN FORCE, the score and the high score, then the keys found (or the doctor
// saved, or the bosses beaten and the enemies counted); Start or 4,680 frames end it, the
// ranking follows. The lines' sounds are the drawing's.
void ResultScreens::force_step() {
    _put(F_FRAME, _get32(F_FRAME) + 1);
    gd::Ref<ModeRegion> region = g->region;
    gd::Int player = region->ctx8(FORCE_PLAYER);
    if ((g->pressed(player & 1) & PadState::START) != 0 && g->sub == 2) {
        g->sub = 3;
    }
    switch (g->sub) {
        case 0: {
            g->attract_advance();
            g->music(MUSIC_RESULT);
            gd::Int side = player & 1;
            _put(F_FRAME, 0);
            _put(BRIGHT, 0);
            gd::Ref<GameRam> ram = g->content->mode_ram("result");
            for (gd::Int i = 0; i < 4; ++i) {
                state->put16(F_TEX + 2 * i, ram->u16(F_TEX_SLOTS + 8 * side + 2 * i));
            }
            gd::Ref<GameGlobals> gl = g->globals;
            gd::Int c = gl->player_char[side];
            gd::Int k = gl->player_costume[side];
            if (c > 0x15 || k > 3) {
                c = 0x16;
                k = 0;
            }
            gd::Int word = g->data->attribute(c, k);
            _put(F_COLOUR, word >> 8);
            for (gd::Int at : gd::Arr<gd::Int>{F_SHOWN, F_BOSS, F_PHASE, F_FORCE}) {
                _put(at, 0);
            }
            g->sub = 1;
            state->put8(F_FLIP, ((word & 0xFF) >> 5) & 1);
            _seq_reset();
            break;
        }
        case 1:
            _fade_in(2);
            break;
        case 2:
            if (_get32(F_FRAME) > F_WAIT) {
                g->sub = 3;
            }
            break;
        case 3:
            g->music_volume(0, F_FADE_MUSIC);
            _put(BRIGHT, 0x100);
            g->sub = 4;
            break;
        case 4:
            _fade_out(5);
            break;
        case 5:
            g->state = GameFlow::State::RANKING_LOAD;
            g->sub = 0;
            break;
        default:
            break;
    }
    if (g->sub > 1) {
        _put(F_SHOWN, _get32(F_SHOWN) + 1);
    }
    _force_lines();
}

// The drawing's counters and sounds: each line's sound as it appears, the bosses one by one,
// then the FORCE count ticking up every other frame.
void ResultScreens::_force_lines() {
    gd::Ref<ModeRegion> region = g->region;
    gd::Int shown = _get32(F_SHOWN);
    if (shown == F_SCORE_AT || shown == F_HIGH_AT) {
        g->sound(SOUND_LINE);
    }
    gd::Int flags = region->ctx8(FORCE_FLAGS);
    if ((flags & FORCE_BOSSES) == 0) {
        if ((flags & FORCE_DOCTOR) != 0) {
            if (shown == F_LIST_AT) {
                g->sound(SOUND_REVEAL);
            }
            return;
        }
        for (gd::Int i = 0; i < 3; ++i) {
            if ((flags & gd::shl(1, i)) != 0 && shown == F_KEY_AT[i]) {
                g->sound(SOUND_LINE);
            }
        }
        return;
    }
    if (shown > F_LIST_AT - 1) {
        _put(F_BOSS, _get32(F_BOSS) + 1);
        if (shown == F_LIST_AT) {
            g->sound(SOUND_LINE);
        }
    }
    gd::Int boss = _get32(F_BOSS);
    for (gd::Int at : F_BOSS_AT) {
        if (boss == at) {
            g->sound(SOUND_LINE);
        }
    }
    switch (_get32(F_PHASE)) {
        case 1:
            if ((shown & 1) == 0) {
                _put(F_FORCE, _get32(F_FORCE) + 1);
                g->sound(SOUND_TICK);
            }
            if (region->ctx16(FORCE_COUNT) < _get32(F_FORCE) || g->sub > 2) {
                _put(F_PHASE, 2);
            }
            break;
        case 0:
            if (boss > F_COUNT_AFTER) {
                _put(F_PHASE, 1);
                _put(F_FORCE, 0);
            }
            break;
        default:
            break;
    }
}

}  // namespace t3
