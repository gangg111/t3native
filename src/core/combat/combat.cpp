// remake/core/combat/combat.gd
// Everything between the two fighters' bodies (combat.md, fight-frame.md): PairwiseDistances,
// ArenaBounds with its ramp and the throw link, the saved segment ends, FighterVelocity,
// CollisionShapesUpdate with the baked attack points, BodySphereProfile, BodySeparate,
// HitTest, AttackVelocity and HitApply.
//
// Written from the verified ports in tools/research/fight_sim.py (arena_bounds, body_separate,
// collision_shapes_update, attack_segments_baked, hit_test, hit_apply) and the decompiled
// PairwiseDistances, BodySphereProfile and HitSpawnEffect.
#include "core/combat/combat.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/camera_math.h"
#include "core/combat/attack_records.h"
#include "core/combat/hit_slot.h"
#include "core/combat/state_bit.h"
#include "core/effects/effect_objects.h"
#include "core/effects/effect_pool.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/replay_state.h"
#include "core/fight/round_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_physics.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_flag.h"
#include "core/fighter/move_system.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

Combat::Combat(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system,
               const gd::Ref<FighterPhysics>& fighter_physics) {
    fight = fight_state;
    t = fight_state->tables;
    moves = move_system;
    physics = fighter_physics;
}

gd::Int Combat::_pair(gd::Int a, gd::Int b) {
    if (a > 1) {
        a = gd::shl(1, a - 1);
    }
    if (b > 1) {
        b = gd::shl(1, b - 1);
    }
    return a + b;
}

// ---- distances

// PairwiseDistances (0x8004391C): distance, offset and direction of every pair, then per
// fighter its distance to its opponent and the direction (or, with a fixed facing or an
// opponent off screen, a screen-based stand-in).
void Combat::pairwise_distances() {
    gd::Arr<gd::Ref<FighterState>> fs = fight->active();
    gd::PI32 offscreen{0, 0, 0};
    for (gd::Int i = 0; i < fs.size(); ++i) {
        offscreen[i] = (fs[i]->screen_x & 0xFFFF) >= SCREEN_EDGE ? 1 : 0;
        if (fight->mode == GameMode::FORCE && fs[i]->is_cpu != 0 && fight->force->slot_free(fs[i]->index - 1)) {
            offscreen[i] = 1;
        }
    }
    for (gd::Int i = fs.size() - 1; i > 0; i += -1) {
        for (gd::Int j = i - 1; j > -1; j += -1) {
            gd::Int dx = Fx::w32(fs[i]->root_x - fs[j]->root_x);
            gd::Int dz = Fx::w32(fs[i]->root_z - fs[j]->root_z);
            gd::Int p = _pair(i, j);
            fight->pair_distance[p] = CameraMath::isqrt(Fx::w32(dx * dx + dz * dz));
            fight->pair_dx[p] = dx;
            fight->pair_dz[p] = dz;
            fight->pair_dir[p] = FightMath::atan2_angle(dz, dx, t);
        }
    }
    for (const gd::Ref<FighterState>& f : fs) {
        f->placed_x = f->root_x;
        f->placed_z = f->root_z;
        gd::Int o = fight->opponent_index(f);
        gd::Int p = _pair(f->index, o);
        f->dist = fight->pair_distance[p] & 0xFFFFFFFFLL;
        gd::Ref<FighterState> opp = fight->fighters[o];
        gd::Int big = 0;
        if (f->char_id == Character::KUMA || f->char_id == Character::TRUE_OGRE) {
            big = 1;
        }
        if (opp->char_id == Character::KUMA || opp->char_id == Character::TRUE_OGRE) {
            big += 1;
        }
        f->dist_adj = (f->dist - big * 0x28) & 0xFFFFFFFFLL;
        f->dir_x = fight->pair_dx[p];
        f->dir_z = fight->pair_dz[p];
        gd::Int dir = o < f->index ? gd::Int(fight->pair_dir[p]) : fight->pair_dir[p] - 0x8000;
        if (f->fixed_facing == 0 && offscreen[o] == 0) {
            f->target_dir = dir & 0xFFFF;
        } else {
            gd::Int x = gd::absi(0xB4 - gd::absi(0xB4 - Fx::s16(f->screen_x)));
            f->target_dir = (f->fixed_facing_side == 0 ? 0x4000 : -0x4000) & 0xFFFF;
            f->dist = x * 10;
            f->dir_x = x * 10;
            f->dir_z = 0;
        }
    }
    fight->fighter_distance = fs[0]->dist;
}

// ---- arena bounds

gd::Int Combat::_excess(gd::Int v, gd::Int limit) {
    gd::Int over = gd::absi(v) - limit;
    if (over <= 0) {
        return 0;
    }
    return v > 0 ? -over : over;
}

gd::Int Combat::_clamp_ramp(gd::Int v, gd::Int limit) {
    if (limit < gd::absi(v)) {
        return v < 1 ? -limit : limit;
    }
    return v;
}

