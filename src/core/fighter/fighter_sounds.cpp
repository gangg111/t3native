// remake/core/fighter/fighter_sounds.gd
// FighterSounds (0x80041138, sound.md#fighter-sound-logic): the fighter's voices and sounds of
// the frame, the sound scripts, and the voice cooldowns. The simulation only decides what plays
// (SimEvents SOUND / SOUND_STOP / SYSTEM_SOUND with the game's 16-bit sound codes); every sound
// is also written to the replay. `rand()` is drawn exactly where the game draws it.
#include "core/fighter/fighter_sounds.h"

#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/camera/camera_phase.h"
#include "core/combat/attack_records.h"
#include "core/effects/effect_objects.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_state.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

void FighterSounds::run(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                        const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events) {
    gd::Ref<FightTables> t = fight->tables;
    gd::Int p = f->player_index;
    gd::Int q = opp->player_index;
    (void)q;   // unused in the GDScript too
    gd::Ref<FightTables::VoiceSet> voices = t->voices[f->voice_set];
    fight->sound_hold[p] = 0;
    // FUN_8004B9B8: a clean hit, or the first frame of a throw victim's move, keys off voice 0.
    if (f->hit_clean != 0 || (f->throw_state < 0 && f->move_frame == 1)) {
        events->add(SimEvents::Kind::VOICE_OFF, f->index, 0);
    }
    bool voiced = false;
    bool impacted = false;
    if (f->ko != 0) {
        gd::Int id = (((fight->rng->next() & 0xFF) * voices->counts[2]) >> 8) | KO_VOICE;
        _voice(fight, p, f->voice_set, id, events);
        voiced = true;
    }
    if (opp->health == 0 && opp->hit_clean != 0) {
        gd::Int code = AttackRecords::first_joint(f->pose_move, t) < HEAVY_JOINT ? KO_IMPACT_LIGHT : KO_IMPACT_HEAVY;
        _sound(fight, p, code, events);
        impacted = true;
    }
    gd::Ref<MoveRow> move = f->pose_move;
    if (move->sounds >= 0) {
        gd::PI32 list = move->bank->sounds;
        for (gd::Int k = 0; k < 3; ++k) {
            gd::Int code = list[move->sounds + k];
            if (code == 0xFFFF) {
                break;
            }
            switch (code >> 12) {
                case 1:
                case 7:
                    if (f->contact != 0 && !impacted) {
                        _impact(fight, f, opp, code, events);
                    }
                    break;
                case 2:
                    if (f->pose_frame == 1 && f->health != 0 && fight->voice_cooldown[p] == 0 && !voiced) {
                        _voice(fight, p, f->voice_set, code, events);
                        fight->voice_cooldown[p] = voices->cooldown;
                    }
                    break;
                case 3:
                    if (f->pose_frame == 1 && f->health != 0 && fight->voice_cooldown[p] == 0 && !voiced) {
                        _voice(fight, p, f->voice_set, code, events);
                        voiced = true;
                    }
                    break;
                case 4:
                case 5:
                    _sound(fight, p, code, events);
                    break;
                case 8: {
                    gd::Int script = code & 0xFFF;
                    if (script != 0 && f->sound_script_last != script) {
                        f->sound_script = script;
                        f->sound_script_last = script;
                        f->sound_script_pos = 0;
                        f->sound_script_frame = Fx::s16(f->pose_frame - 1);
                    }
                    break;
                }
                case 9:
                    _special(fight, f, opp, code & 0xFFF, events);
                    break;
                default:
                    break;
            }
        }
    }
    if (f->got_hit != 0) {
        script_stop(f);
    }
    if (f->sound_script != -1 && gd::Arr<gd::Int>{CameraPhase::RESET, CameraPhase::INTRO, CameraPhase::FIGHT,
                                                  CameraPhase::WINNER}.has(fight->camera_phase)) {
        _run_script(fight, f, opp, events);
    } else {
        _ambient(fight, f, opp, voices, voiced, events);
    }
    if (fight->voice_cooldown[p] > 0) {
        fight->voice_cooldown[p] -= 1;
    }
    if (fight->damage_voice_timer[p] > 0) {
        fight->damage_voice_timer[p] -= 1;
    }
}

void FighterSounds::script_stop(const gd::Ref<FighterState>& f) {
    f->sound_script = -1;
    f->sound_script_last = 0;
    f->sound_script_pos = 0;
    f->sound_script_frame = 0;
}

