#pragma once
// remake/core/fighter/fighter_state.gd
// The simulation's record of one fighter (fighter.md). Field names are fighter.md's in
// snake_case; the comment gives the record offset. Values keep the widths of the original
// fields: code that stores into an s16 or u8 field wraps the value (Fx.s16, & 0xFF) where the
// game's arithmetic can leave the range. Move rows are MoveRow references (null for none).
//
// A fighter record persists for the whole match: round starts reset only what the game
// resets (FUN_8002BFCC), so everything else carries over as in the original.
#include <string>

#include "gd/gd.h"

namespace t3 {

class FighterBody;
class FighterHands;
class FighterTables;
class HitSlot;
class MotionBank;
class MoveRow;

class FighterState {
public:
    static constexpr gd::Int RECORDS = 0x800A96F0LL;     // the fighter records in the game's RAM
    static constexpr gd::Int RECORD_SIZE = 0x188C;
    // The properties copy_from leaves out: the skeleton (the copy is never animated or drawn) and the
    // views of other fields (`move` would set root_move to the pose move, `anchor` is pos_x … pos_z).
    static inline const gd::Arr<std::string> NOT_COPIED{"body", "move", "anchor"};

    // A fighter record's address in the game (the pointers the game keeps in its globals).
    static gd::Int address(gd::Int fighter_index);

    // The fighter index of a record address (the inverse of `address`).
    static gd::Int index_at(gd::Int record_address);

    // ---- identity (+0x12 … +0x23)
    gd::Int player_index = 0;           // +0x12 0/1: VRAM and pad slot
    gd::Int costume_key = 0;            // +0x14 charId · 4 + costume
    gd::Int bank_type = 0;              // +0x16 motion bank type (character record +9)
    gd::Int char_id = 0;                // +0x18
    gd::Int voice_set = 0;              // +0x1A character record +8
    gd::Int costume_slot = 0;           // +0x1C
    // The costume's move list (FighterCopyMoveText: ARC member 4 at 0x800A39D0 + 0x232·player; a
    // count, then name / command strings).
    gd::PBytes move_text;
    gd::Int index = 0;                  // +0x1E fighter index
    gd::Int cur_opp_index = 1;          // +0x1F
    gd::Int opp_index = 1;              // +0x20 default opponent
    gd::Int throw_partner = 0;          // +0x21
    gd::Int last_attacker = 0;          // +0x22
    gd::Int last_hit_target = 0;        // +0x23

    // ---- placement and angles
    gd::Int pos_x = 0;                  // +0x00 anchor of the running move
    gd::Int pos_y = 0;                  // +0x04
    gd::Int pos_z = 0;                  // +0x08
    gd::Int tilt_x = 0;                 // +0x0C
    gd::Int facing = 0;                 // +0x0E
    gd::Int tilt_z = 0;                 // +0x10
    gd::Int root_dx = 0;                // +0x24 root displacement of the animation
    gd::Int root_dy = 0;                // +0x26
    gd::Int root_dz = 0;                // +0x28
    gd::Int target_dir = 0;             // +0x2A (u16 angle)
    gd::Int heading = 0;                // +0x2C
    gd::Int heading_delta = 0;          // +0x2E (u16)
    gd::Int rel_angle = 0;              // +0x30 (u16 folded)
    gd::Int facing_quadrant = 0;        // +0x32
    gd::Int aim_dir = 0;                // +0x34 (u16)
    gd::Int track_accum = 0;            // +0x36
    gd::Int opp_to_self_dir = 0;        // +0x38 (u16)
    gd::Int opp_heading = 0;            // +0x3A (u16)
    gd::Int opp_heading_delta = 0;      // +0x3C (u16)
    gd::Int opp_rel_angle = 0;          // +0x3E (u16)
    gd::Int opp_quadrant = 0;           // +0x40
    gd::Int opp_aim_dir = 0;            // +0x42 (u16)