// ArenaBounds (0x80043394) for the normal modes: the root back inside |x|, |z| <= 300,000, and
// the pull towards the opponent (or back to where the fighter stood) beyond 0x1E00 apart;
// Tekken Ball's court and Tekken Force's walls in their modes.
void Combat::arena_bounds(const gd::Ref<FighterState>& f) {
    gd::Ref<FighterState> opp = fight->opponent(f);
    gd::Int dx = 0;
    gd::Int dy = 0;
    gd::Int dz = 0;
    if (fight->mode == GameMode::BALL) {
        // The Tekken Ball court: a fighter on the other half goes back to the net, and one far
        // across loses its posture (no attack can hit it) and its body push.
        dz = _excess(f->root_z, BALL_COURT_Z);
        dx = _excess(f->root_x, BALL_COURT_X);
        gd::Int x = f->root_x;
        if ((f->index == 0 && x >= 0) || (f->index == 1 && x < 1)) {
            dx = -x;
            if (gd::absi(x) > BALL_CROSS) {
                f->state = NO_POSTURE;
                f->hit_done[0] = 0;
                f->hit_done[1] = 0;
                f->no_body_push = 1;
            }
        }
        if (BALL_CEILING - f->root_y > 0) {
            dy = BALL_CEILING - f->root_y;
        }
    } else if (fight->mode == GameMode::FORCE && fight->round_state != RoundState::INTRO) {
        gd::Vector2i d = fight->force->walls(f);
        dz = d.x;
        dx = d.y;
    } else {
        dx = _excess(f->root_x, ARENA_HALF);
        dz = _excess(f->root_z, ARENA_HALF);
        if (FOLLOW_START < f->dist) {
            gd::Int ex = Fx::w32(opp->placed_x - f->root_x);
            gd::Int ez = Fx::w32(opp->placed_z - f->root_z);
            gd::Int d = FightMath::sqrt_table(Fx::w32(ex * ex + ez * ez), t);
            if (d < FOLLOW_MAX + 1) {
                if (f->dist <= d) {
                    dx = Fx::w32(f->placed_x - f->root_x);
                    dz = Fx::w32(f->placed_z - f->root_z);
                }
            } else {
                gd::Vector2i v = physics->polar(Fx::w32(d - FOLLOW_MAX), f->target_dir);
                dx = v.x;
                dz = v.y;
            }
        }
    }
    if (fight->mode == GameMode::BALL && f->throw_state < 0) {
        fight->bound_ramp[f->index] = 0;
        fight->bound_ramp_step[f->index] = 0;
    }
    gd::Int limit = fight->bound_ramp[f->index];
    dx = _clamp_ramp(dx, limit);
    dy = _clamp_ramp(dy, limit);
    dz = _clamp_ramp(dz, limit);
    gd::Arr<gd::Ref<FighterState>> movers{f};
    if (f->in_throw != 0 && opp->in_throw != 0 && fight->throw_link != 0) {
        movers.append(opp);
    }
    for (const gd::Ref<FighterState>& m : movers) {
        m->pos_x = Fx::w32(m->pos_x + dx);
        m->pos_z = Fx::w32(m->pos_z + dz);
        m->pos_y = Fx::w32(m->pos_y + dy);
        m->root_x = Fx::w32(m->root_x + dx);
        m->root_z = Fx::w32(m->root_z + dz);
        m->root_y = Fx::w32(m->root_y + dy);
    }
}

// FUN_800432B0 (Tekken Ball and Tekken Force): a throw victim's anchor follows the thrower's
// while the throw link holds; in Tekken Force the link drops when the pair drifts more than
// 0x578 apart after the move's 18th frame.
void Combat::throw_anchor(const gd::Ref<FighterState>& f) {
    if (fight->mode != GameMode::BALL && fight->mode != GameMode::FORCE) {
        return;
    }
    gd::Ref<FighterState> opp = fight->opponent(f);
    if (f->in_throw == 0 || opp->in_throw == 0 || f->throw_state >= 0) {
        return;
    }
    if (fight->mode == GameMode::FORCE && f->move_frame > 0x12
            && (fight->pair_distance[_pair(f->index, opp->index)] & 0xFFFFFFFFLL) > THROW_DRIFT) {
        fight->throw_link = 0;
    }
    if (fight->throw_link != 0) {
        f->pos_x = opp->pos_x;
        f->pos_z = opp->pos_z;
    }
}

// FUN_80043260: the throw link follows the number of fighters in a throw.
void Combat::update_throw_link() {
    if (fight->throw_count < 2) {
        fight->throw_link = 0;
    } else if (fight->throw_count_seen != fight->throw_count) {
        fight->throw_link = 1;
    }
    fight->throw_count_seen = fight->throw_count;
}

// ArenaRampStep (0x800431D8): the correction limit grows by an increment that grows by 2.
void Combat::ramp_step() {
    for (gd::Int i = 0; i < 3; ++i) {
        if (fight->bound_ramp[i] < RAMP_MAX) {
            fight->bound_ramp[i] += fight->bound_ramp_step[i];
            fight->bound_ramp_step[i] += 2;
        }
        if (RAMP_MAX < fight->bound_ramp[i]) {
            fight->bound_ramp[i] = RAMP_MAX;
            fight->bound_ramp_step[i] = 2;
        }
    }
}

// FUN_80045B10: the number of fighters in a throw.
gd::Int Combat::count_throws() {
    gd::Int n = 0;
    for (const gd::Ref<FighterState>& f : fight->active()) {
        if (f->in_throw != 0) {
            n += 1;
        }
    }
    return n;
}

// ---- per-frame records

// FighterSavePrevSegments (0x8006AD84): the anchor and segment ends of the last frame; a
// segment without a start joint then sweeps from its old end.
void Combat::save_prev_segments(const gd::Ref<FighterState>& f) {
    gd::Int i = f->index;
    fight->segment_anchor[i] = gd::PI32{f->pos_x, f->pos_y, f->pos_z};
    gd::PI32 s0 = f->attack_segs[0];
    gd::PI32 s1 = f->attack_segs[1];
    fight->segment_end0[i] = s0.slice(3, 6);
    fight->segment_end1[i] = s1.slice(3, 6);
    for (gd::Int k = 0; k < 3; ++k) {
        s0[k] = s0[3 + k];
        s1[k] = s1[3 + k];
    }
}

// SavePlayerPosition (0x80046F98).
void Combat::save_position(const gd::Ref<FighterState>& f) {
    gd::PI32 r = fight->prev_roots[f->index];
    r[0] = f->root_x;
    r[1] = f->root_y;
    r[2] = f->root_z;
}

// FighterVelocity (0x80040CA4).
void Combat::velocity(const gd::Ref<FighterState>& f) {
    f->vel_x = Fx::w32(f->root_x - f->prev_root_x);
    f->vel_y = Fx::w32(f->root_y - f->prev_root_y);
    f->vel_z = Fx::w32(f->root_z - f->prev_root_z);
    f->prev_root_x = f->root_x;
    f->prev_root_y = f->root_y;
    f->prev_root_z = f->root_z;
}

// AttackVelocity (0x80040CF8): the direction of the first attack segment.
void Combat::attack_velocity(const gd::Ref<FighterState>& f) {
    gd::PI32 s = f->attack_segs[0];
    f->atk_dir_x = Fx::w32(s[3] - s[0]);
    f->atk_dir_y = Fx::w32(s[4] - s[1]);
    f->atk_dir_z = Fx::w32(s[5] - s[2]);
}