void FighterSounds::_ambient(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                             const gd::Ref<FighterState>& opp, const gd::Ref<FightTables::VoiceSet>& voices,
                             bool voiced, const gd::Ref<SimEvents>& events) {
    gd::Int p = f->player_index;
    if (!voiced && f->health != 0 && f->hit_clean != 0 && fight->damage_voice_timer[p] == 0
            && (gd::Int(fight->frame_counter) & 1) != 0) {
        gd::Int n = voices->damage_counts[0];
        gd::Int first = 0;
        if (opp->damage >= HEAVY_DAMAGE) {
            n = voices->damage_counts[1];
            first = voices->damage_counts[0];
        }
        voiced = true;
        if (n != 0) {
            gd::Int id = voices->damage[first + (((fight->rng->next() & 0xFF) * n) >> 8)];
            _voice(fight, p, f->voice_set, id, events);
            fight->damage_voice_timer[p] = DAMAGE_VOICE_HOLD;
        }
    }
    if (fight->voice_cooldown[p] == 0 && !voiced) {
        if (f->damage != 0 && f->throw_state == 0 && f->move_changed != 0 && (fight->rng->next() & 3) == 0) {
            gd::Int id = voices->shouts[(voices->shouts.size() * (fight->rng->next() & 0xFF)) >> 8];
            _voice(fight, p, f->voice_set, id, events);
            fight->voice_cooldown[p] = voices->cooldown;
        }
    }
    if (fight->sound_hold[p] == 0) {
        if (f->pose_frame == f->pose_move->air_first && f->pose_frame != 1) {
            _sound(fight, p, JUMP, events);
        }
        if (f->landed_a != 0 && fight->sound_hold[p] == 0) {
            _sound(fight, p, LAND_A, events);
        }
        if (f->landed_b != 0 && fight->sound_hold[p] == 0) {
            _sound(fight, p, LAND_B, events);
        }
    }
    if (f->pose_frame == f->pose_move->active_first - 1) {
        _sound(fight, p, SWING, events);
    }
}

void FighterSounds::_run_script(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                                const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events) {
    gd::PI32 script = fight->tables->sound_scripts[f->sound_script];
    gd::Int p = f->player_index;
    gd::Int q = opp->player_index;
    f->sound_script_frame = Fx::s16(f->sound_script_frame + 1);
    while (true) {
        gd::Int entry = script[f->sound_script_pos];
        gd::Int frame = entry & 0xFFF;
        if (frame == 0) {
            break;
        }
        if (f->sound_script_frame < frame) {
            return;
        }
        if (f->sound_script_frame == frame) {
            gd::Int code = (entry >> 16) & 0xFFFF;
            switch ((entry >> 12) & 0xF) {
                case 0:
                    fight->replay->record_sound(REPLAY_SYSTEM, 0, Fx::s16(code));
                    events->add(SimEvents::Kind::SYSTEM_SOUND, f->index, code & 0xFFF);
                    break;
                case 1:
                    if (_is_voice(code)) {
                        _voice(fight, p, f->voice_set, code, events);
                    } else {
                        _sound(fight, p, code, events);
                    }
                    break;
                case 2:
                    if (_is_voice(code)) {
                        _voice(fight, q, opp->voice_set, code, events);
                    } else {
                        _sound(fight, q, code, events);
                    }
                    break;
                case 3:
                    _special(fight, f, opp, code & 0xFFF, events);
                    break;
                default:
                    break;
            }
        }
        f->sound_script_pos += 1;
    }
    script_stop(f);
}

void FighterSounds::_impact(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                            const gd::Ref<FighterState>& opp, gd::Int code, const gd::Ref<SimEvents>& events) {
    gd::Ref<FightTables> t = fight->tables;
    gd::Int q = opp->player_index;
    gd::Int joint = AttackRecords::first_joint(f->pose_move, t);
    bool wooden = opp->char_id == Character::MOKUJIN && fight->wooden_sounds[q] != 0;
    if (opp->guarded == 0) {
        script_stop(opp);
        if (opp->char_id == Character::GUN_JACK) {
            _gun_jack_hit(fight, f, opp, events);
        } else if (wooden) {
            gd::PI32 impacts = t->impact_voices;
            _sound(fight, q, impacts[((fight->rng->next() & 0xFF) * impacts.size()) >> 8], events);
        } else if (STRIKE_CODES.has(code)) {
            _strike(fight, f, events);
        } else {
            _sound(fight, f->player_index, code, events);
        }
    } else if (wooden) {
        _sound(fight, q, joint < GUARD_JOINT ? WOOD_GUARD_LIGHT : WOOD_GUARD_HEAVY, events);
    } else {
        _sound(fight, q, joint < GUARD_JOINT ? GUARD_LIGHT : GUARD_HEAVY, events);
    }
}

void FighterSounds::_gun_jack_hit(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                                  const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events) {
    gd::Int code = GUN_JACK_LIGHT;
    if (f->damage >= 7 && opp->damage >= 0x10) {
        code = (fight->vblank & 3) == 0 ? GUN_JACK_HEAVY_A : GUN_JACK_HEAVY_B;
    }
    _sound(fight, opp->player_index, code, events);
}

