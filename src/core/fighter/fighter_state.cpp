// remake/core/fighter/fighter_state.gd
// The simulation's record of one fighter (fighter.md). Field names are fighter.md's in
// snake_case; the comment gives the record offset.
#include "core/fighter/fighter_state.h"

#include "content/fighter_tables.h"
#include "core/combat/hit_slot.h"
#include "core/fighter/fighter_hands.h"

namespace t3 {

gd::Int FighterState::address(gd::Int fighter_index) {
    return RECORDS + RECORD_SIZE * fighter_index;
}

gd::Int FighterState::index_at(gd::Int record_address) {
    return (record_address - RECORDS) / RECORD_SIZE;
}

FighterState::FighterState() {
    // Object defaults, in declaration order.
    hit_slots = gd::Arr<gd::Ref<HitSlot>>{gd::make<HitSlot>(), gd::make<HitSlot>()};
    hands = gd::make<FighterHands>();

    for (gd::Int i = 0; i < 4; ++i) {
        attack_segs.append(gd::PI32{0, 0, 0, 0, 0, 0});
    }
    for (gd::Int i = 0; i < 14; ++i) {
        hurt_zones.append(gd::PI32{0, 0, 0, 0, 0});
    }
    for (gd::Int i = 0; i < 8; ++i) {
        body_points.append(gd::PI32{0, 0, 0, 0});
    }
    in_history.resize(60);
    in_pressed_hist.resize(60);
    cmd_buffer.resize(10);
}

void FighterState::set_costume(gd::Int slot, const gd::Ref<FighterTables>& tables) {
    costume_key = tables->key_of_slot(slot);
    costume_slot = tables->costume_keys[costume_key];
    char_id = costume_key >> 2;
}

// GDScript: `for p in get_property_list()` over the script variables, skipping NOT_COPIED, then
// `set(name, _copied(other.get(name)))`. Translated as one assignment per member in declaration
// order: all 236 stored members of the header except `body`; the properties `move` and `anchor`
// (NOT_COPIED) are views of pose_move / root_move and pos_x ... pos_z, not storage. `_copied`
// duplicates the packed arrays (move_text, hit_done, body_contact_with, push_table, reaction,
// prev_hurt_zone, in_history, cmd_buffer, in_pressed_hist, press_count, blend_root_delta), the
// arrays of arrays (attack_segs, hurt_zones, body_points) element by element, hit_slots into two
// new HitSlots and hands into a new FighterHands; ints and the shared MoveRow / MotionBank refs
// (root_move, pose_move, move_row, bank, last_pose_move, blend_src_move, blend_dst_move) are
// assigned as they are.
void FighterState::copy_from(const gd::Ref<FighterState>& other) {
    player_index = _copied(other->player_index);
    costume_key = _copied(other->costume_key);
    bank_type = _copied(other->bank_type);
    char_id = _copied(other->char_id);
    voice_set = _copied(other->voice_set);
    costume_slot = _copied(other->costume_slot);
    move_text = _copied(other->move_text);
    index = _copied(other->index);
    cur_opp_index = _copied(other->cur_opp_index);
    opp_index = _copied(other->opp_index);
    throw_partner = _copied(other->throw_partner);
    last_attacker = _copied(other->last_attacker);
    last_hit_target = _copied(other->last_hit_target);
    pos_x = _copied(other->pos_x);
    pos_y = _copied(other->pos_y);
    pos_z = _copied(other->pos_z);
    tilt_x = _copied(other->tilt_x);
    facing = _copied(other->facing);
    tilt_z = _copied(other->tilt_z);
    root_dx = _copied(other->root_dx);
    root_dy = _copied(other->root_dy);
    root_dz = _copied(other->root_dz);
    target_dir = _copied(other->target_dir);
    heading = _copied(other->heading);
    heading_delta = _copied(other->heading_delta);
    rel_angle = _copied(other->rel_angle);
    facing_quadrant = _copied(other->facing_quadrant);
    aim_dir = _copied(other->aim_dir);
    track_accum = _copied(other->track_accum);
    opp_to_self_dir = _copied(other->opp_to_self_dir);
    opp_heading = _copied(other->opp_heading);
    opp_heading_delta = _copied(other->opp_heading_delta);
    opp_rel_angle = _copied(other->opp_rel_angle);
    opp_quadrant = _copied(other->opp_quadrant);
    opp_aim_dir = _copied(other->opp_aim_dir);
    round_wins = _copied(other->round_wins);
    round_won = _copied(other->round_won);
    win_pose = _copied(other->win_pose);
    root_move = _copied(other->root_move);
    root_frame = _copied(other->root_frame);
    pose_move = _copied(other->pose_move);
    pose_frame = _copied(other->pose_frame);
    event_frame = _copied(other->event_frame);
    move_frame = _copied(other->move_frame);
    damage = _copied(other->damage);
    state = _copied(other->state);
    attack = _copied(other->attack);
    guard = _copied(other->guard);
    attack_hi = _copied(other->attack_hi);
    state_class = _copied(other->state_class);
    hold_frames = _copied(other->hold_frames);
    branch_window = _copied(other->branch_window);
    frame_step = _copied(other->frame_step);
    sound_script = _copied(other->sound_script);
    sound_script_last = _copied(other->sound_script_last);
    throw_state = _copied(other->throw_state);
    hit_freeze = _copied(other->hit_freeze);
    slide_step_x = _copied(other->slide_step_x);
    slide_step_z = _copied(other->slide_step_z);
    react_chain = _copied(other->react_chain);
    slide_state = _copied(other->slide_state);
    hit_done = _copied(other->hit_done);
    was_hit_this_move = _copied(other->was_hit_this_move);
    contact_this_move = _copied(other->contact_this_move);
    guarded_prev = _copied(other->guarded_prev);
    in_reaction = _copied(other->in_reaction);
    branch_kind = _copied(other->branch_kind);
    cond_flag_used = _copied(other->cond_flag_used);
    launch_armed = _copied(other->launch_armed);
    air_kind = _copied(other->air_kind);
    ballistic = _copied(other->ballistic);
    juggle_count = _copied(other->juggle_count);
    ground_offset = _copied(other->ground_offset);
    air_speed = _copied(other->air_speed);
    air_vel_x = _copied(other->air_vel_x);
    air_vel_y = _copied(other->air_vel_y);
    air_vel_z = _copied(other->air_vel_z);
    cur_slot = _copied(other->cur_slot);
    cur_transition = _copied(other->cur_transition);
    slide_target_x = _copied(other->slide_target_x);
    slide_target_z = _copied(other->slide_target_z);
    opp_start_x = _copied(other->opp_start_x);
    opp_start_z = _copied(other->opp_start_z);
    attack_seg_count = _copied(other->attack_seg_count);
    crouch_move = _copied(other->crouch_move);
    step_kind = _copied(other->step_kind);
    attack_class = _copied(other->attack_class);
    air_phase = _copied(other->air_phase);
    track_mode = _copied(other->track_mode);
    move_flag_ba = _copied(other->move_flag_ba);
    move_flag_bb = _copied(other->move_flag_bb);
    skip_step_physics = _copied(other->skip_step_physics);
    attack_pending = _copied(other->attack_pending);
    anchor_dirty = _copied(other->anchor_dirty);
    slide_to_point = _copied(other->slide_to_point);
    reset_flag_c0 = _copied(other->reset_flag_c0);
    apply_end_turn = _copied(other->apply_end_turn);
    invulnerable = _copied(other->invulnerable);
    active = _copied(other->active);
    in_throw = _copied(other->in_throw);
    is_cpu = _copied(other->is_cpu);
    side_flag = _copied(other->side_flag);
    fixed_facing = _copied(other->fixed_facing);
    fixed_facing_side = _copied(other->fixed_facing_side);
    no_look_at = _copied(other->no_look_at);
    human_guard = _copied(other->human_guard);
    last_extra_damage = _copied(other->last_extra_damage);
    got_hit = _copied(other->got_hit);
    guarded = _copied(other->guarded);
    hit_clean = _copied(other->hit_clean);
    contact = _copied(other->contact);
    whiffed = _copied(other->whiffed);
    counter_hit = _copied(other->counter_hit);
    close_hit = _copied(other->close_hit);
    body_contact = _copied(other->body_contact);
    body_contact_with = _copied(other->body_contact_with);
    no_body_push = _copied(other->no_body_push);
    move_changed = _copied(other->move_changed);
    in_air = _copied(other->in_air);
    landed_a = _copied(other->landed_a);
    landed_b = _copied(other->landed_b);
    ko = _copied(other->ko);
    about_to_hit = _copied(other->about_to_hit);
    tap_lp = _copied(other->tap_lp);
    tap_rp = _copied(other->tap_rp);
    tap_both = _copied(other->tap_both);
    active_segs = _copied(other->active_segs);
    extra_kind = _copied(other->extra_kind);
    blend_active = _copied(other->blend_active);
    forced_hit = _copied(other->forced_hit);
    forced_hit_in = _copied(other->forced_hit_in);
    last_damage = _copied(other->last_damage);
    damage_override = _copied(other->damage_override);
    extra_damage = _copied(other->extra_damage);
    hit_freeze_in = _copied(other->hit_freeze_in);
    dist_adj = _copied(other->dist_adj);
    dist = _copied(other->dist);
    dir_x = _copied(other->dir_x);
    dir_z = _copied(other->dir_z);
    hit_cooldown = _copied(other->hit_cooldown);
    push_frames = _copied(other->push_frames);
    push_speed = _copied(other->push_speed);
    push_table_frames = _copied(other->push_table_frames);
    push_dir = _copied(other->push_dir);
    push_table = _copied(other->push_table);
    push_table_pos = _copied(other->push_table_pos);
    turn_frames = _copied(other->turn_frames);
    turn_step = _copied(other->turn_step);
    recover_mash = _copied(other->recover_mash);
    recover_frames = _copied(other->recover_frames);
    attack_alert = _copied(other->attack_alert);
    sound_script_pos = _copied(other->sound_script_pos);
    sound_script_frame = _copied(other->sound_script_frame);
    power_timer = _copied(other->power_timer);
    push_repeat = _copied(other->push_repeat);
    push_repeat_timer = _copied(other->push_repeat_timer);
    step_cooldown = _copied(other->step_cooldown);
    placed_x = _copied(other->placed_x);
    placed_z = _copied(other->placed_z);
    prev_pose_frame = _copied(other->prev_pose_frame);
    prev_slot = _copied(other->prev_slot);
    prev_move_unk10 = _copied(other->prev_move_unk10);
    push_repeat_trans = _copied(other->push_repeat_trans);
    hit_slots = _copied(other->hit_slots);
    best_hit_slot = _copied(other->best_hit_slot);
    entry_frame = _copied(other->entry_frame);
    transition = _copied(other->transition);
    trans_bit7 = _copied(other->trans_bit7);
    trans_bit6 = _copied(other->trans_bit6);
    move_slot = _copied(other->move_slot);
    move_row = _copied(other->move_row);
    reaction = _copied(other->reaction);
    reaction_throw_slot = _copied(other->reaction_throw_slot);
    attack_segs = _copied(other->attack_segs);
    hurt_zones = _copied(other->hurt_zones);
    body_points = _copied(other->body_points);
    prev_hurt_zone = _copied(other->prev_hurt_zone);
    prev_root_x = _copied(other->prev_root_x);
    prev_root_y = _copied(other->prev_root_y);
    prev_root_z = _copied(other->prev_root_z);
    vel_x = _copied(other->vel_x);
    vel_y = _copied(other->vel_y);
    vel_z = _copied(other->vel_z);
    atk_dir_x = _copied(other->atk_dir_x);
    atk_dir_y = _copied(other->atk_dir_y);
    atk_dir_z = _copied(other->atk_dir_z);
    hit_dir_x = _copied(other->hit_dir_x);
    hit_dir_y = _copied(other->hit_dir_y);
    hit_dir_z = _copied(other->hit_dir_z);
    body_push_x = _copied(other->body_push_x);
    body_push_y = _copied(other->body_push_y);
    body_push_z = _copied(other->body_push_z);
    health = _copied(other->health);
    health_max = _copied(other->health_max);
    carried_health = _copied(other->carried_health);
    health_left = _copied(other->health_left);
    in_dir = _copied(other->in_dir);
    in_pressed = _copied(other->in_pressed);
    in_held = _copied(other->in_held);
    script_input = _copied(other->script_input);
    in_hist_index = _copied(other->in_hist_index);
    cmd_count = _copied(other->cmd_count);
    cmd_read = _copied(other->cmd_read);
    cmd_merge_timer = _copied(other->cmd_merge_timer);
    in_history = _copied(other->in_history);
    cmd_buffer = _copied(other->cmd_buffer);
    in_pressed_hist = _copied(other->in_pressed_hist);
    press_count = _copied(other->press_count);
    bank = _copied(other->bank);
    // body: NOT_COPIED
    scale_base = _copied(other->scale_base);
    scale = _copied(other->scale);
    root_x = _copied(other->root_x);
    root_y = _copied(other->root_y);
    root_z = _copied(other->root_z);
    screen_x = _copied(other->screen_x);
    hands = _copied(other->hands);
    air_free = _copied(other->air_free);
    ogre_freeze = _copied(other->ogre_freeze);
    ai_slot = _copied(other->ai_slot);
    ai_target = _copied(other->ai_target);
    keep_light = _copied(other->keep_light);
    prev_root_dx = _copied(other->prev_root_dx);
    root_dy_blended = _copied(other->root_dy_blended);
    blend_mode = _copied(other->blend_mode);
    blend_mode_b = _copied(other->blend_mode_b);
    last_pose_move = _copied(other->last_pose_move);
    last_pose_frame = _copied(other->last_pose_frame);
    blend_root_delta = _copied(other->blend_root_delta);
    blend_weight = _copied(other->blend_weight);
    blend_frames = _copied(other->blend_frames);
    blend_counter = _copied(other->blend_counter);
    blend_src_move = _copied(other->blend_src_move);
    blend_dst_move = _copied(other->blend_dst_move);
    blend_src_frame = _copied(other->blend_src_frame);
    blend_dst_frame = _copied(other->blend_dst_frame);
}

gd::PI32 FighterState::_copied(const gd::PI32& v) {
    return v.duplicate();
}

gd::PBytes FighterState::_copied(const gd::PBytes& v) {
    return v.duplicate();
}

gd::PI64 FighterState::_copied(const gd::PI64& v) {
    return v.duplicate();
}

gd::Arr<gd::PI32> FighterState::_copied(const gd::Arr<gd::PI32>& v) {
    gd::Arr<gd::PI32> out = v.duplicate();
    for (gd::Int i = 0; i < out.size(); ++i) {
        out[i] = _copied(v[i]);
    }
    return out;
}

gd::Arr<gd::Ref<HitSlot>> FighterState::_copied(const gd::Arr<gd::Ref<HitSlot>>& v) {
    gd::Arr<gd::Ref<HitSlot>> out = v.duplicate();
    for (gd::Int i = 0; i < out.size(); ++i) {
        out[i] = _copied(v[i]);
    }
    return out;
}

// TYPE_OBJECT HitSlot: a new instance with every script variable copied (in declaration order).
// A null object is not `is HitSlot`, so it is returned as it is.
gd::Ref<HitSlot> FighterState::_copied(const gd::Ref<HitSlot>& v) {
    if (v == nullptr) {
        return v;
    }
    gd::Ref<HitSlot> copy = gd::make<HitSlot>();
    copy->point = _copied(v->point);
    copy->direction = _copied(v->direction);
    copy->zone = _copied(v->zone);
    copy->base_damage = _copied(v->base_damage);
    copy->damage = _copied(v->damage);
    copy->attacker = _copied(v->attacker);
    copy->used = _copied(v->used);
    copy->guarded = _copied(v->guarded);
    copy->chip = _copied(v->chip);
    copy->hit = _copied(v->hit);
    copy->counter = _copied(v->counter);
    copy->close = _copied(v->close);
    copy->airborne = _copied(v->airborne);
    copy->no_damage = _copied(v->no_damage);
    return copy;
}

// TYPE_OBJECT FighterHands: a new instance with every script variable copied (in declaration order).
gd::Ref<FighterHands> FighterState::_copied(const gd::Ref<FighterHands>& v) {
    if (v == nullptr) {
        return v;
    }
    gd::Ref<FighterHands> copy = gd::make<FighterHands>();
    copy->current = _copied(v->current);
    copy->target = _copied(v->target);
    copy->rate = _copied(v->rate);
    copy->variant = _copied(v->variant);
    copy->face = _copied(v->face);
    copy->face_speed = _copied(v->face_speed);
    copy->ogre_level = _copied(v->ogre_level);
    copy->ogre_target = _copied(v->ogre_target);
    copy->ogre_rate = _copied(v->ogre_rate);
    copy->wing_frame = _copied(v->wing_frame);
    copy->wing_phase = _copied(v->wing_phase);
    copy->wing_first = _copied(v->wing_first);
    copy->wing_variant = _copied(v->wing_variant);
    return copy;
}

}  // namespace t3
