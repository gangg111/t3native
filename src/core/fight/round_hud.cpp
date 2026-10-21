// remake/core/fight/round_hud.gd
// HudRoundUpdate (0x8003D904), the round part: the announcer's voices (SimEvents, run by the
// simulation) and the round messages (read by the presentation each frame). The bars, timer and
// round marks the same routine draws are plain FightState fields. See hud.md.
#include "core/fight/round_hud.h"

#include <cstdio>

#include "content/fight_tables.h"
#include "core/ball/tekken_ball.h"
#include "core/effects/effect_objects.h"
#include "core/fight/fight_state.h"
#include "core/fight/round_state.h"
#include "core/fighter/fighter_state.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "gd/json.h"

namespace t3 {

namespace {

// GDScript's "%x" (lower-case hex) for the round and stage numbers.
std::string hex_str(gd::Int v) {
    char buf[32];
    if (v < 0) {
        std::snprintf(buf, sizeof(buf), "-%llx", static_cast<unsigned long long>(-v));
    } else {
        std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
    }
    return buf;
}

}  // namespace

RoundHud::Message::Message(const std::string& message_text, gd::Int message_palette, gd::Int message_font, gd::Int at_x,
                           gd::Int at_y) {
    text = message_text;
    palette = message_palette;
    font = message_font;
    x = at_x;
    y = at_y;
}

// The voices of the frame. FightFrame calls it after the round flow, before the camera, unless
// a replay plays (also on paused frames).
void RoundHud::update(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    // The demonstration fight (attract flag) and a challenger's entrance have no announcements.
    if (fight->attract == 0 && fight->challenger == 0) {
        switch (fight->round_state) {
            case RoundState::INTRO:
                if (fight->round_frame != 0) {
                    _round_start_voices(fight, events);
                }
                break;
            case RoundState::RESULT:
                if ((fight->mode != GameMode::FORCE || (fight->result_flags & TIME_UP) != 0) && fight->pause_request == 0) {
                    _result_voice(fight, events);
                }
                break;
            case RoundState::WIN_WAIT:
            case RoundState::END:
                if (fight->mode != GameMode::FORCE && fight->mode != GameMode::PRACTICE
                        && (fight->match_result != 0 || fight->replay_skip == 0 || fight->pause_request != 0)) {
                    _winner_voice(fight, events);
                }
                break;
            default:
                break;
        }
    }
    if (fight->hud_shown != 0) {
        if (fight->mode == GameMode::FORCE) {
            fight->force->bars_step();
        } else {
            _bars(fight);
        }
    }
}

// FUN_8004E520: the bars start empty at a round start and refill.
void RoundHud::reset_bars(const gd::Ref<FightState>& fight) {
    for (gd::Int p = 0; p < 2; ++p) {
        fight->hud_bars[p] = gd::PI32{0, 0, 0, 0};
    }
}

// FUN_8004E55C: the health bars' lengths. A drop shows at once, a refill grows 3 pixels per
// frame, and the recent-damage part closes 1/16 of its length (rounded up) per frame.
void RoundHud::_bars(const gd::Ref<FightState>& fight) {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Ref<FighterState> f = fight->fighters[p];
        gd::PI32 bar = fight->hud_bars[p];
        gd::Int target = bar[1];
        gd::Int step = Fx::div_trunc(f->health_max, BAR_LENGTH);
        if (bar[0] != f->health) {
            bar[0] = f->health;
            target = Fx::div_trunc(f->health - 1 + step, step);
        }
        target = gd::mini(target, BAR_LENGTH);
        gd::Int drawn = bar[2];
        gd::Int recent = drawn;
        if (drawn < bar[3]) {
            recent = bar[3] - ((bar[3] - drawn + 0xF) >> 4);
        }
        gd::Int grown = target;
        if (target > drawn && drawn + BAR_GROWTH <= target) {
            grown = drawn + BAR_GROWTH;
        }
        recent = gd::maxi(recent, grown);
        fight->hud_bars[p] = gd::PI32{bar[0], Fx::s16(target), Fx::s16(grown), Fx::s16(recent)};
    }
}