    // ---- match
    gd::Int round_wins = 0;             // +0x44
    gd::Int round_won = 0;              // +0x46
    gd::Int win_pose = 0;               // +0x48

    // ---- running move
    gd::Ref<MoveRow> root_move;         // +0x4C
    gd::Int root_frame = 0;             // +0x50
    gd::Ref<MoveRow> pose_move;         // +0x54
    gd::Int pose_frame = 0;             // +0x58
    gd::Int event_frame = 0;            // +0x5A (s16): the pose frame when the last frame's events ran
    gd::Int move_frame = 0;             // +0x5C
    gd::Int damage = 0;                 // +0x5E
    gd::Int state = 0;                  // +0x60 (u32)
    gd::Int attack = 0;                 // +0x64 (u16)
    gd::Int guard = 0;                  // +0x66 (u16)
    gd::Int attack_hi = 0;              // +0x68
    gd::Int state_class = 0;            // +0x69
    gd::Int hold_frames = 0;            // +0x6A
    gd::Int branch_window = 0;          // +0x6C
    gd::Int frame_step = 0;             // +0x6E
    gd::Int sound_script = -1;          // +0x70: running sound script id, −1 none
    gd::Int sound_script_last = 0;      // +0x72: the last script started by the move's sound list
    gd::Int throw_state = 0;            // +0x74
    gd::Int hit_freeze = 0;             // +0x76
    gd::Int slide_step_x = 0;           // +0x78
    gd::Int slide_step_z = 0;           // +0x7C
    gd::Int react_chain = 0;            // +0x80
    gd::Int slide_state = 0;            // +0x82
    gd::PI32 hit_done{0, 0, 0, 0};      // +0x83 … +0x86 (+0x86 also set at a throw start)
    gd::Int was_hit_this_move = 0;      // +0x87
    gd::Int contact_this_move = 0;      // +0x88
    gd::Int guarded_prev = 0;           // +0x89
    gd::Int in_reaction = 0;            // +0x8A
    gd::Int branch_kind = 0;            // +0x8B
    gd::Int cond_flag_used = 0;         // +0x8C
    gd::Int launch_armed = 0;           // +0x8E
    gd::Int air_kind = 0;               // +0x90
    gd::Int ballistic = 0;              // +0x92
    gd::Int juggle_count = 0;           // +0x94
    gd::Int ground_offset = 0;          // +0x96
    gd::Int air_speed = 0;              // +0x98
    gd::Int air_vel_x = 0;              // +0x9A
    gd::Int air_vel_y = 0;              // +0x9C
    gd::Int air_vel_z = 0;              // +0x9E
    gd::Int cur_slot = 0;               // +0xA0
    gd::Int cur_transition = 0;         // +0xA2
    gd::Int slide_target_x = 0;         // +0xA4
    gd::Int slide_target_z = 0;         // +0xA8
    gd::Int opp_start_x = 0;            // +0xAC
    gd::Int opp_start_z = 0;            // +0xB0
    gd::Int attack_seg_count = 0;       // +0xB4
    gd::Int crouch_move = 0;            // +0xB5
    gd::Int step_kind = 0;              // +0xB6
    gd::Int attack_class = 0;           // +0xB7
    gd::Int air_phase = 0;              // +0xB8
    gd::Int track_mode = 0;             // +0xB9
    gd::Int move_flag_ba = 0;           // +0xBA
    gd::Int move_flag_bb = 0;           // +0xBB
    gd::Int skip_step_physics = 0;      // +0xBC
    gd::Int attack_pending = 0;         // +0xBD
    gd::Int anchor_dirty = 0;           // +0xBE
    gd::Int slide_to_point = 0;         // +0xBF
    gd::Int reset_flag_c0 = 0;          // +0xC0
    gd::Int apply_end_turn = 0;         // +0xC1
    gd::Int invulnerable = 0;           // +0xC2
    gd::Int active = 0;                 // +0xC3
    gd::Int in_throw = 0;               // +0xC4
    gd::Int is_cpu = 0;                 // +0xC5
    gd::Int side_flag = 0;              // +0xC6
    gd::Int fixed_facing = 0;           // +0xC7
    gd::Int fixed_facing_side = 0;      // +0xC8
    gd::Int no_look_at = 0;             // +0xC9
    gd::Int human_guard = 0;            // +0xCA
    gd::Int last_extra_damage = 0;      // +0xCC
    gd::Int got_hit = 0;                // +0xCE
    gd::Int guarded = 0;                // +0xCF
    gd::Int hit_clean = 0;              // +0xD0
    gd::Int contact = 0;                // +0xD1
    gd::Int whiffed = 0;                // +0xD2
    gd::Int counter_hit = 0;            // +0xD3
    gd::Int close_hit = 0;              // +0xD4
    gd::Int body_contact = 0;           // +0xD5
    gd::PI32 body_contact_with{0, 0, 0};   // +0xD6
    gd::Int no_body_push = 0;           // +0xD9
    gd::Int move_changed = 0;           // +0xDA
    gd::Int in_air = 0;                 // +0xDB
    gd::Int landed_a = 0;               // +0xDC
    gd::Int landed_b = 0;               // +0xDD
    gd::Int ko = 0;                     // +0xDE
    gd::Int about_to_hit = 0;           // +0xDF
    gd::Int tap_lp = 0;                 // +0xE0
    gd::Int tap_rp = 0;                 // +0xE1
    gd::Int tap_both = 0;               // +0xE2
    gd::Int active_segs = 0;            // +0xE3
    gd::Int extra_kind = 0;             // +0xE4
    gd::Int blend_active = 0;           // +0xE5
    gd::Int forced_hit = 0;             // +0xE6
    gd::Int forced_hit_in = 0;          // +0xE7
    gd::Int last_damage = 0;            // +0xE8
    gd::Int damage_override = 0;        // +0xEA
    gd::Int extra_damage = 0;           // +0xEC
    gd::Int hit_freeze_in = 0;          // +0xEE
    gd::Int dist_adj = 0;               // +0xF4 (u32)
    gd::Int dist = 0;                   // +0xF8 (u32)
    gd::Int dir_x = 0;                  // +0xFC: x offset to the opponent (PairwiseDistances)
    gd::Int dir_z = 0;                  // +0x100
    gd::Int hit_cooldown = 0;           // +0x104
    gd::Int push_frames = 0;            // +0x106
    gd::Int push_speed = 0;             // +0x108
    gd::Int push_table_frames = 0;      // +0x10A
    gd::Int push_dir = 0;               // +0x10C
    gd::PI32 push_table;                // +0x110: the push values still to come
    gd::Int push_table_pos = 0;
    gd::Int turn_frames = 0;            // +0x114
    gd::Int turn_step = 0;              // +0x116
    gd::Int recover_mash = 0;           // +0x118
    gd::Int recover_frames = 0;         // +0x11A
    gd::Int attack_alert = 0;           // +0x11C
    gd::Int sound_script_pos = 0;       // +0x11E: the running script's next entry
    gd::Int sound_script_frame = 0;     // +0x120: the running script's frame counter
    gd::Int power_timer = 0;            // +0x122
    gd::Int push_repeat = 0;            // +0x124
    gd::Int push_repeat_timer = 0;      // +0x126
    gd::Int step_cooldown = 0;          // +0x128
    gd::Int placed_x = 0;               // +0x12C
    gd::Int placed_z = 0;               // +0x130
    gd::Int prev_pose_frame = 0;        // +0x134
    gd::Int prev_slot = 0;              // +0x136
    gd::Int prev_move_unk10 = 0;        // +0x138
    gd::Int push_repeat_trans = 0;      // +0x13A
    gd::Arr<gd::Ref<HitSlot>> hit_slots;   // +0x13C (two HitSlots, created in the constructor)
    gd::Int best_hit_slot = 0;          // +0x194: index into hit_slots
    gd::Int entry_frame = 0;            // +0x198
    gd::Int transition = 0;             // +0x19A
    gd::Int trans_bit7 = 0;             // +0x19C
    gd::Int trans_bit6 = 0;             // +0x19E
    gd::Int move_slot = 0;              // +0x1A0
    gd::Ref<MoveRow> move_row;          // +0x1A4 pending move
    gd::PI32 reaction;                  // +0x1A8 the reaction record (21 s16), or a throw-victim entry
    gd::Int reaction_throw_slot = -1;   // +0x1A8 for 0x1xxx reactions: the throw victim's move slot

