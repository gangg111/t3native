// remake/core/fighter/fighter_animation.gd
// FighterSkeletonUpdate (0x8002BCB8) / FighterAnimate (0x8003AAD0): root and pose decoding,
// RootReanchor, FighterTransitionBlend, RootUpdate, the hand poses, blinking, and
// FighterComposeJoints with the blend, the head look-at, True Ogre's sway and the costume
// attachments (moves.md#root-motion, moves.md#motion-blending, animation.md).
//
// Written from the verified ports root_reanchor, root_update, transition_blend,
// matrix_add_scaled and matrix_orthonormalize in tools/research/fight_sim.py and the decompiled
// FighterAnimate, FighterComposeJoints, HeadLookAt, FUN_80034354 and FUN_80033C54.
#include "core/fighter/fighter_animation.h"

#include "content/camera_tables.h"
#include "content/fight_tables.h"
#include "content/fighter_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "content/pose_tables.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_phase.h"
#include "core/camera/view_matrix.h"
#include "core/combat/state_bit.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/attachment_dynamics.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/fighter_skeleton.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/pose_solver.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"
#include "core/math/gte.h"

namespace t3 {

FighterAnimation::FighterAnimation(const gd::Ref<FightState>& fight_state) {
    fight = fight_state;
    t = fight_state->tables;
    solver = gd::make<PoseSolver>(t->pose);
    look_base = _angles_to_matrix(gd::PI32{0x4000, 0, 0x4000});
}

// PoseSolver.build with Gameplay fix #8 as it is now: the option can change in the middle of a session.
void FighterAnimation::_solve(const gd::PI32& pose, const gd::Arr<gd::PI32>& slots) {
    solver->wide_elbow = fight->rules->fix_elbow_bend_overflow;
    gd::Arr<gd::PI32> out = slots;   // the same array (a handle): PoseSolver::build takes a non-const reference
    solver->build(pose, out);
}

// FighterSkeletonUpdate: every active fighter in index order (FUN_8003AA6C).
void FighterAnimation::update_all(const gd::Ref<ViewMatrix>& view, bool blend_enabled, bool draw) {
    for (const auto& f : fight->active()) {
        if (f->active != 0) {
            // FUN_800363B0: Mokujin's stick is drawn only with Yoshimitsu's moves.
            if (f->char_id == Character::MOKUJIN) {
                f->body->stick_shown = f->bank_type == BankType::YOSHIMITSU;
            }
            if (f->keep_light == 0) {
                flash_step(fight, f);
            } else {
                // FUN_8003AA6C: SetBackColor(0, 0, 0) instead of FUN_8003A3B8; the flash waits.
                fight->flash_level[f->index] = LIT_BLACK;
            }
            animate(f, view, blend_enabled, draw);
        }
    }
}

// FUN_8003A3B8's counter: a lighting flash (Tekken Force's health pick-up) runs 32 frames. The
// count lights the frame it is read in (FightState.flash_level, StageLighting.fighter_back_colour)
// and is stepped after: counts 1 to 32 colour the fighter, then it ends.
void FighterAnimation::flash_step(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f) {
    gd::Int p = f->player_index;
    fight->flash_level[f->index] = gd::Int(fight->health_flash[p]);
    if (fight->health_flash[p] != 0) {
        fight->health_flash[p] += 1;
        if (fight->health_flash[p] > FLASH_FRAMES) {
            fight->health_flash[p] = 0;
        }
    }
}

// FighterAnimate for one fighter.
void FighterAnimation::animate(const gd::Ref<FighterState>& f, const gd::Ref<ViewMatrix>& view, bool blend_enabled,
                               bool draw) {
    gd::Ref<FighterBody> body = f->body;
    fight->replay->play_pose(f);
    // FUN_80038B60: a throw victim takes its partner's scale.
    f->scale = f->throw_state < 0 ? fight->fighters[f->throw_partner]->scale_base : f->scale_base;
    gd::PI32 d = _decode_root(f, f->root_move, f->root_frame - 1);
    f->root_dx = d[0];
    f->root_dy = d[1];
    f->root_dz = d[2];
    body->pose_vector = f->pose_move->anim_bank->pose(f->pose_move->anim, f->pose_frame - 1);
    _reanchor(f);
    body->decoded_root_dy = f->root_move->anim_bank->pose(f->root_move->anim, f->root_frame - 1)[1];
    if (blend_enabled && fight->blend_skip == 0) {
        _transition_blend(f);
    }
    fight->blend_skip = 0;
    fight->replay->record_pose(f);
    _root_update(f);
    if (fight->paused_player == 0 && fight->freeze == 0) {
        f->hands->update(f->char_id, f->air_free, t->fighter);
        if (f->char_id != Character::TRUE_OGRE) {
            fight->gon_mouth[f->player_index] = gd::Int(f->hands->variant[1]);
            for (gd::Int c = 0; c < 2; ++c) {
                f->hands->variant[c] = fight->replay->hand_pose(f, f->hands->variant[c], c);
            }
        } else {
            f->hands->wing_variant = fight->replay->wings(f, f->hands->wing_variant);
            f->hands->variant[0] = fight->replay->hand_pose(f, f->hands->variant[0], 0);
        }
    }
    _blink(f);
    _compose(f);
    if (draw) {
        f->screen_x = view->screen_of(body->joints[SCREEN_JOINT])[0];
    }
}

gd::PI32 FighterAnimation::_decode_root(const gd::Ref<FighterState>& f, const gd::Ref<MoveRow>& move, gd::Int frame) {
    gd::PI32 p = move->anim_bank->pose(move->anim, frame);
    return gd::PI32{Fx::s16((p[0] * f->scale) >> 12), Fx::s16((p[1] * f->scale) >> 12), Fx::s16((p[2] * f->scale) >> 12)};
}

// RootReanchor (0x8003AC1C): switching the root move does not make the body jump.
void FighterAnimation::_reanchor(const gd::Ref<FighterState>& f) {
    if (f->anchor_dirty == 0) {
        return;
    }
    gd::Int frame = gd::maxi(0, f->root_frame - f->frame_step - 1);
    f->anchor_dirty = 0;
    gd::PI32 d = _decode_root(f, f->root_move, frame);
    gd::Int a = (((0x8000 - f->facing) >> 3) & 0x1FFE) >> 1;
    gd::Int c = FightMath::cos12(a, t);
    gd::Int s = FightMath::sin12(a, t);
    f->pos_x = Fx::w32(f->root_x - Fx::trunc12(d[0] * c - d[2] * s));
    f->pos_z = Fx::w32(f->root_z - Fx::trunc12(d[0] * s + d[2] * c));
}

// RootUpdate (0x8003AD48): root matrix, blend weight and root position.
void FighterAnimation::_root_update(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterBody> body = f->body;
    gd::PI32 angles{Fx::s16(-f->tilt_x), Fx::s16(f->heading - 0x8000), Fx::s16(-f->tilt_z)};
    angles = fight->replay->root_rotation(f, angles);
    if (fight->replay_playing != 0) {
        f->heading = Fx::s16(angles[1] - 0x8000);
    }
    body->root_mat = FighterSkeleton::rotation_xyz(angles[0] & 0xFFFF, angles[1] & 0xFFFF, angles[2] & 0xFFFF, t->pose);
    gd::Int dy = f->root_dy;
    if (f->blend_mode == Blend::NONE || f->blend_frames == 0) {
        f->blend_weight = 0x1000;
        f->root_dy_blended = dy;
    } else {
        f->blend_weight = Fx::s16(Fx::div_trunc(Fx::w32(f->blend_counter << 12), f->blend_frames));
        gd::Int v = Fx::w32(f->blend_root_delta[1] * f->blend_weight * 2);
        dy = Fx::s16(((v < 0 ? v + 0x1FFF : v) >> 13) + dy);
        f->root_dy_blended = dy;
    }
    f->root_x = f->pos_x;
    f->root_y = f->pos_y;
    f->root_z = f->pos_z;
    if (f->air_phase == 0) {
        gd::Int a = ((0x8000 - f->facing) >> 4) & 0xFFF;
        gd::Int c = FightMath::cos12(a, t);
        gd::Int s = FightMath::sin12(a, t);
        f->root_x = Fx::w32(f->root_x + Fx::trunc12(f->root_dx * c - f->root_dz * s));
        f->root_y = Fx::w32(f->root_y + dy);
        f->root_z = Fx::w32(f->root_z + Fx::trunc12(f->root_dx * s + f->root_dz * c));
    } else if (f->air_phase == 2) {
        f->root_y = Fx::w32(f->root_y + dy);
    }
    fight->replay->root_position(f);
}

// FighterTransitionBlend (0x8003C4C4)

// BlendSuppressed (0x8003CAD0): move flag bit 8, +0xBA, or the bytes +0x89 and +0x8A of the word
// at +0x88 (guardedPrev, inReaction).
bool FighterAnimation::_blend_suppressed(const gd::Ref<FighterState>& f) {
    return (f->pose_move->flags & MoveFlag::NO_BLEND) != 0 || f->move_flag_ba != 0 || f->guarded_prev != 0
        || f->in_reaction != 0;
}

gd::Int FighterAnimation::_decay_length(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> pose = f->pose_move;
    if (f->last_pose_move == pose && f->branch_kind != 3) {
        return 0;
    }
    if (f->move_changed == 0) {
        return 0;
    }
    gd::Int frame = f->pose_frame;
    gd::Int left = gd::maxi(0, pose->length - frame);
    gd::Int n = 0;
    if (left != 0) {
        gd::Int active = pose->active_first;
        if (active == 0 || active <= frame) {
            n = f->throw_state < 0 ? 0x10 : 4;
        } else {
            n = gd::mini(active - frame, 0x10);
        }
    }
    if (left < n) {
        n = left;
    }
    return n >= 2 ? n : 0;
}

bool FighterAnimation::_no_blend_forward(const gd::Ref<FighterState>& f) {
    return (f->pose_move->flags & MoveFlag::NO_BLEND_FORWARD) != 0 || f->move_flag_bb != 0 || f->trans_bit6 != 0
        || f->juggle_count != 0 || (f->in_air != 0 && f->was_hit_this_move != 0);
}

gd::Int FighterAnimation::_branch_length(const gd::Ref<FighterState>& f) {
    if (f->move_row == nullptr) {
        return 0;
    }
    if (f->pose_frame < f->entry_frame && f->branch_kind != 3) {
        return gd::maxi(0, f->entry_frame - f->pose_frame + 1);
    }
    return 0;
}

gd::Int FighterAnimation::_stance_length(const gd::Ref<FighterState>& f) {
    if (f->move_row != nullptr) {
        return 0;
    }
    gd::Int left = f->pose_move->length - f->pose_frame;
    if (left >= 0x10) {
        return 0;
    }
    gd::Int n = f->frame_step >= 1 ? left + 1 : f->pose_frame;
    return n >= 2 ? n : 0;
}

void FighterAnimation::_transition_blend(const gd::Ref<FighterState>& f) {
    if (fight->freeze != 0 || fight->paused_player != 0) {
        _blend_finish(f);
        return;
    }
    if (fight->blend_hold == 0) {
        gd::Int n = 0;
        if (fight->replay_playing == 0) {
            n = _blend_choose(f);
        }
        if (f->blend_active != 0) {
            _blend_start(f, n);
            _blend_deltas(f);
        }
    } else {
        fight->blend_hold -= 1;
        f->blend_mode = Blend::NONE;
    }
    if (f->blend_active == 0) {
        _blend_count(f);
    }
    _blend_finish(f);
}

// Ends a finished blend and starts a new one: the decay blend when the move changed, else a
// forward blend into the pending move or the stance; returns its length (0: none).
gd::Int FighterAnimation::_blend_choose(const gd::Ref<FighterState>& f) {
    gd::Int n = 0;
    f->blend_active = 0;
    bool forward = true;
    if (!_blend_suppressed(f)) {
        if (f->blend_mode == Blend::DECAY && f->blend_counter == 0) {
            f->blend_mode = Blend::NONE;
        }
        n = _decay_length(f);
        if (n != 0) {
            f->blend_active = 1;
            f->blend_mode = Blend::DECAY;
        }
        forward = f->blend_mode != Blend::DECAY;
    } else if (f->blend_mode == Blend::DECAY) {
        f->blend_mode = Blend::NONE;
    }
    if (!forward) {
        return n;
    }
    if (_no_blend_forward(f)) {
        if (f->blend_mode == Blend::BRANCH || f->blend_mode == Blend::STANCE) {
            f->blend_mode = Blend::NONE;
        }
        return n;
    }
    if (f->blend_mode == Blend::BRANCH && f->blend_counter == f->blend_frames) {
        f->blend_mode = Blend::NONE;
    }
    if (f->blend_mode == Blend::STANCE && (f->blend_counter == f->blend_frames || f->move_changed != 0)) {
        f->blend_mode = Blend::NONE;
    }
    if (f->blend_mode == Blend::NONE) {
        n = _branch_length(f);
        gd::Int mode = Blend::BRANCH;
        if (n == 0) {
            n = _stance_length(f);
            mode = Blend::STANCE;
        }
        if (n != 0) {
            f->blend_mode = mode;
            f->blend_active = 1;
        }
    }
    return n;
}

// The blend's source and destination poses and its length `n` (during replay playback the
// recorded ones are kept and only the counter restarts).
void FighterAnimation::_blend_start(const gd::Ref<FighterState>& f, gd::Int n) {
    bool playback = fight->replay_playing != 0;
    switch (f->blend_mode) {
        case Blend::BRANCH:
            if (!playback) {
                f->blend_src_move = f->pose_move;
                f->blend_src_frame = f->entry_frame & 0xFF;
                f->blend_dst_move = fight->move_for_slot(f, f->move_slot);
                f->blend_dst_frame = (ENTRY_DST_CODES.has(f->transition) ? f->entry_frame : 0) & 0xFF;
                f->blend_frames = n;
            }
            f->blend_counter = 0;
            break;
        case Blend::STANCE:
            if (!playback) {
                f->blend_src_move = f->pose_move;
                f->blend_src_frame = (f->frame_step < 1 ? 0 : f->pose_move->length) & 0xFF;
                f->blend_dst_move = fight->move_for_slot(f, Fx::s16(f->pose_move->stance_slot));
                f->blend_dst_frame = 0;
                if (f->blend_dst_move == nullptr) {
                    // Bug #61 (not reproduced): no stance row, the blend keeps the running move.
                    f->blend_dst_move = f->pose_move;
                    f->blend_dst_frame = f->blend_src_frame;
                }
                f->blend_frames = n;
            }
            f->blend_counter = 0;
            break;
        case Blend::DECAY:
            if (!playback) {
                f->blend_counter = n - 1;
                f->blend_frames = n;
                f->blend_src_move = f->last_pose_move;
                f->blend_src_frame = (f->event_frame - 1) & 0xFF;
                f->blend_dst_move = f->pose_move;
                f->blend_dst_frame = (f->pose_frame - 1) & 0xFF;
            } else {
                f->blend_counter = f->blend_frames - 1;
            }
            break;
        default:
            break;
    }
}

// The root and joint rotation differences: the displayed pose against the new move's first pose
// for the decay blend, destination against source for the forward blends.
void FighterAnimation::_blend_deltas(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterBody> body = f->body;
    if (f->blend_mode == Blend::DECAY) {
        gd::PI32 dst = _decode_root(f, f->blend_dst_move, f->blend_dst_frame);
        // +0x1810 and +0x1814 are never written; +0x1812 is the displayed root dy.
        gd::PI32 shown{0, f->root_dy_blended, 0};
        for (gd::Int k = 0; k < 3; ++k) {
            f->blend_root_delta[k] = Fx::s16(gd::Int(shown[k]) - dst[k]);
        }
        gd::Arr<gd::PI32> slots = _pose_slots(f->blend_dst_move, f->blend_dst_frame);
        for (gd::Int i = 1; i < FighterBody::JOINTS; ++i) {
            gd::PI32 prev = body->prev_local_mats[i - 1];
            gd::PI32 delta = body->blend_delta[i - 1];
            for (gd::Int e = 0; e < 9; ++e) {
                delta[e] = Fx::s16(gd::Int(prev[e]) - slots[i][e]);
            }
        }
    } else if (f->blend_mode == Blend::BRANCH || f->blend_mode == Blend::STANCE) {
        gd::PI32 src = _decode_root(f, f->blend_src_move, f->blend_src_frame);
        gd::PI32 dst = _decode_root(f, f->blend_dst_move, f->blend_dst_frame);
        for (gd::Int k = 0; k < 3; ++k) {
            f->blend_root_delta[k] = Fx::s16(gd::Int(dst[k]) - src[k]);
        }
        gd::Arr<gd::PI32> a = _pose_slots(f->blend_src_move, f->blend_src_frame);
        gd::Arr<gd::PI32> b = _pose_slots(f->blend_dst_move, f->blend_dst_frame);
        for (gd::Int i = 1; i < FighterBody::JOINTS; ++i) {
            gd::PI32 delta = body->blend_delta[i - 1];
            for (gd::Int e = 0; e < 9; ++e) {
                delta[e] = Fx::s16(gd::Int(b[i][e]) - a[i][e]);
            }
        }
    }
}

// A running blend's counter: down for the decay blend, up to its length for the forward ones.
void FighterAnimation::_blend_count(const gd::Ref<FighterState>& f) {
    switch (f->blend_mode) {
        case Blend::DECAY:
            if (f->blend_counter >= 1) {
                f->blend_counter -= 1;
            } else if (f->blend_counter < 0) {
                f->blend_counter = 0;
            }
            break;
        case Blend::NONE:
            f->blend_counter = 0;
            f->blend_frames = 0;
            break;
        case Blend::BRANCH:
        case Blend::STANCE:
            if (f->blend_counter < f->blend_frames) {
                f->blend_counter += 1;
            } else if (f->blend_frames < f->blend_counter) {
                f->blend_counter = f->blend_frames;
            }
            break;
        default:
            break;
    }
}

void FighterAnimation::_blend_finish(const gd::Ref<FighterState>& f) {
    f->last_pose_move = f->pose_move;
    f->last_pose_frame = f->pose_frame;
    f->root_dy_blended = f->body->decoded_root_dy;
}

// The 18 local rotations of a move's frame, from zeroed slots (the pose solver leaves slots
// whose limb target is too close unchanged).
gd::Arr<gd::PI32> FighterAnimation::_pose_slots(const gd::Ref<MoveRow>& move, gd::Int frame) {
    gd::Arr<gd::PI32> slots;
    for (gd::Int i = 0; i < FighterBody::JOINTS; ++i) {
        slots.append(gd::PI32{0, 0, 0, 0, 0, 0, 0, 0, 0});
    }
    _solve(move->anim_bank->pose(move->anim, frame), slots);
    return slots;
}

// blinking (FUN_80034354)

// The eyes: moves with flag 0x40000 (and lying moves without the look-at flag) keep them
// closed; otherwise they stay open for a random odd 1 to 255 frames and closed for 4. Gon's
// mouth follows with HandFaceCommand kind 1. The texture copies are the presentation's.
void FighterAnimation::_blink(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterHands> hands = f->hands;
    gd::Int flags = f->pose_move->flags;
    bool gon = f->char_id == Character::GON;
    bool closed = false;
    if ((flags & MoveFlag::REACT_CHAIN) != 0) {
        closed = true;
        // Under the overhead KO camera (0x800B08D4) Gon shuts his eyes on these moves by the jaw's
        // shape 3 (and a palette swap of his own, FighterView.set_overhead).
        if (gon && camera != nullptr && camera->overhead != 0) {
            hands->command(1, 3, 1, f->char_id);
        }
    } else if ((f->pose_move->state & StateBit::DOWN) != 0 && (flags & MoveFlag::LOOK_AT) == 0) {
        closed = true;
    }
    if (closed) {
        hands->face_speed = 2;
        hands->face = 1;
    }
    if (hands->face_speed == 0) {
        if (hands->face == 0) {
            if (gon && fight->gon_mouth[f->player_index] == 0 && hands->current[1] == 0) {
                hands->command(1, 3, 1, f->char_id);
                fight->gon_mouth[f->player_index] = 0;
            }
            hands->face_speed = BLINK_CLOSED;
            hands->face = 1;
        } else {
            if (gon && fight->gon_mouth[f->player_index] == 0 && hands->current[1] == 0) {
                hands->command(1, 0, 1, f->char_id);
                fight->gon_mouth[f->player_index] = 0;
            }
            hands->face_speed = (fight->rng->next() & 0xFE) + 1;
            hands->face = 0;
        }
    }
    hands->face_speed = Fx::s16(hands->face_speed - 1);
}

// FighterComposeJoints (0x8003B220)

void FighterAnimation::_compose(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterBody> body = f->body;
    body->root = gd::make<JointFrame>(body->root_mat, gd::PI32{f->root_x, f->root_y, f->root_z});
    if (fight->paused_player == 0 && fight->freeze == 0) {
        _solve(body->pose_vector, body->local_mats);
        if (f->blend_mode == Blend::NONE) {
            for (gd::Int i = 1; i < FighterBody::JOINTS; ++i) {
                body->prev_local_mats[i - 1] = body->local_mats[i].duplicate();
            }
        } else {
            for (gd::Int i = 1; i < FighterBody::JOINTS; ++i) {
                gd::PI32 m = _add_scaled(body->blend_delta[i - 1], body->local_mats[i], f->blend_weight);
                body->local_mats[i] = _orthonormalize(m);
                body->prev_local_mats[i - 1] = body->local_mats[i].duplicate();
            }
        }
        if (f->no_look_at == 0) {
            if (f->char_id == Character::GON) {
                _gon_eyes_follow(f, fight->opponent(f));
            }
            _head_look_at(f, fight->opponent(f));
        }
    }
    bool ogre = f->char_id == Character::TRUE_OGRE;
    gd::Int tilt = 0;
    if (ogre) {
        tilt = CameraMath::atan2_units4096(body->local_mats[5][0], body->local_mats[5][1], t->camera) * -4;
    }
    for (gd::Int i = 0; i < FighterBody::JOINTS; ++i) {
        if (!body->present[i]) {
            continue;
        }
        gd::PI32 local = ogre ? _ogre_local(f, i, tilt) : body->local_mats[i];
        gd::PI32 offset = body->offsets[ogre ? _ogre_slot(i) : i];
        gd::Ref<JointFrame> parent = body->parents[i] < 0 ? body->root : body->joints[body->parents[i]];
        body->joints[i] = JointFrame::compose_offset(parent, local, offset);
    }
    for (gd::Int i = FighterBody::JOINTS; i < FighterBody::PARTS; ++i) {
        if (!body->present[i]) {
            continue;
        }
        gd::Int a = i - FighterBody::JOINTS;
        gd::Ref<JointFrame> parent = body->parents[i] < 0 ? body->root : body->joints[body->parents[i]];
        if (fight->freeze == 0 && fight->paused_player == 0) {
            body->attach_local[a] = body->attachments->update(a, parent, body->joints[i], body->offsets[i], body->joints);
        }
        gd::PI32 local = ogre ? _ogre_local(f, i, tilt) : body->attach_local[a];
        gd::PI32 offset = body->offsets[ogre ? _ogre_slot(i) : i];
        body->joints[i] = JointFrame::compose_offset(parent, local, offset);
    }
    body->draw_joints = body->joints.duplicate();
}

// MatrixAddScaled (0x8003A980): base + sat16(weight · delta >> 12).
gd::PI32 FighterAnimation::_add_scaled(const gd::PI32& delta, const gd::PI32& base, gd::Int weight) {
    gd::Int w = Fx::s16(weight);
    gd::PI32 out;
    out.resize(9);
    for (gd::Int e = 0; e < 9; ++e) {
        out[e] = Fx::s16(Fx::sat16((w * Fx::s16(delta[e])) >> 12) + base[e]);
    }
    return out;
}

// MatrixOrthonormalize (0x800762B0): Gram-Schmidt on the first two columns, the third their
// cross product.
gd::PI32 FighterAnimation::_orthonormalize(const gd::PI32& m) {
    gd::PI32 c0 = Gte::vector_normal(gd::PI32{m[0], m[3], m[6]}, t->fighter->rsqrt);
    gd::Int dot = Fx::w32(m[1] * c0[0] + m[4] * c0[1] + m[7] * c0[2]) >> 12;
    gd::PI32 v{Fx::w32(m[1] - (Fx::w32(dot * c0[0]) >> 12)), Fx::w32(m[4] - (Fx::w32(dot * c0[1]) >> 12)),
               Fx::w32(m[7] - (Fx::w32(dot * c0[2]) >> 12))};
    gd::PI32 c1 = Gte::vector_normal(v, t->fighter->rsqrt);
    gd::Int d1 = Fx::s16(c0[0]);
    gd::Int d2 = Fx::s16(c0[1]);
    gd::Int d3 = Fx::s16(c0[2]);
    gd::Int i1 = Fx::s16(c1[0]);
    gd::Int i2 = Fx::s16(c1[1]);
    gd::Int i3 = Fx::s16(c1[2]);
    gd::PI32 c2{(d2 * i3 - d3 * i2) >> 12, (d3 * i1 - d1 * i3) >> 12, (d1 * i2 - d2 * i1) >> 12};
    gd::PI32 out;
    out.resize(9);
    for (gd::Int r = 0; r < 3; ++r) {
        out[3 * r] = Fx::s16(c0[r]);
        out[3 * r + 1] = Fx::s16(c1[r]);
        out[3 * r + 2] = Fx::s16(c2[r]);
    }
    return out;
}

// True Ogre (FUN_80033C54)

// The local matrix slot True Ogre draws joint `i` with: joints 5, 6, 18 and 19 (the wings and
// tail) take the next slot of the cycle 5 → 18 → 19 → 6 → 5. ComposeJoint reads the joint offset
// from the slot's matrix too, so these joints are also placed with the slot's offset.
gd::Int FighterAnimation::_ogre_slot(gd::Int i) {
    switch (i) {
        case 5:
            return 0x12;
        case 6:
            return 5;
        case 0x12:
            return 0x13;
        case 0x13:
            return 6;
        default:
            break;
    }
    return i;
}

// The local rotation True Ogre draws joint `i` with: joints 5, 6, 18 and 19 sway with a counter
// and his level channel (written into the slot of _ogre_slot); the others keep their pose
// rotation.
gd::PI32 FighterAnimation::_ogre_local(const gd::Ref<FighterState>& f, gd::Int i, gd::Int tilt) {
    gd::Ref<FighterBody> body = f->body;
    gd::Int phase = -1;
    gd::Int slot = _ogre_slot(i);
    switch (i) {
        case 6:
            phase = 1;
            break;
        case 5:
            if (fight->paused_player == 0 && fight->freeze == 0) {
                body->ogre_sway = Fx::s16(body->ogre_sway + 1);
            }
            fight->ogre_phase_x = (body->ogre_sway & 0x7F) << 5;
            fight->ogre_phase_y = (body->ogre_sway & 0x3F) << 6;
            fight->ogre_height = gd::clampi(-gd::Int(body->joints[6]->t[1]) / 2, 0, 0x100);
            phase = 0;
            break;
        case 0x12:
            phase = 2;
            break;
        case 0x13:
            phase = 3;
            break;
        default:
            break;
    }
    gd::PI32 m = slot < FighterBody::JOINTS ? body->local_mats[slot] : body->attach_local[slot - FighterBody::JOINTS];
    if (fight->paused_player != 0 || fight->freeze != 0 || phase < 0) {
        return m;
    }
    gd::Int level = f->hands->ogre_level;
    gd::Int amount = fight->ogre_height * (0x100 - level);
    amount = (amount < 0 ? amount + 0xFF : amount) >> 8;
    gd::Int sx = FightMath::sin12(fight->ogre_phase_x + phase * 0x400, t) * amount;
    gd::Int angle_x = Fx::s16((sx < 0 ? sx + 0x1FF : sx) >> 9);
    gd::Int sy = FightMath::sin12(fight->ogre_phase_y + phase * 0x400, t) * amount;
    gd::Int angle_y = Fx::s16(((sy < 0 ? sy + 0xFF : sy) & 0xFFFFFFFFLL) >> 8) + tilt;
    gd::PI32 rot = _angles_to_matrix(gd::PI32{0, angle_x, Fx::s16(angle_y)});
    gd::Int row_scale = ((level < 0 ? level + 3 : level) >> 2) + 0x100;
    gd::Int col_scale = ((level < 0 ? level + 7 : level) >> 3) + 0x100;
    if (i == 0x13) {
        row_scale = 0x200 - row_scale;
    }
    for (gd::Int r = 0; r < 3; ++r) {
        gd::Int a = rot[3 * r] * row_scale;
        rot[3 * r] = Fx::s16(((a < 0 ? a + 0xFF : a) & 0xFFFFFFFFLL) >> 8);
        gd::Int b = rot[3 * r + 1] * col_scale;
        rot[3 * r + 1] = Fx::s16(((b < 0 ? b + 0xFF : b) & 0xFFFFFFFFLL) >> 8);
        gd::Int c = rot[3 * r + 2] * col_scale;
        rot[3 * r + 2] = Fx::s16(((c < 0 ? c + 0xFF : c) & 0xFFFFFFFFLL) >> 8);
    }
    if (slot < FighterBody::JOINTS) {
        body->local_mats[slot] = rot;
    } else {
        body->attach_local[slot - FighterBody::JOINTS] = rot;
    }
    return rot;
}

// head look-at (HeadLookAt, 0x800392C8)

// GonEyesFollow (0x80039A50): the direction of the opponent's head in the frame of Gon's own
// head (his joint block of the last frame), as an angle in 4096 units that GonEyesSetOffset turns
// into the pupils' shift: 0 to 17 to one side, 0 to −17 to the other. HeadLookAt leaves his yaw alone.
void FighterAnimation::_gon_eyes_follow(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<JointFrame> head = f->body->joints[HEAD_JOINT];
    // A record without a model (Tekken Ball's second fighter) has no joints: its words are zero.
    gd::PI32 target = opp->body != nullptr ? opp->body->joints[HEAD_JOINT]->t : gd::PI32{0, 0, 0};
    f->body->gon_gaze = gon_gaze(head->rot, head->t, target, t->camera);
}

// The shift GonEyesFollow turns the opponent's head position `target` into, for Gon's head
// frame (`rot`, `own`).
gd::Int FighterAnimation::gon_gaze(const gd::PI32& rot, const gd::PI32& own, const gd::PI32& target,
                                   const gd::Ref<CameraTables>& tables) {
    gd::PI32 local = Gte::apply_lv(Fx::transpose(rot),
                                   gd::PI32{target[0] - own[0], target[1] - own[1], target[2] - own[2]});
    gd::Int angle = CameraMath::atan2_units4096(local[0], local[1], tables);
    if (angle < GAZE_HALF_TURN) {
        return gd::maxi(-Fx::div_trunc(angle, GAZE_UNITS), -GAZE_RANGE);
    }
    return gd::mini(-Fx::div_trunc(angle - GAZE_FORWARD, GAZE_UNITS), GAZE_RANGE);
}

void FighterAnimation::_head_look_at(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) {
    gd::Ref<FighterBody> body = f->body;
    gd::Ref<FighterBody::LookAt> look = body->look_at;
    gd::Int active = 0;
    gd::Ref<MoveRow> pose = f->pose_move;
    if ((pose->flags & MoveFlag::LOOK_AT) != 0 && fight->camera_phase == CameraPhase::FIGHT) {
        active = 1;
        if (pose->active_first != 0) {
            gd::Int from = gd::maxi(pose->active_last, pose->length - 0x11);
            if (f->pose_frame < from) {
                active = 0;
            }
        }
    }
    gd::Int was = look->active;
    look->active = active;
    if (active == 0) {
        return;
    }
    gd::PI32 o1 = opp->body->joints[CHEST_JOINT]->t;
    gd::PI32 o2 = opp->body->joints[HEAD_JOINT]->t;
    gd::PI32 head = body->joints[HEAD_JOINT]->t;
    gd::Int tx = Fx::s16(Fx::s16(o1[0] + ((o2[0] - o1[0]) >> 1)) - head[0]);
    gd::Int ty = Fx::s16(Fx::s16(o1[1] + ((o2[1] - o1[1]) >> 1)) - head[1]);
    gd::Int tz = Fx::s16(Fx::s16(o1[2] + ((o2[2] - o1[2]) >> 1)) - head[2]);
    // FUN_8004B4B0: the chest's axes, then the target in the chest frame (MAC results).
    gd::PI32 chest = body->joints[CHEST_JOINT]->rot;
    gd::PI32 ax = Fx::mvmva(chest, 0x1000, 0, 0);
    gd::PI32 ay = Fx::mvmva(chest, 0, 0x1000, 0);
    gd::PI32 az = Fx::mvmva(chest, 0, 0, 0x1000);
    gd::PI32 frame{ax[0], ax[1], ax[2], ay[0], ay[1], ay[2], az[0], az[1], az[2]};
    gd::PI32 local = Gte::apply(frame, tx, ty, tz);
    gd::Int lx = local[0];
    gd::Int ly = local[1];
    gd::Int lz = local[2];
    gd::Int elevation = FightMath::atan2_angle(t->pose->square_root0(Fx::w32(ly * ly + lz * lz)), Fx::s16(lx), t);
    gd::Int pitch = Fx::s16(-elevation);
    if (pitch > LOOK_UP) {
        pitch = LOOK_UP;
    }
    if (pitch < LOOK_DOWN) {
        pitch = LOOK_DOWN;
    }
    gd::Int yaw = f->char_id == Character::GON ? 0 : Fx::s16(FightMath::atan2_angle(Fx::s16(ly), Fx::s16(lz), t));
    gd::Int tilt = FightMath::atan2_angle(t->pose->square_root0(Fx::w32(ax[0] * ax[0] + ax[2] * ax[2])), Fx::s16(ax[1]), t);
    gd::Int tilt_up = Fx::s16(tilt) + 0x4000;
    gd::Int limit = 0;
    if (tilt_up < 0xAAB) {
        limit = 0x38E3;
    } else if (tilt_up < 0x38E3) {
        limit = 0x38E3 - Fx::div_trunc((Fx::s16(tilt) + 0x3556) * 0x238E, 0x2E39);
    } else {
        limit = 0x1555;
    }
    yaw = gd::clampi(yaw, -limit, limit);
    bool newly = active != was;
    bool refresh = true;
    if (!newly) {
        if (gd::absi(ly) < 400 && gd::absi(lz) < 300) {
            look->hold = (look->hold + 1) & 0xFF;
            refresh = look->hold >= LOOK_HOLD;
            if (refresh) {
                look->hold = 0;
            }
        } else {
            look->hold = 0;
        }
    } else {
        look->hold = 0;
    }
    if (refresh) {
        look->target = gd::PI32{0, pitch, yaw, look->target[3]};
    }
    if (look->snap != 0) {
        look->target = gd::PI32{0, pitch, yaw, look->target[3]};
        look->current = look->target.duplicate();
        look->snap = 0;
    } else if (newly) {
        look->current = _head_angles(body->local_mats[HEAD_SLOT]);
    } else {
        for (gd::Int k : gd::Arr<gd::Int>{1, 2}) {
            gd::Int e = look->target[k] - look->current[k];
            gd::Int step = e - Fx::div_trunc(e, 2);
            if (gd::absi(step) > LOOK_STEP) {
                step = step > 0 ? LOOK_STEP : -LOOK_STEP;
            }
            look->current[k] = Fx::s16(look->current[k] + step);
        }
        look->current[0] = look->target[0];
    }
    body->local_mats[HEAD_SLOT] = Fx::mul_matrix(look_base, _angles_to_matrix(look->current));
}

// The three angles of the head's animated local matrix, so the look-at starts without a pop.
gd::PI32 FighterAnimation::_head_angles(const gd::PI32& m) {
    gd::PI32 v48 = Gte::apply(m, 0x1000, 0, 0);
    gd::PI32 v38 = Gte::apply(m, 0, 0, 0x1000);
    gd::Int a28 = Fx::s16(FightMath::atan2_angle(Fx::s16(v48[1]), Fx::s16(v48[2]), t));
    gd::Int n = -a28;
    gd::Int idx = ((n < 0 ? n + 0xF : n) >> 4) & 0xFFF;
    gd::Int s = FightMath::sin12(idx, t);
    gd::Int c = FightMath::cos12(idx, t);
    gd::Int y48 = Fx::trunc12(v48[1] * c - v48[2] * s);
    gd::Int y38 = Fx::trunc12(v38[1] * c - v38[2] * s);
    gd::Int z38 = Fx::trunc12(v38[1] * s + v38[2] * c);
    gd::Int a24raw = FightMath::atan2_angle(Fx::s16(v48[0]), Fx::s16(y48), t) - 0x4000;
    gd::Int a24 = Fx::s16(a24raw);
    gd::Int m2 = -a24;
    gd::Int idx2 = ((m2 < 0 ? m2 + 0xF : m2) >> 4) & 0xFFF;
    gd::Int s2 = FightMath::sin12(idx2, t);
    gd::Int c2 = FightMath::cos12(idx2, t);
    gd::Int x38 = Fx::trunc12(v38[0] * c2 - y38 * s2);
    gd::Int a26 = Fx::s16(FightMath::atan2_angle(Fx::s16(z38), Fx::s16(x38), t) - 0x4000);
    return gd::PI32{a26, a24, a28, 0};
}

// FUN_8003A5A0: a rotation from three 16-bit angles (x at +0, y at +2, z at +4).
gd::PI32 FighterAnimation::_angles_to_matrix(const gd::PI32& a) {
    gd::Int i0 = ((a[0] & 0xFFFF) >> 4) & 0xFFF;
    gd::Int i1 = ((a[1] & 0xFFFF) >> 4) & 0xFFF;
    gd::Int i2 = (Fx::s16(a[2]) >> 4) & 0xFFF;
    gd::Int s0 = FightMath::sin12(i0, t);
    gd::Int c0 = FightMath::cos12(i0, t);
    gd::Int s1 = FightMath::sin12(i1, t);
    gd::Int c1 = FightMath::cos12(i1, t);
    gd::Int s2 = FightMath::sin12(i2, t);
    gd::Int c2 = FightMath::cos12(i2, t);
    gd::Int r3 = (c0 * c2) >> 12;
    gd::PI32 g1 = Fx::mvmva_gpf(s0, c2, s2, c1);
    gd::Int r6 = (c0 * s2) >> 12;
    gd::PI32 g2 = Fx::mvmva_gpf(s1, g1[0], g1[1], r3);
    gd::PI32 g3 = Fx::mvmva_gpf(c1, c2, s2, c0);
    gd::PI32 m;
    m.resize(9);
    m[7] = Fx::s16(g1[2]);
    m[6] = Fx::s16(-s1);
    m[1] = Fx::s16(g2[0] - r6);
    m[4] = Fx::s16(r3 + g2[1]);
    m[2] = Fx::s16(g1[1] + g2[2]);
    m[5] = Fx::s16(((r6 * s1) >> 12) - g1[0]);
    m[0] = Fx::s16(g3[0]);
    m[3] = Fx::s16(g3[1]);
    m[8] = Fx::s16(g3[2]);
    return m;
}

}  // namespace t3