// The messages shown this frame.
gd::Arr<gd::Ref<RoundHud::Message>> RoundHud::messages(const gd::Ref<FightState>& fight) {
    gd::Arr<gd::Ref<Message>> out;
    if (fight->mode == GameMode::DEMO) {                // the demonstration fight shows GAME OVER
        out.append(gd::make<Message>("GAME OVER", 2, 1, 0x7E, 0x104));
    }
    if (fight->attract != 0 || fight->challenger != 0) {
        return out;
    }
    switch (fight->round_state) {
        case RoundState::INTRO:
            if (fight->round_frame != 0) {
                _round_start_messages(fight, out);
            }
            break;
        case RoundState::RESULT:
            if (fight->mode != GameMode::FORCE || (fight->result_flags & TIME_UP) != 0) {
                _result_message(fight, out);
            }
            break;
        case RoundState::WIN_WAIT:
        case RoundState::END:
            if (fight->mode != GameMode::FORCE && fight->mode != GameMode::PRACTICE) {
                _winner_messages(fight, out);
            }
            break;
        case RoundState::STAGE_CLEAR:
            out.append(gd::make<Message>("STAGE CLEAR!!", 8, 2, 0x29, 0x70));
            break;
        default:
            break;
    }
    return out;
}

// ---- voices

// FUN_8003DCE0: the intro voice, "ROUND n" and "FIGHT".
void RoundHud::_round_start_voices(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    bool first = fight->rounds_played == 1;
    bool intro = fight->round_frame == (first ? FIRST_ROUND_INTRO : 1);
    switch (gd::Int(fight->mode)) {
        case GameMode::TEAM:
        case GameMode::SURVIVAL:
            // The USA version says nothing here (usa-version.md); the remake keeps Japan Rev.1's
            // behaviour in both text regions.
            if (intro) {
                _announce(VOICE_INTRO, events);
            }
            break;
        case GameMode::PRACTICE:
            if (intro) {
                win_voice(fight, (fight->human_mask & 1) == 0 ? 1 : 0, events);
            }
            break;
        case GameMode::FORCE:
            if (intro) {
                win_voice(fight, fight->force->player()->player_index, events);
            }
            break;
        default: {
            bool last = fight->max_rounds <= fight->rounds_played;
            if (intro) {
                _announce(last ? VOICE_INTRO_LAST : VOICE_INTRO, events);
            }
            if (fight->round_frame == (first ? FIRST_ROUND_NUMBER : LATER_ROUND_NUMBER) && !last) {
                _announce(fight->tables->round_voices[fight->rounds_played], events);
            }
            break;
        }
    }
    if (fight->round_frame == (first ? FIRST_ROUND_FIGHT : LATER_ROUND_FIGHT)) {
        _announce(VOICE_FIGHT, events);
    }
}

// ResultWinVoice (0x80075A90): the fighter's victory voice (none for Tiger); true when played.
bool RoundHud::win_voice(const gd::Ref<FightState>& fight, gd::Int index, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> f = fight->fighters[index];
    if (f->costume_key == TIGER_KEY) {
        return false;
    }
    events->add(SimEvents::Kind::SOUND, -1, f->voice_set == VOICE_SET_15 ? VOICE_WIN_BOSS : VOICE_WIN, f->player_index);
    return true;
}

// FUN_8003E158: the result's sound on the first frame of round state 2.
void RoundHud::_result_voice(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    if (fight->round_counter != 1) {
        return;
    }
    gd::PI32 voices = fight->tables->result_voices;
    gd::Int code = voices[0];
    gd::Int flags = fight->result_flags;
    if ((flags & TIME_UP) == 0) {
        code = voices[6];
        if ((flags & PERFECT) == 0) {
            code = voices[2];
            if ((flags & DOUBLE_KO) == 0) {
                code = voices[7];
                if ((flags & GREAT) != 0) {
                    code = voices[5];
                }
            }
        }
    }
    events->add(SimEvents::Kind::SOUND, 0, code, 0);
}

// FUN_8003E2F8: the winner's announcement on the first frame of the result display; the
// music fades out when the match is decided.
void RoundHud::_winner_voice(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    if (fight->round_counter != 1) {
        return;
    }
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    bool fade = fight->match_result != 0;
    if (f0->round_won == f1->round_won) {
        _announce(VOICE_DRAW, events);
    } else {
        gd::Int winner = f1->round_won < f0->round_won ? 0 : 1;
        if (fight->human_mask == 3 || fight->mode == GameMode::TEAM) {
            if (win_voice(fight, winner, events)) {
                EffectObjects::announcer_follow_up(fight);
            } else {
                _announce(VOICE_WINS, events);
            }
        } else if ((gd::shr(fight->human_mask, winner) & 1) == 0) {
            _announce(VOICE_LOSES, events);
            if (fight->team_round == 0) {
                fade = false;
            }
        } else {
            _announce(VOICE_WINS, events);
        }
    }
    if (fade) {
        // FUN_8006B9B0 also sets the music state 0x800A0980 to 3 (fading out).
        events->add(SimEvents::Kind::MUSIC_VOLUME, -1, 0, MUSIC_FADE_FRAMES);
    }
}