    // ---- shapes, velocities, health
    gd::Arr<gd::PI32> attack_segs;      // +0x1AC 4 × (start xyz, end xyz)
    gd::Arr<gd::PI32> hurt_zones;       // +0x20C 14 × (x, y, z, radius, radius²)
    gd::Arr<gd::PI32> body_points;      // +0x324 8 × (x, y, z, radius)
    gd::PI32 prev_hurt_zone{0, 0, 0};   // +0x3A4
    gd::Int prev_root_x = 0;            // +0x3B8
    gd::Int prev_root_y = 0;            // +0x3BC
    gd::Int prev_root_z = 0;            // +0x3C0
    gd::Int vel_x = 0;                  // +0x3C4
    gd::Int vel_y = 0;                  // +0x3C8
    gd::Int vel_z = 0;                  // +0x3CC
    gd::Int atk_dir_x = 0;              // +0x3D0
    gd::Int atk_dir_y = 0;              // +0x3D4
    gd::Int atk_dir_z = 0;              // +0x3D8
    gd::Int hit_dir_x = 0;              // +0x3DC
    gd::Int hit_dir_y = 0;              // +0x3E0
    gd::Int hit_dir_z = 0;              // +0x3E4
    gd::Int body_push_x = 0;            // +0x3E8
    gd::Int body_push_y = 0;            // +0x3EC
    gd::Int body_push_z = 0;            // +0x3F0
    gd::Int health = 0;                 // +0x3F4 (16.16)
    gd::Int health_max = 0;             // +0x3F8
    gd::Int carried_health = 0;         // +0x3FC
    gd::Int health_left = 0;            // +0x400 (s16): health in 1/4096 of the maximum at the round result (FUN_8003E6D0)