// ---- collision shapes

// CollisionShapesUpdate (0x80042204): attack segments (baked inside the active window, from the
// joints outside it), the hurt cylinders and the body spheres.
void Combat::collision_shapes(const gd::Ref<FighterState>& f, const gd::Arr<gd::Ref<JointFrame>>& joints) {
    gd::Ref<MoveRow> pose = f->pose_move;
    if (pose->active_first <= f->pose_frame && f->pose_frame <= pose->active_last) {
        _attack_segments_baked(f);
    } else {
        for (const WeaponPoint& w : WEAPON_POINTS) {
            if (f->bank_type != w.bank_type || (w.character >= 0 && f->char_id != w.character)) {
                continue;
            }
            gd::Ref<JointFrame> joint = joints[w.joint];
            gd::PI32 point = w.point;
            gd::PI32 p = point.duplicate();
            gd::PI32 m = joint->rot;
            gd::PI32 out{0, 0, 0};
            for (gd::Int r = 0; r < 3; ++r) {
                out[r] = Fx::w32(Fx::w32((m[3 * r] * p[0] + m[3 * r + 1] * p[1] + m[3 * r + 2] * p[2]) >> 12) + joint->t[r]);
            }
            // A new frame: the one composed this step stays in FighterBody.draw_joints.
            gd::Int target = w.target;
            joints[target] = gd::make<JointFrame>(joints[target]->rot, out);
        }
        gd::PBytes d = AttackRecords::header(pose, t);
        for (gd::Int k = 0; k < f->attack_seg_count; ++k) {
            gd::PI32 seg = f->attack_segs[k];
            gd::Int a = _segment_joint(f, d[2 * k]);
            if (a >= 0) {
                gd::PI32 p = joints[a]->t;
                seg[3] = p[0];
                seg[4] = p[1];
                seg[5] = p[2];
            }
            gd::Int b = d[2 * k + 1];
            if (b != 0) {
                b = _segment_joint(f, b);
                if (b >= 0) {
                    gd::PI32 q = joints[b]->t;
                    seg[0] = q[0];
                    seg[1] = q[1];
                    seg[2] = q[2];
                }
            }
        }
    }
    for (gd::Int k = 0; k < 14; ++k) {
        gd::PI32 p = joints[t->hurt_zone_joints[k]]->t;
        gd::PI32 zone = f->hurt_zones[k];
        zone[0] = p[0];
        gd::Int lift = HURT_ZONE_Y.get(k, 0);
        zone[1] = Fx::w32(p[1] + lift);
        zone[2] = p[2];
    }
    for (gd::Int k = 0; k < 8; ++k) {
        gd::PI32 p = joints[t->body_sphere_joints[k]]->t;
        gd::PI32 sphere = f->body_points[k];
        sphere[0] = p[0];
        sphere[1] = p[1];
        sphere[2] = p[2];
    }
    f->prev_hurt_zone = f->hurt_zones[0].slice(0, 3);
}

gd::Int Combat::_segment_joint(const gd::Ref<FighterState>& f, gd::Int joint) {
    if (joint == 0x13) {
        return f->char_id == Character::OGRE ? 6 : joint;
    }
    return joint < 0x18 ? joint : -1;
}

// FUN_8006A6A4: the attack segments inside the active window from the baked points.
void Combat::_attack_segments_baked(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> move = f->pose_move;
    if (f->attack_seg_count == 0 || !AttackRecords::has_descriptor(move)) {
        return;
    }
    gd::PBytes data = AttackRecords::bytes_of(move, t);
    gd::Int desc = AttackRecords::offset_of(move, t);
    if (data[desc] > 0x17) {
        return;
    }
    gd::Int i = f->index;
    gd::PI32 seg0 = f->attack_segs[0];
    gd::PI32 seg1 = f->attack_segs[1];
    gd::Int delta = Fx::w32(f->heading - f->facing);
    gd::PI32 swing;
    if (delta != 0) {
        gd::Vector2i sc = _trig_neg16(delta);
        swing = gd::PI32{sc.x, sc.y, delta, f->root_x, f->root_y, f->root_z};
    }
    gd::Vector2i trig = _trig_neg16(f->heading);
    gd::Int flags = gd::Int(data[desc + 4]) << 8 | gd::Int(data[desc + 5]);
    gd::Int extra = 0;
    switch (gd::Int(fight->alt_points[i])) {
        case 1:
            extra = (flags & 0x7FFF) >> 4;
            break;
        case 2:
            extra = (data[desc + 4] & 0x80) != 0 ? (flags & 0x7FFE) >> 3 : 0;
            break;
        case 0:
            extra = 0;
            break;
        default:
            return;
    }
    gd::Int kind = flags & 0xF;
    gd::Int frame = f->pose_frame - move->active_first;
    if (!LAYOUT_STRIDE.has(kind)) {
        return;
    }
    gd::Vector2i layout = LAYOUT_STRIDE.at(kind);
    gd::Int p = desc + 6 + gd::Int(layout.x) * frame + layout.y + extra;
    gd::PI32 cur{f->pos_x, f->pos_y, f->pos_z};
    gd::PI32 prev = fight->segment_anchor[i];
    gd::Int q = 0;
    if (gd::Arr<gd::Int>{1, 2, 4, 6, 7}.has(kind)) {
        if (frame != 0) {
            _copy(seg0, 0, fight->segment_end0[i], 0);
        } else {
            _place(seg0, 0, _unpack(data, p - (kind == 2 ? 10 : 5)), prev, trig, swing);
        }
        _place(seg0, 3, _unpack(data, p), cur, trig, swing);
        q = p + 5;
    } else {
        _place(seg0, 0, _unpack(data, p), cur, trig, swing);
        _place(seg0, 3, _unpack(data, p + 5), cur, trig, swing);
        q = p + 10;
    }
    switch (kind) {
        case 2:
        case 3:
            if (frame != 0) {
                _copy(seg1, 0, fight->segment_end1[i], 0);
            } else {
                _place(seg1, 0, _unpack(data, q - (kind == 2 ? 10 : 15)), prev, trig, swing);
            }
            _place(seg1, 3, _unpack(data, q), cur, trig, swing);
            break;
        case 4:
        case 5:
            _place(seg1, 0, _unpack(data, q), cur, trig, swing);
            _place(seg1, 3, _unpack(data, q + 5), cur, trig, swing);
            break;
        case 6:
        case 10:
            _place(seg1, 0, _unpack(data, q), cur, trig, swing);
            _copy(seg1, 3, seg0, 3);
            break;
        case 7:
            _copy(seg1, 0, seg0, 3);
            _place(seg1, 3, _unpack(data, q), cur, trig, swing);
            break;
        case 9:
        case 12:
            if (kind == 12) {
                _place(seg1, 0, _unpack(data, q), cur, trig, swing);
            } else if (frame == 0) {
                _place(seg1, 0, _unpack(data, q - 5), prev, trig, swing);
            } else {
                _copy(seg1, 0, fight->segment_end1[i], 0);
            }
            _copy(seg1, 3, seg0, 0);
            break;
        case 13:
            _copy(seg1, 0, seg0, 0);
            _place(seg1, 3, _unpack(data, q), cur, trig, swing);
            break;
        case 14:
            _place(seg1, 3, _unpack(data, q), cur, trig, swing);
            _copy(seg1, 0, seg1, 3);
            break;
        default:
            break;
    }
}

