// remake/core/fighter/move_events.gd
// MoveEvents (0x80045B60, moves.md#frame-events): fires the running move's frame events whose
// frame the pose passed this frame (`eventFrame < frame ≤ poseFrame`).
#include "core/fighter/move_events.h"

#include "content/fighter_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

void MoveEvents::run(const gd::Ref<FighterState>& f, const gd::Ref<FighterTables>& tables,
                     const gd::Ref<SimEvents>& out) {
    f->extra_kind = 0;
    gd::Ref<MoveRow> move = f->pose_move;
    if (f->active == 0 || move == nullptr || move->events < 0) {
        return;
    }
    gd::PI32 events = move->bank->events;
    gd::Int i = move->events;
    while (events[i] != 0) {
        gd::Int frame = events[i];
        gd::Int command = events[i + 1];
        i += 2;
        if (frame > f->pose_frame || frame <= f->event_frame) {
            continue;
        }
        gd::Int hi = command >> 8;
        gd::Int lo = command & 0xFF;
        if (hi >= EFFECT_OWN && hi < SPARK_FIRST + SPARK_INDEX.size()) {
            gd::PI32 point = _joint_point(f, lo);
            if (hi == EFFECT_OWN) {
                out->add_at(SimEvents::Kind::EFFECT, f->index, point, f->index, lo);
                out->add(SimEvents::Kind::FIGHTER_VIBRATE, f->index, VIBRATE_EFFECT);
            } else if (hi == EFFECT_OPPONENT) {
                out->add_at(SimEvents::Kind::EFFECT, f->index, point, f->cur_opp_index, lo);
            } else {
                gd::Int index = SPARK_INDEX[hi - SPARK_FIRST];
                out->add_at(SimEvents::Kind::SPARK, f->index, point, index, lo);
            }
        } else if (hi >= HAND_FIRST && hi <= HAND_LAST) {
            f->skip_step_physics = 1;
            gd::Int cmd = hi - HAND_FIRST;
            gd::Int hands = cmd >> 4;
            if (hands == 0) {
                hands = 3;
            }
            gd::Int shape = cmd & 7;
            gd::Int speed = Fx::s8(lo);
            if (hi == HAND_RESET) {
                shape = 0;
                speed = lo;
            } else if (hi == HAND_COSTUME) {
                shape = tables->hand_shape(f->costume_slot);
                speed = lo;
            }
            f->hands->command(hands, shape, speed, f->char_id);
            out->add(SimEvents::Kind::HAND, f->index, hands, shape)->c = speed;
        } else if (hi >= 1 && hi <= SHAKE_LAST) {
            out->add(SimEvents::Kind::SHAKE_REQUEST, f->index, hi - 1);
        } else if (hi == GROUND_IMPACT) {
            out->add_at(SimEvents::Kind::DUST, f->index, gd::PI32{f->root_x, f->root_y, f->root_z});
            // Bug #16: the game passes the player slot, not the fighter index (the same in
            // two-fighter modes).
            out->add(SimEvents::Kind::FIGHTER_VIBRATE, f->index, VIBRATE_IMPACT);
        } else if (hi >= EXTRA_FIRST && hi <= EXTRA_LAST) {
            f->extra_kind = hi - EXTRA_FIRST + 1;
            f->extra_damage = lo;
        }
    }
}

gd::PI32 MoveEvents::_joint_point(const gd::Ref<FighterState>& f, gd::Int joint) {
    if (f->body == nullptr) {
        return gd::PI32();
    }
    return f->body->joints[joint]->t.duplicate();
}

}  // namespace t3