    // ---- input (+0x402 … +0x4E8)
    gd::Int in_dir = 5;                 // +0x402
    gd::Int in_pressed = 0;             // +0x404
    gd::Int in_held = 0;                // +0x406
    gd::Int script_input = 0;           // +0x408
    gd::Int in_hist_index = 0;          // +0x40C
    gd::Int cmd_count = -1;             // +0x410
    gd::Int cmd_read = -1;              // +0x414
    gd::Int cmd_merge_timer = 0;        // +0x418
    gd::PI32 in_history;                // +0x41C 60 × (pressed << 4 | direction)
    gd::PI32 cmd_buffer;                // +0x458 10 entries
    gd::PI32 in_pressed_hist;           // +0x462 60 raw pressed words
    gd::PI32 press_count{0, 0, 0, 0};   // +0x4DC

    // ---- model and pose
    gd::Ref<MotionBank> bank;           // the player's motion bank (g_fighterDivmot 0x800AE0E8)
    gd::Ref<FighterBody> body;          // joints, local matrices and the model (+0x8B0 …)
    gd::Int scale_base = 0;             // +0x4EC
    gd::Int scale = 0;                  // +0x4EE
    gd::Int root_x = 0;                 // +0xF68
    gd::Int root_y = 0;                 // +0xF6C
    gd::Int root_z = 0;                 // +0xF70
    gd::Int screen_x = 0;               // +0x1278
    gd::Ref<FighterHands> hands;        // +0x127C … +0x1298 (created in the constructor)
    gd::Int air_free = 0;               // +0x128A
    gd::Int ogre_freeze = 0;            // +0x12D0
    gd::Int ai_slot = -1;               // +0x1886
    gd::Int ai_target = -1;             // +0x1887
    gd::Int keep_light = 0;             // +0x1888