void Combat::_copy(const gd::PI32& dst, gd::Int at, const gd::PI32& src, gd::Int from) {
    for (gd::Int k = 0; k < 3; ++k) {
        dst[at + k] = src[from + k];
    }
}

// sin and cos of -angle (16-bit angle units, the plain int), 4.12.
gd::Vector2i Combat::_trig_neg16(gd::Int angle) {
    gd::Int a = -angle;
    gd::Int idx = ((a < 0 ? a + 0xF : a) >> 4) & 0xFFF;
    return gd::Vector2i(FightMath::sin12(idx, t), FightMath::cos12(idx, t));
}

// FUN_800699FC: a 40-bit point, x and y 13 bits (bias 0x1000), z 14 bits (bias 0x2000).
gd::PI32 Combat::_unpack(const gd::PBytes& data, gd::Int at) {
    gd::Int v = 0;
    for (gd::Int k = 0; k < 5; ++k) {
        v |= gd::shl(at + k < data.size() ? gd::Int(data[at + k]) : 0, 8 * k);
    }
    return gd::PI32{(v & 0x1FFF) - 0x1000, ((v >> 13) & 0x1FFF) - 0x1000, (v >> 26) - 0x2000};
}

// FUN_8006A518: origin (swung about the root by heading - facing) + point rotated by -heading.
void Combat::_place(const gd::PI32& out, gd::Int at, const gd::PI32& p, const gd::PI32& origin,
                    gd::Vector2i trig, const gd::PI32& swing) {
    gd::Int ox = origin[0];
    gd::Int oy = origin[1];
    gd::Int oz = origin[2];
    if (!swing.is_empty()) {
        gd::Int s = swing[0];
        gd::Int c = swing[1];
        gd::Int dx = Fx::w32(swing[3] - ox);
        gd::Int dz = Fx::w32(swing[5] - oz);
        ox = Fx::w32(swing[3] - Fx::trunc12(Fx::w32(dx * c - dz * s)));
        oz = Fx::w32(swing[5] - Fx::trunc12(Fx::w32(dx * s + dz * c)));
    }
    gd::Int sn = trig.x;
    gd::Int cs = trig.y;
    out[at] = Fx::w32(ox + Fx::trunc12(Fx::w32(p[0] * cs)) - Fx::trunc12(Fx::w32(p[2] * sn)));
    out[at + 1] = Fx::w32(oy + p[1]);
    out[at + 2] = Fx::w32(oz + Fx::trunc12(Fx::w32(p[0] * sn)) + Fx::trunc12(Fx::w32(p[2] * cs)));
}

// BodySphereProfile (0x8003EEFC): the body sphere radii by the move's profile (+0x30).
void Combat::body_sphere_profile(const gd::Ref<FighterState>& f) {
    gd::Ref<MoveRow> pose = f->pose_move;
    gd::Int profile = pose->body_profile;
    gd::PI32 radii = t->body_radii[f->char_id];
    gd::PI32 r;
    r.resize(8);
    switch (profile & 0xF) {
        case 0: {
            bool drop = false;
            if (((profile >> 12) & 1) == 0 || pose->active_first == 0) {
                drop = true;
            } else if (pose->active_first - 8 < f->pose_frame) {
                drop = f->pose_frame < pose->active_last;
            }
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = drop && (gd::shr(Fx::s16(profile), k + 4) & 1) != 0 ? 0 : gd::Int(radii[k]);
            }
            break;
        }
        case 1:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = k == 0 ? gd::Int(radii[0]) : k == 3 ? gd::Int(radii[3]) : 1;
            }
            break;
        case 2:
            r.fill(1);
            break;
        case 3:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = radii[k] >> 1;
            }
            break;
        case 4:
            r.fill(0);
            break;
        case 5:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = k == 0 ? radii[0] >> 1 : k == 3 ? radii[3] >> 1 : 1;
            }
            break;
        case 6:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = k == 0 ? radii[0] + 0x3C : k == 4 || k == 5 || k == 6 || k == 7 ? 1 : gd::Int(radii[k]);
            }
            break;
        case 7:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = radii[k] + 100;
            }
            break;
        default:
            for (gd::Int k = 0; k < 8; ++k) {
                r[k] = radii[k];
            }
            break;
    }
    for (gd::Int k = 0; k < 8; ++k) {
        bool zero = false;
        if (pose->active_first == 0) {
            if (f->in_air == 0 || f->was_hit_this_move == 0) {
                if ((pose->flags & MoveFlag::BODY_ADJUST) != 0 && (k >= 6 || k == 4 || k == 5)) {
                    zero = true;
                }
            } else if (k >= 6) {
                zero = true;
            }
        } else if (k == 1 || k == 2) {
            zero = true;
        }
        f->body_points[k][3] = zero ? 0 : Fx::s16(r[k]);
    }
}

