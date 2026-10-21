// remake/core/fight/vibration.gd
// Pad vibration requests (PadVibrate patterns, SimEvents VIBRATE a = pad (2 both), b = pattern):
// HitVibration (0x80075CBC) for the frame's hits, FighterVibrate (0x800760D4) for the move and
// effect events and VibrateAll (0x80076078) for camera shakes. Nothing vibrates during a replay.
#include "core/fight/vibration.h"

#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_flag.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/sim_events.h"

namespace t3 {

bool Vibration::_allowed(const gd::Ref<FightState>& fight) {
    return fight->replay_playing == 0
        && (fight->mode != GameMode::BALL || fight->region->ctx_s32(TekkenBall::LAST_POINT) == 0);
}

// FUN_800761D8: the pad of a fighter (Tekken Force: the human's pad for the first record, the
// other pad for the enemies).
gd::Int Vibration::_pad(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f) {
    if (fight->mode == GameMode::FORCE) {
        gd::Int human = fight->force->human();
        return f->index == 0 ? human : (human + 1) & 1;
    }
    return f->player_index;
}

// HitVibration: the outcome of every fighter's last applied hit on its pad, then the attacker's
// and thrower's pads.
void Vibration::hits(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events) {
    if (!_allowed(fight)) {
        return;
    }
    gd::PI32 flagged{0, 0, 0};   // by attacker record (the third: the ball, a Force enemy)
    for (const auto& f : fight->active()) {
        gd::Int pad = _pad(fight, f);
        gd::Int attacker = f->last_attacker;
        if (f->guarded != 0) {
            events->add(SimEvents::Kind::VIBRATE, f->index, pad, GUARDED);
            flagged[attacker] |= 1;
        }
        // The KO flag goes to the pad of the hit's attacker (or the thrower). The game starts the
        // index at −1, so a KO without either this frame (guard damage) flags a stack word and no
        // pad gets pattern 0x0B (game-bugs.md #58, not reproduced): here the last attacker's does.
        gd::Int ko_pad = attacker;
        if (f->hit_clean != 0) {
            if (f->last_damage < HEAVY_DAMAGE) {
                events->add(SimEvents::Kind::VIBRATE, f->index, pad, HIT_LIGHT);
                flagged[attacker] |= 2;
            } else {
                events->add(SimEvents::Kind::VIBRATE, f->index, pad, HIT_HEAVY);
                flagged[attacker] |= 4;
            }
            if (f->close_hit != 0) {
                events->add(SimEvents::Kind::VIBRATE, f->index, pad, HIT_CLOSE);
                flagged[attacker] |= 8;
            }
            if (f->counter_hit != 0) {
                events->add(SimEvents::Kind::VIBRATE, f->index, pad, HIT_COUNTER);
                flagged[attacker] |= 0x10;
            }
        }
        if (f->last_extra_damage > 0) {
            events->add(SimEvents::Kind::VIBRATE, f->index, pad, EXTRA_DAMAGE);
            ko_pad = f->throw_partner;
            flagged[ko_pad] |= 0x20;
        }
        if (f->landed_b != 0) {
            events->add(SimEvents::Kind::VIBRATE, f->index, pad, LANDED);
        }
        if (f->ko != 0) {
            events->add(SimEvents::Kind::VIBRATE, f->index, pad, KNOCKED_OUT);
            flagged[ko_pad] |= 0x80;
        }
        if ((f->pose_move->flags & MoveFlag::POWER) != 0 && f->move_changed != 0) {
            events->add(SimEvents::Kind::VIBRATE, f->index, pad, POWER_MOVE);
            flagged[(f->player_index + 1) & 1] |= 0x40;
        }
    }
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int pad = _pad(fight, fight->fighters[p]);
        for (const auto& entry : FLAGGED) {
            gd::Int bit = entry[0];
            if ((flagged[p] & bit) != 0) {
                events->add(SimEvents::Kind::VIBRATE, p, pad, entry[1]);
            }
        }
    }
}

// FighterVibrate: kind 0 burning, 1 move effect, 2 ground impact, 3 fire breath, 4 Gon's flame,
// 5 unused here.
void Vibration::fighter(const gd::Ref<FightState>& fight, gd::Int index, gd::Int kind, const gd::Ref<SimEvents>& events) {
    if (!_allowed(fight)) {
        return;
    }
    gd::Int pad = _pad(fight, fight->fighters[index]);
    gd::Arr<gd::Int> patterns = FIGHTER_KINDS[kind];
    gd::Int own = patterns[0];
    gd::Int other = patterns[1];
    events->add(SimEvents::Kind::VIBRATE, index, pad, own);
    if (other >= 0) {
        events->add(SimEvents::Kind::VIBRATE, index, (pad + 1) & 1, other);
    }
}

// VibrateAll: a camera shake's pulse on both pads.
void Vibration::all(const gd::Ref<FightState>& fight, gd::Int script, const gd::Ref<SimEvents>& events) {
    if (_allowed(fight)) {
        events->add(SimEvents::Kind::VIBRATE, -1, BOTH_PADS, script + 1);
    }
}

}  // namespace t3