    // ---- motion blending (+0x1810 …)
    gd::Int prev_root_dx = 0;           // +0x1810
    gd::Int root_dy_blended = 0;        // +0x1812
    gd::Int blend_mode = 0;             // +0x181C
    gd::Int blend_mode_b = 0;           // +0x1820: cleared with the blend mode (FUN_8003C4AC)
    gd::Ref<MoveRow> last_pose_move;    // +0x1824
    gd::Int last_pose_frame = 0;        // +0x182C
    gd::PI32 blend_root_delta{0, 0, 0}; // +0x1868
    gd::Int blend_weight = 0;           // +0x186E
    gd::Int blend_frames = 0;           // +0x1870
    gd::Int blend_counter = 0;          // +0x1878
    gd::Ref<MoveRow> blend_src_move;    // +0x187C
    gd::Ref<MoveRow> blend_dst_move;    // +0x1880
    gd::Int blend_src_frame = 0;        // +0x1884
    gd::Int blend_dst_frame = 0;        // +0x1885

    // Scripted scenes (the attract demonstration): the running move as one row for root and pose.
    // GDScript property `move` (get/set): read as `get_move()`, written as `set_move(v)`.
    gd::Ref<MoveRow> get_move() const { return pose_move; }
    void set_move(const gd::Ref<MoveRow>& value) {
        pose_move = value;
        root_move = value;
    }
    // Enbu's anchor as one vector.
    // GDScript property `anchor` (get/set): read as `get_anchor()`, written as `set_anchor(v)`.
    gd::PI32 get_anchor() const { return gd::PI32{pos_x, pos_y, pos_z}; }
    void set_anchor(const gd::PI32& value) {
        pos_x = value[0];
        pos_y = value[1];
        pos_z = value[2];
    }

    FighterState();

    // FUN_80036440: selects a costume slot (and the costume key and character it belongs to).
    void set_costume(gd::Int slot, const gd::Ref<FighterTables>& tables);

    // The whole record copied from `other` (Tekken Ball copies the hitter's record into the third
    // record, the ball's attacker). Move rows, banks and texts are shared data; everything the
    // record owns is copied.
    // Reflection in GDScript (get_property_list / get / set): the .cpp assigns every member in
    // declaration order through `_copied`, skipping the NOT_COPIED names (body, move, anchor).
    void copy_from(const gd::Ref<FighterState>& other);

    // `_copied(v: Variant)` split by the types the record actually holds. Overload resolution
    // picks the exact type; everything else (ints, MoveRow / MotionBank refs: shared data) goes
    // to the template and is returned as is, like the GDScript fallthrough `return v`.
    template <typename T>
    static T _copied(const T& v) { return v; }
    static gd::PI32 _copied(const gd::PI32& v);                        // TYPE_PACKED_INT32_ARRAY
    static gd::PBytes _copied(const gd::PBytes& v);                    // TYPE_PACKED_BYTE_ARRAY
    static gd::PI64 _copied(const gd::PI64& v);                        // TYPE_PACKED_INT64_ARRAY
    static gd::Arr<gd::PI32> _copied(const gd::Arr<gd::PI32>& v);      // TYPE_ARRAY, elements copied
    static gd::Arr<gd::Ref<HitSlot>> _copied(const gd::Arr<gd::Ref<HitSlot>>& v);   // TYPE_ARRAY
    static gd::Ref<HitSlot> _copied(const gd::Ref<HitSlot>& v);        // TYPE_OBJECT HitSlot: new, members copied
    static gd::Ref<FighterHands> _copied(const gd::Ref<FighterHands>& v);   // TYPE_OBJECT FighterHands
};

}  // namespace t3