// ---- body separation

// FUN_80043F40: clears the contact flags; a fighter in a throw and its partner are not pushed.
void Combat::clear_contacts() {
    // FUN_80043F40: the third record too in Tekken Ball (the ball's attacker) and Tekken Force.
    gd::Arr<gd::Ref<FighterState>> records = fight->mode == GameMode::BALL || fight->mode == GameMode::FORCE
        ? fight->fighters.slice(0, FightState::RECORDS) : fight->active();
    for (const gd::Ref<FighterState>& f : records) {
        f->body_contact = 0;
        f->no_body_push = 0;
        for (gd::Int k = 0; k < 3; ++k) {
            f->body_contact_with[k] = 0;
        }
    }
    for (const gd::Ref<FighterState>& f : records) {
        if (f->throw_state != 0) {
            f->no_body_push = 1;
            fight->fighters[f->throw_partner]->no_body_push = 1;
        }
    }
}

// BodySeparate (0x8004401C).
void Combat::body_separate(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b) {
    gd::Int pair = fight->pair_distance[_pair(a->index, b->index)] & 0xFFFFFFFFLL;
    bool solid = a->invulnerable != 1 && a->active != 0 && b->invulnerable != 1 && b->active != 0;
    if (!(pair < SEPARATION_RANGE + 1 && solid && (a->no_body_push == 0 || b->no_body_push == 0))) {
        return;
    }
    _overlap_solve(a, b);
    // `for pair_f: Array in [[a, b], [b, a]]`: (fighter, other) pairs.
    gd::Arr<gd::Arr<gd::Ref<FighterState>>> pairs{gd::Arr<gd::Ref<FighterState>>{a, b},
                                                  gd::Arr<gd::Ref<FighterState>>{b, a}};
    for (const gd::Arr<gd::Ref<FighterState>>& pair_f : pairs) {
        gd::Ref<FighterState> f = pair_f[0];
        gd::Ref<FighterState> other = pair_f[1];
        if (f->body_push_x != 0 || f->body_push_y != 0 || f->body_push_z != 0) {
            f->body_contact = 1;
            f->body_contact_with[other->index] = 1;
            if (fight->mode == GameMode::FORCE && fight->throw_count > 1) {
                // Tekken Force during throws: a pushable body is pushed twice as far, the other not.
                if (f->no_body_push == 0) {
                    f->body_push_x = Fx::w32(f->body_push_x << 1);
                    f->body_push_y = Fx::w32(f->body_push_y << 1);
                    f->body_push_z = Fx::w32(f->body_push_z << 1);
                } else {
                    f->body_push_x = 0;
                    f->body_push_y = 0;
                    f->body_push_z = 0;
                }
            }
            f->pos_x = Fx::w32(f->pos_x + f->body_push_x);
            f->pos_y = Fx::w32(f->pos_y + f->body_push_y);
            f->pos_z = Fx::w32(f->pos_z + f->body_push_z);
            f->root_x = Fx::w32(f->root_x + f->body_push_x);
            f->root_y = Fx::w32(f->root_y + f->body_push_y);
            f->root_z = Fx::w32(f->root_z + f->body_push_z);
        }
    }
}

gd::Int Combat::_axis_angle(gd::Int facing) {
    return 0x400 - Fx::div_trunc(Fx::s16(facing), 16);
}

gd::Int Combat::_deepest_overlap(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b) {
    gd::Int best = 0;
    for (const gd::PI32& pa : a->body_points) {
        gd::Int ar = pa[3] & 0xFFFF;
        if (ar == 0) {
            continue;
        }
        for (const gd::PI32& pb : b->body_points) {
            gd::Int br = pb[3] & 0xFFFF;
            if (br == 0) {
                continue;
            }
            gd::Int reach = ar + br;
            gd::Int dx = Fx::s16(pb[0] - pa[0]);
            gd::Int dy = Fx::s16(pb[1] - pa[1]);
            gd::Int dz = Fx::s16(pb[2] - pa[2]);
            if (gd::absi(dx) < (reach & 0xFFFF) && gd::absi(dy) < (reach & 0xFFFF) && gd::absi(dz) < (reach & 0xFFFF)) {
                gd::Int d = FightMath::sqrt_table(Fx::w32(dx * dx + dy * dy + dz * dz), t);
                if (Fx::s16(best) < Fx::s16(reach - d)) {
                    best = (reach - d) & 0xFFFF;
                }
            }
        }
    }
    return Fx::s16(best);
}