// FUN_80040E98: an announcer sound (SoundPlaySystem of the code's low 12 bits).
void RoundHud::_announce(gd::Int code, const gd::Ref<SimEvents>& events) {
    events->add(SimEvents::Kind::SYSTEM_SOUND, -1, code & 0xFFF);
}

// ---- messages

// FUN_8003DF60: the round title and "READY?".
void RoundHud::_round_start_messages(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out) {
    bool first = fight->rounds_played == 1;
    switch (gd::Int(fight->mode)) {
        case GameMode::TEAM:
            out.append(gd::make<Message>("TEAM BATTLE", 8, 2, 0x3F, 0x70));
            break;
        case GameMode::SURVIVAL:
            out.append(gd::make<Message>("SURVIVAL BATTLE", 8, 2, 0x13, 0x70));
            break;
        case GameMode::PRACTICE:
            out.append(gd::make<Message>("PRACTICE MODE", 8, 2, 0x29, 0x70));
            break;
        case GameMode::FORCE:
            if (fight->region->fight_index > 3) {
                out.append(gd::make<Message>("FINAL STAGE", 8, 2, 0x3F, 0x70));
            } else {
                out.append(gd::make<Message>("STAGE " + hex_str(fight->region->fight_index + 1), 8, 2, 0x6B, 0x70));
            }
            break;
        default:
            if (fight->max_rounds <= fight->rounds_played) {
                out.append(gd::make<Message>("FINAL ROUND", 8, 2, 0x3F, 0x70));
            } else if (fight->round_frame >= (first ? FIRST_ROUND_TITLE : 1)) {
                out.append(gd::make<Message>("ROUND " + hex_str(fight->rounds_played), 8, 2, 0x6B, 0x70));
            }
            break;
    }
    if (fight->round_frame >= (first ? FIRST_ROUND_READY : LATER_ROUND_READY)) {
        out.append(gd::make<Message>("READY?", 9, 2, 0x76, 0xE4));
    }
}

// FUN_8003E200: the round result.
void RoundHud::_result_message(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out) {
    if (fight->replay_request != 0) {
        return;
    }
    gd::Ref<ModeRegion> region = fight->region;
    if (fight->mode == GameMode::BALL && region->ctx_s32(TekkenBall::LAST_POINT) < region->ctx_s32(TekkenBall::RESULT) - 1) {
        // Tekken Ball shows the result with its last replay.
        return;
    }
    gd::Int flags = fight->result_flags;
    if ((flags & TIME_UP) != 0) {
        out.append(gd::make<Message>("TIME UP", 3, 2, 0x6B, 0xE4));
    } else if ((flags & PERFECT) != 0) {
        out.append(gd::make<Message>("PERFECT!", 2, 2, 0x60, 0xE4));
    } else if ((flags & DOUBLE_KO) != 0) {
        out.append(gd::make<Message>("DOUBLE K.O.", 1, 2, 0x3F, 0xE4));
    } else {
        out.append(gd::make<Message>("K.O.", 1, 2, 0x8C, 0xE4));
    }
}

// FUN_8003E408: the winner of the round.
void RoundHud::_winner_messages(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out) {
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    if (f0->round_won == f1->round_won) {
        out.append(gd::make<Message>("DRAW", 6, 2, 0x8C, 0x170));
        return;
    }
    gd::Int winner = f0->round_won <= f1->round_won ? 1 : 0;
    if (fight->human_mask == 3 || fight->mode == GameMode::TEAM) {
        gd::Ref<FighterState> f = fight->fighters[winner];
        std::string name = fight->tables->character(f->costume_key)["name"].str();
        gd::Int x = Fx::s16(0xB8 - (static_cast<gd::Int>(name.size()) * 0xB + 0x42));
        out.append(gd::make<Message>(name + " WINS!", 4, 2, x, 0x170));
        if (fight->match_result != 0 && fight->mode == GameMode::ARCADE) {
            out.append(gd::make<Message>("WINNER", 9, 1, winner * 0xC4 + 0x2F, 0x68));
            out.append(gd::make<Message>("LOSER", 9, 1, (1 - winner) * 0xC3 + 0x36, 0x68));
        }
    } else if ((gd::shr(fight->human_mask, winner) & 1) == 0) {
        out.append(gd::make<Message>("YOU LOSE", 5, 2, 0x60, 0x170));
    } else {
        out.append(gd::make<Message>("YOU WIN!", 4, 2, 0x60, 0x170));
    }
}

}  // namespace t3