void FighterSounds::_strike(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                            const gd::Ref<SimEvents>& events) {
    gd::Int code = 0;
    if (AttackRecords::first_joint(f->pose_move, fight->tables) < GUARD_JOINT) {
        gd::Int r = fight->rng->next();
        if (f->damage < 0xD) {
            code = fight->tables->strike_sounds[(r >> 7) & 1];
        } else if (f->damage < 0x17) {
            code = 0x7042;
        } else if (f->damage < 0x21) {
            code = 0x7043;
        } else {
            code = 0x7044;
        }
    } else if (f->damage < 0xD) {
        code = 0x7045;
    } else if (f->damage < 0x17) {
        code = 0x7046;
    } else if (f->damage < 0x21) {
        code = 0x7047;
    } else {
        code = 0x7048;
    }
    _sound(fight, f->player_index, code, events);
}

void FighterSounds::_special(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                             const gd::Ref<FighterState>& opp, gd::Int n, const gd::Ref<SimEvents>& events) {
    gd::Int p = f->player_index;
    switch (n) {
        case 0:
            fight->sound_hold[p] = 1;
            break;
        case 1:
            if (opp->sound_script != -1) {
                _voice(fight, p, f->voice_set, fight->tables->voices[f->voice_set]->ids[2], events);
                script_stop(opp);
            }
            break;
        case 2:
            fight->voice_cooldown[p] = 1;
            break;
        case 3: {
            gd::PI32 sounds = fight->tables->special_sounds;
            _sound(fight, opp->player_index, sounds[((fight->rng->next() & 0xFF) * sounds.size()) >> 8], events);
            break;
        }
        default:
            break;
    }
}

void FighterSounds::replay_voice(const gd::Ref<FightState>& fight, gd::Int player, gd::Int voice_set, gd::Int code,
                                 const gd::Ref<SimEvents>& events) {
    _voice(fight, player, voice_set, code, events);
}

void FighterSounds::replay_sound(const gd::Ref<FightState>& fight, gd::Int player, gd::Int code,
                                 const gd::Ref<SimEvents>& events) {
    _sound(fight, player, code, events);
}

bool FighterSounds::_is_voice(gd::Int code) {
    gd::Int c = (code >> 12) - VOICE_FIRST;
    return c >= 0 && c < VOICE_CATEGORIES;
}

void FighterSounds::_voice(const gd::Ref<FightState>& fight, gd::Int player, gd::Int voice_set, gd::Int id,
                           const gd::Ref<SimEvents>& events) {
    id = Fx::s16(id);
    gd::Int nibble = id >> 12;
    if (!(fight->fighters[player]->in_throw != 0 && nibble == THROW_VOICE)) {
        fight->replay->record_sound(REPLAY_VOICE, player, id);
    }
    gd::Int c = ((id & 0xFFFF) >> 12) - VOICE_FIRST;
    if (c < 0 || c >= VOICE_CATEGORIES) {
        return;
    }
    gd::Ref<FightTables::VoiceSet> voices = fight->tables->voices[voice_set];
    gd::Int index = id & 0xFFF;
    if (index > voices->counts[c] - 1) {
        return;
    }
    for (gd::Int k = 0; k < c; ++k) {
        index += voices->counts[k];
    }
    gd::Int code = voices->voices[index];
    events->add(SimEvents::Kind::SOUND, player, code, player);
    if (c + VOICE_FIRST == SHOUT_CATEGORY) {
        events->add(SimEvents::Kind::SHOUT, player, id, player);
    }
    if (c + VOICE_FIRST == THROW_VOICE) {
        EffectObjects::voice_echo(fight, player, code, false);
        events->add(SimEvents::Kind::VOICE_OFF, player, 0);
    } else if (fight->replay_playing != 0) {
        EffectObjects::voice_echo(fight, player, code, true);
    }
}

void FighterSounds::_sound(const gd::Ref<FightState>& fight, gd::Int player, gd::Int id,
                           const gd::Ref<SimEvents>& events) {
    id = Fx::s16(id);
    fight->replay->record_sound(REPLAY_SOUND, player, id);
    gd::Int index = id & 0xFFF;
    if (index == 0) {
        return;
    }
    gd::Int code = fight->tables->sound_codes[index];
    if (id >> 12 == 0) {
        events->add(SimEvents::Kind::SOUND_STOP, player, code, player);
    } else {
        events->add(SimEvents::Kind::SOUND, player, code, player);
        if (fight->replay_playing != 0) {
            EffectObjects::voice_echo(fight, player, code, true);
        }
    }
}

}  // namespace t3