// BodyOverlapSolve (0x80046FCC): fills bodyPushX/Y/Z of both fighters.
void Combat::_overlap_solve(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b) {
    gd::Arr<gd::Ref<FighterState>> both{a, b};
    for (const gd::Ref<FighterState>& f : both) {
        f->body_push_x = 0;
        f->body_push_y = 0;
        f->body_push_z = 0;
    }
    gd::Int overlap = _deepest_overlap(a, b);
    bool charging = false;
    if (a->pose_move->active_first != 0 && b->pose_move->active_first != 0) {
        if (overlap < 1 && gd::absi(a->root_y - b->root_y) > HEIGHT_GAP) {
            return;
        }
        gd::Arr<gd::Vector2i> fronts;
        for (const gd::Ref<FighterState>& f : both) {
            gd::Int angle = _axis_angle(f->facing) & 0xFFF;
            fronts.append(gd::Vector2i(Fx::trunc12(FightMath::cos12(angle, t) * FRONT_REACH),
                                       Fx::trunc12(FightMath::sin12(angle, t) * FRONT_REACH)));
        }
        gd::Int gx = Fx::w32(b->root_x + fronts[1].x - (a->root_x + fronts[0].x));
        gd::Int gz = Fx::w32(b->root_z + fronts[1].y - (a->root_z + fronts[0].y));
        if (gd::absi(gx) < FRONT_NEAR && gd::absi(gz) < FRONT_NEAR
                && Fx::w32(Fx::w32((b->pos_x - a->pos_x) * gx) + Fx::w32((b->pos_z - a->pos_z) * gz)) < 0) {
            charging = true;
            if (overlap < CHARGE_OVERLAP) {
                overlap = CHARGE_OVERLAP;
            }
        }
    }
    if (overlap <= 0) {
        return;
    }
    gd::PI32 moved;
    for (const gd::Ref<FighterState>& f : both) {
        gd::PI32 prev = fight->prev_roots[f->index];
        gd::Int dx = Fx::s16(f->root_x - (prev[0] & 0xFFFF));
        gd::Int dy = Fx::s16(f->root_y - (prev[1] & 0xFFFF));
        gd::Int dz = Fx::s16(f->root_z - (prev[2] & 0xFFFF));
        moved.append(FightMath::sqrt_table(Fx::w32(dx * dx + dy * dy + dz * dz), t) & 0xFFFF);
    }
    gd::Int toward = CameraMath::atan2_units4096(Fx::w32(b->root_x - a->root_x), Fx::w32(b->root_z - a->root_z), t->camera);
    gd::PI32 axes;
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Ref<FighterState> f = k == 0 ? a : b;
        gd::Int target = k == 0 ? toward : Fx::s16(toward + 0x800);
        gd::Int angle = _axis_angle(f->facing);
        gd::Int off = (angle - target) & 0xFFF;
        if (off > 0x7FF) {
            off = 0x1000 - off;
        }
        if (off > 0x400) {
            angle = 0xC00 - Fx::div_trunc(Fx::s16(f->facing), 16);
        }
        axes.append(angle & 0xFFF);
    }
    gd::Int d1 = moved[0];
    gd::Int d2 = moved[1];
    gd::Int w1 = d1;
    gd::Int w2 = d2;
    if (d1 == 0 && d2 == 0) {
        w1 = 1;
        w2 = 1;
    }
    gd::Int total = w1 + w2;
    gd::Int share_a = Fx::div_trunc(Fx::w32(overlap * w1), total);
    gd::Int share_b = Fx::div_trunc(Fx::w32(overlap * w2), total);
    gd::Int cos0 = FightMath::cos12(axes[0], t);
    gd::Int cos1 = FightMath::cos12(axes[1], t);
    gd::Int sin0 = FightMath::sin12(axes[0], t);
    gd::Int sin1 = FightMath::sin12(axes[1], t);
    gd::Int ax = a->root_x + Fx::trunc12(cos1 * d2) - Fx::trunc12(cos0 * share_a);
    gd::Int az = a->root_z + Fx::trunc12(sin1 * d2) - Fx::trunc12(sin0 * share_a);
    gd::Int bx = b->root_x + Fx::trunc12(cos0 * d1) - Fx::trunc12(cos1 * share_b);
    gd::Int bz = b->root_z + Fx::trunc12(sin0 * d1) - Fx::trunc12(sin1 * share_b);
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Ref<FighterState> f = k == 0 ? a : b;
        gd::Int px = Fx::w32((k == 0 ? ax : bx) - f->root_x);
        gd::Int pz = Fx::w32((k == 0 ? az : bz) - f->root_z);
        f->body_push_x = charging ? px : px >> 1;
        f->body_push_z = charging ? pz : pz >> 1;
        f->body_push_y = 0;
    }
}

// ---- hits

// HitClearSlots (0x800442A0).
void Combat::clear_hit_slots(const gd::Ref<FighterState>& f) {
    f->contact = 0;
    f->whiffed = 0;
    f->got_hit = 0;
    f->hit_freeze_in = 0;
    f->forced_hit_in = 0;
    for (const gd::Ref<HitSlot>& s : f->hit_slots) {
        s->clear();
    }
}

// HitTest (0x80044304). A forced hit (Tekken Force) lands on a CPU defender it touches, at the
// defender's root, without the segment test.
void Combat::hit_test(const gd::Ref<FighterState>& attacker, const gd::Ref<FighterState>& defender,
                      const gd::Ref<SimEvents>& events) {
    gd::Ref<HitSlot> slot = nullptr;
    for (const gd::Ref<HitSlot>& s : defender->hit_slots) {
        if (s->used == 0) {
            slot = s;
        }
    }
    gd::Int done = defender->index;
    if (slot != nullptr && attacker->invulnerable == 0 && defender->invulnerable == 0 && attacker->active != 0
            && defender->active != 0 && attacker->hit_done[done] == 0) {
        gd::Ref<MoveRow> pose = defender->pose_move;
        bool skip = attacker->attack == LOW_ATTACK && defender->state_class == JUMP_CLASS
            && ((pose->air_first <= defender->pose_frame && defender->pose_frame <= pose->air_last - 5) || defender->in_air != 0);
        if (!skip && !((fight->mode == GameMode::BALL || fight->mode == GameMode::FORCE) && defender->in_throw != 0)) {
            gd::Int level = attacker->attack | (defender->step_kind == 2 ? 1 : 0);
            bool hit = false;
            if (attacker->forced_hit == 0 || defender->is_cpu == 0 || defender->body_contact_with[attacker->index] == 0) {
                hit = _hit_segments(attacker, defender, slot);
            } else {
                gd::PI32 a{attacker->root_x, attacker->root_y, attacker->root_z};
                gd::PI32 d{defender->root_x, defender->root_y, defender->root_z};
                for (gd::Int k = 0; k < 3; ++k) {
                    slot->point[k] = d[k];
                    slot->direction[k] = Fx::s16(Fx::w32(a[k] - d[k]) >> 1);
                }
                hit = true;
                events->add(SimEvents::Kind::SOUND, 0, FORCED_HIT_SOUND, 0);
            }
            if (hit && (level & defender->state & StateBit::POSTURE) != 0) {
                attacker->hit_done[done] = 1;
                attacker->contact_this_move = 1;
                attacker->contact = 1;
                attacker->last_hit_target = defender->index;
                defender->was_hit_this_move = 1;
                defender->got_hit = 1;
                defender->hit_freeze_in = attacker->pose_move->hit_freeze;
                slot->used = 1;
                slot->attacker = attacker->index;
                defender->forced_hit_in = attacker->forced_hit;
                if (attacker->cur_slot == 0x898) {
                    defender->hit_freeze_in = 0x1E;
                }
                if (attacker->damage != 0 || defender->in_air != 0) {
                    defender->hit_cooldown = 4;
                }
            }
        }
    }
    if (attacker->pose_move->active_last == attacker->pose_frame && attacker->contact_this_move == 0) {
        attacker->whiffed = 1;
    }
}

// HitTestSegments (0x80047E54): the attacker's live segments against the defender's cylinders.
// Projectile descriptors (first joint >= 0x18) use the segments their owner's effect objects
// published this frame (fire, breath and gas clouds) instead.
bool Combat::_hit_segments(const gd::Ref<FighterState>& attacker, const gd::Ref<FighterState>& defender,
                           const gd::Ref<HitSlot>& slot) {
    gd::PBytes d = AttackRecords::header(attacker->pose_move, t);
    gd::Arr<gd::PI32> segments = attacker->attack_segs;
    gd::Int count = gd::mini(attacker->active_segs, 4);
    if (d[0] >= PROJECTILE_JOINT) {
        segments = fight->effects->segments[gd::mini(attacker->index, 1)];
        count = segments.size();
    }
    for (gd::Int k = 0; k < count; ++k) {
        if (defender->hit_cooldown != 0) {
            return false;
        }
        gd::PI32 s = segments[k];
        for (gd::Int z = 0; z < 14; ++z) {
            if (FightMath::segment_hits_cylinder(s, defender->hurt_zones[z], fight->rules->fix_segment_hit_overflow)) {
                slot->zone = z;
                for (gd::Int c = 0; c < 3; ++c) {
                    slot->point[c] = s[3 + c];
                    slot->direction[c] = Fx::s16(s[3 + c] - s[c]);
                }
                return true;
            }
        }
    }
    return false;
}

// HitExtraDamage (0x80044C4C).
gd::Int Combat::_extra_damage(const gd::Ref<FighterState>& f) {
    gd::Int total = 0;
    gd::Int own = f->extra_damage != 0 ? f->extra_damage : f->damage;
    if (f->extra_kind == 2) {
        total = own;
    }
    if (f->extra_kind == 3) {
        total -= own;
    }
    if (f->throw_state < 0) {
        gd::Ref<FighterState> opp = fight->opponent(f);
        if (opp->extra_kind == 1) {
            total += opp->extra_damage != 0 ? opp->extra_damage : opp->damage;
        }
    }
    return total;
}

// HitClassify (0x80044D24).
void Combat::_classify(const gd::Ref<FighterState>& defender, const gd::Ref<FighterState>& attacker,
                       const gd::Ref<HitSlot>& slot) {
    if (attacker->damage == 0) {
        slot->no_damage = 1;
        return;
    }
    gd::Int close = attacker->pose_move->close_reaction;
    if (close != 0 && (gd::Int(fight->fighter_distance) & 0xFFFFFFFFLL) < (t->close_reactions[close][1] & 0xFFFFFFFFLL)) {
        slot->close = 1;
    }
    if ((defender->guard & attacker->attack) == 0 || (defender->rel_angle & 0xFFFF) > 0x3FFF) {
        slot->hit = 1;
        bool startup = defender->damage != 0 && defender->pose_frame < defender->pose_move->active_first;
        if (startup || defender->power_timer != 0 || attacker->power_timer != 0) {
            slot->counter = 1;
        }
        if (defender->in_air != 0 || (defender->pose_move->state & StateBit::AIRBORNE) != 0) {
            slot->airborne = 1;
        }
    } else {
        slot->guarded = 1;
        if (fight->chip_damage != 0 || attacker->power_timer != 0) {
            slot->chip = 1;
        }
    }
}

// HitDamage (0x80044E70): the attacker's base damage into the slot and the hit's damage added.
void Combat::_damage(const gd::Ref<FighterState>& defender, const gd::Ref<FighterState>& attacker,
                     const gd::Ref<HitSlot>& slot) {
    slot->base_damage = attacker->damage & 0xFFFF;
    gd::Int base = Fx::s16(attacker->damage);
    gd::Int d = 0;
    if (slot->guarded != 0) {
        d = slot->chip != 0 ? Fx::s16(Fx::div_trunc(base, 10) + 2) : 0;
    } else {
        if (slot->close != 0) {
            d = base + Fx::div_trunc(base, 2);
        } else {
            d = Fx::s16(Fx::div_trunc(base * FightMath::ZONE_PERCENT[slot->zone], 100));
        }
        if (slot->counter != 0) {
            gd::Int e = Fx::div_trunc(gd::absi(Fx::s16(defender->damage)) * 3, 10);
            if (defender->power_timer == 0 && attacker->power_timer == 0) {
                gd::Int w = defender->pose_move->active_first;
                if (w == 0 || w < defender->pose_frame) {
                    w = defender->pose_frame & 0xFFFF;
                }
                e = Fx::div_trunc(Fx::div_trunc(Fx::s16(e) * 3, 10) * defender->pose_frame, Fx::s16(w));
            }
            d = e + Fx::div_trunc(Fx::s16(d) * 12, 10);
        }
        d = Fx::s16(d);
        if (slot->airborne != 0) {
            d = defender->juggle_count == 0 ? Fx::s16(Fx::div_trunc(d * 80, 100)) : Fx::s16(Fx::div_trunc(d, 2));
        }
    }
    slot->damage = Fx::s16(slot->damage + d);
}

// HitApply (0x80044634): classification, damage, the best slot, the reaction record and KO.
void Combat::hit_apply(const gd::Ref<FighterState>& f, const gd::Ref<SimEvents>& events) {
    gd::Ref<FighterState> opp = fight->opponent(f);
    gd::Int extra = _extra_damage(f);
    gd::Int total = 0;
    for (const gd::Ref<HitSlot>& slot : f->hit_slots) {
        if (slot->used != 0) {
            f->fixed_facing = 0;
            opp = fight->fighters[slot->attacker];
            moves->relative_angles(f, opp);
            _classify(f, opp, slot);
            _damage(f, opp, slot);
            total += slot->damage;
        }
    }
    if (f->got_hit != 0) {
        gd::Int best = 0;
        gd::Ref<HitSlot> other = f->hit_slots[1];
        if (other->used != 0) {
            gd::Int a = f->hit_slots[0]->score();
            gd::Int b = other->score();
            if (a < b || (a == b && f->hit_slots[0]->damage < other->damage)) {
                best = 1;
            }
        }
        f->best_hit_slot = best;
        gd::Ref<HitSlot> s = f->hit_slots[best];
        f->last_damage = gd::absi(s->damage);
        f->close_hit = s->close;
        f->guarded = s->guarded;
        f->hit_clean = s->hit;
        f->counter_hit = s->counter;
        // Practice's COUNTER ATTACKS: every clean hit outside a reaction counts as a counter hit.
        if (fight->mode == GameMode::PRACTICE && fight->counter_forced[f->index] != 0 && f->in_reaction == 0 && f->hit_clean != 0) {
            f->counter_hit = 1;
        }
        f->last_attacker = s->attacker;
        f->hit_dir_x = opp->atk_dir_x;
        f->hit_dir_y = opp->atk_dir_y;
        f->hit_dir_z = opp->atk_dir_z;
        f->reaction_throw_slot = -1;
        if (fight->mode == GameMode::BALL && fight->ball->charged_hit()) {
            // Tekken Ball's charged hit (0x800B6B54, read past Ghidra's decompile): the reaction
            // of the ball's attacker, the third record.
            _set_reaction(f, fight->fighters[TekkenBall::THIRD]->pose_move->reaction);
        } else if (f->forced_hit_in != 0) {
            f->reaction = t->forced_reaction;
        } else if (f->close_hit == 0) {
            _set_reaction(f, opp->pose_move->reaction);
        } else {
            _set_reaction(f, t->close_reactions[opp->pose_move->close_reaction][0] & 0xFFFF);
        }
    }
    gd::Int old = f->health;
    if (fight->no_damage == 0 && fight->practice == 0) {
        f->health = Fx::w32(old - (extra + total) * 0x10000);
    }
    if (fight->mode == GameMode::BALL && opp->index != TekkenBall::THIRD) {
        // Tekken Ball: only the ball (the third record) does damage.
        f->health = old;
    }
    if (f->health < 0) {
        f->health = 0;
    }
    if (f->health_max < f->health) {
        f->health = f->health_max;
    }
    if (f->health == 0 && old != 0) {
        f->ko = 1;
    }
    if (f->ko != 0 && f->hit_slots[f->best_hit_slot]->chip != 0) {
        f->guarded = 0;
        f->hit_clean = 1;
    }
    if (f->got_hit != 0) {
        _spawn_effect(f, f->hit_slots[f->best_hit_slot], Fx::s16(total), events);
    }
    if (f->ko != 0 && fight->ko_started == 0 && fight->mode != GameMode::FORCE) {
        fight->replay->start_ko(opp);
        fight->ko_started = 1;
    }
    if (fight->mode == GameMode::FORCE && f->is_cpu != 0) {
        _force_score(f, extra);
    }
    f->last_extra_damage = Fx::s16(extra);
}

// Tekken Force: the damage an enemy took from the player or a forced hit, and twice the extra
// damage, scored.
void Combat::_force_score(const gd::Ref<FighterState>& f, gd::Int extra) {
    gd::Int score = 0;
    if (f->got_hit != 0) {
        gd::Ref<FighterState> player = fight->force->player();
        for (const gd::Ref<HitSlot>& slot : f->hit_slots) {
            if (slot->used != 0) {
                gd::Ref<FighterState> attacker = fight->fighters[slot->attacker];
                if (attacker == player || attacker->forced_hit != 0) {
                    score += Fx::s16(slot->base_damage);
                }
            }
        }
    }
    if (extra > 0) {
        score += extra * 2;
    }
    if (score != 0) {
        fight->force->add_score(score);
    }
}

// The reaction of a move's +0x32 word: a record below 0x1000, else a throw victim entry
// (only its first field, the victim's move slot, is meaningful).
void Combat::_set_reaction(const gd::Ref<FighterState>& f, gd::Int value) {
    if ((value & 0xF000) == 0) {
        f->reaction = t->reactions[value];
    } else {
        gd::Int n = value & 0xFFF;
        f->reaction = t->throw_victim_slots.slice(n, n + FightTables::REACTION_WORDS);
        f->reaction_throw_slot = n;
    }
}

void Combat::_spark_event(const gd::Ref<SimEvents>& events, const gd::Ref<FighterState>& f,
                          const gd::Ref<HitSlot>& slot, gd::Int kind, gd::Int damage, gd::Int side) {
    gd::Ref<SimEvents::Event> e = events->add_at(SimEvents::Kind::HIT_SPARK, f->index, slot->point.duplicate(), kind, damage);
    e->c = side;
    e->vector = slot->direction.duplicate();
}

// HitSpawnEffect (0x80044B78): the guard or hit flash and spark at the contact point.
void Combat::_spawn_effect(const gd::Ref<FighterState>& f, const gd::Ref<HitSlot>& slot, gd::Int total,
                           const gd::Ref<SimEvents>& events) {
    if (slot->hit == 0 && f->ko == 0) {
        if (slot->guarded != 0) {
            _spark_event(events, f, slot, 2, 0, slot->attacker);
            fight->replay->log_effect(3, 2, slot->point, slot->direction, 0);
        } else if (slot->no_damage != 0) {
            _spark_event(events, f, slot, 1, 0, slot->attacker);
            fight->replay->log_effect(3, 1, slot->point, slot->direction, 0);
        }
        return;
    }
    gd::Int damage = gd::maxi(total, Fx::s16(slot->base_damage));
    // Tekken Ball flips the spark's side by the defender (the attacker may be the ball's record).
    gd::Int side = fight->mode == GameMode::BALL ? 1 - f->index : slot->attacker;
    _spark_event(events, f, slot, 0, damage, side);
    fight->replay->log_effect(3, 0, slot->point, slot->direction, 0);
    if (damage >= EffectObjects::SPARK_DAMAGE) {
        fight->replay->log_effect(side, 0, slot->point, slot->direction, 0);
    }
    EffectObjects::hit_objects(fight, damage, f, side, slot->point, events);
}

}  // namespace t3
