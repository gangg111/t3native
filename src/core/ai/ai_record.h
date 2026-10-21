#pragma once
// remake/core/ai/ai_record.gd
// A CPU fighter's AI record (0x330 bytes at 0x8009F6C0 + 0x330 · slot; ai.md#entry-and-state).
// Field comments give the record offsets; the port keeps the game's widths where values wrap.
#include "content/ai_tables.h"
#include "gd/gd.h"

namespace t3 {

class FighterState;
class MoveRow;

class AiRecord {
public:
    static constexpr gd::Int PARAM_WORDS = 55;
    static constexpr gd::Int HISTORY = 8;

    // The difficulty words (ai.md#difficulty-parameters) by index, where identified: mostly
    // probabilities out of 4096 drawn against the 12-bit LCG, else frame counts.
    enum Param : gd::Int {
        HOLD_BYTES = 0,       // 0-1: four held-input lengths
        WAIT = 2,             // wait after starting an attack, plus a random one of WAIT_EXTRA..+3
        WAIT_EXTRA = 3,
        THROW_STEPS = 7,      // follow-up steps of a multi-part throw
        THROW_TRY = 8,
        THROW_ESCAPE = 9,
        THROW_CONTINUE = 10,
        THROW_LIST = 11,      // the choice between the two throw lists; King's hook
        FILTER_POSTURE = 12,  // the extra candidate filters while collecting attacks
        FILTER_FAST = 13,
        GUARD = 14,           // guard or evade an attack that is coming (raised per hit up to the cap)
        GUARD_CAP = 15,
        PUNISH = 16,          // punish and hold guard
        PUNISH_CAP = 17,      // also the chance to answer a move that already hurt the CPU
        SIDE_STEP = 18,
        COUNTER = 19,         // counter-attack an attack that is coming
        COUNTER_CAP = 20,
        DUCK = 21,            // duck or step back from an attack that is coming
        DUCK_CAP = 22,
        ATTACKS_A = 25,       // the larger of the two: attacks in a row before a pause
        ATTACKS_B = 27,
        HOOKS = 29,           // consult the character hooks; the evasion pick and forced escapes
        VS_LAUNCHED = 30,     // attack a launched opponent
        FOCUS = 35,           // the general threshold of most secondary draws
        AIR_RETREAT = 36,     // back away from an air attack at close range
        KEEP_RETREAT = 38,    // keep movement choices 3 and 6
        BANK_COUNTER = 45,    // bank-specific counter moves
        GRAB_GUARD = 47,      // guard or evade a grab
        STANCE = 49,          // a stance action
    };

    gd::Int slot = 0;                    // +0x00
    gd::Int frame_count = 0;             // +0x02: grows whenever the pose frame changes (bit 0 picks the walk)
    gd::Int pad = 0;                     // +0x04: held pad produced this frame
    gd::Int prev_pad = 0;                // +0x06
    gd::PI32 script_steps;               // +0x08: the pending input script
    gd::Int script_at = -1;              // +0x08: its next step, −1 none
    gd::Ref<FighterState> target;        // +0x10
    gd::Int band = 0;                    // +0x14: distance band 0-5
    gd::Int reach_band = 0;              // +0x18: the opponent's reach band 1-5
    gd::Int distance = 0;                // +0x1C
    gd::Int prev_distance = 0;           // +0x20
    gd::Int distance_change = 0;         // +0x24
    gd::Int move_changed = 0;            // +0x28
    gd::Int far_count = 0;               // +0x2A
    gd::Int move_frames = 0;             // +0x2C
    gd::Int angle = 0;                   // +0x2E: the CPU's relAngle
    gd::Int health_seen = 0;             // +0x30
    gd::Int pose_frame_seen = 0;         // +0x34
    gd::Int neutral_action = 0;          // +0x36
    gd::Int approach = 0;                // +0x38
    gd::Int approach_kind = 0;           // +0x3A: 2 a long approach
    gd::Int crouching = 0;               // +0x3C
    gd::Int guarding = 0;                // +0x3E
    gd::Int approach_hold = 0;           // +0x40
    gd::Int setup_cooldown = 0;          // +0x42
    gd::Int neutral_cooldown = 0;        // +0x44
    gd::Int air_cooldown = 0;            // +0x46
    gd::Int direction_hold = 0;          // +0x48
    gd::Int crouch_timer = 0;            // +0x4A
    gd::Int crouch_attack_hold = 0;      // +0x4C
    gd::Int move_frame_seen = 0;         // +0x4E
    gd::Int attacking = 0;               // +0x50: the CPU's move has an active window
    gd::Int special_open = 0;            // +0x52: an air or special move with its window open
    gd::Int whiff_timer = 0;             // +0x54
    gd::Ref<MoveRow> collected_move;     // +0x58: the move the candidates were collected from
    gd::Ref<MoveRow> pressed_move;       // +0x5C
    gd::Ref<MoveRow> last_attack;        // +0x60
    gd::Int situation = 0;               // +0x64
    gd::Int wait = 0;                    // +0x68: frames before the next attack (−1: attack now)
    gd::Int guard_holds = 0;             // +0x6A
    gd::Int guard_delay = 0;             // +0x6C
    gd::Int cancel_wait = 0;             // +0x6E
    gd::Int candidate_count = 0;         // +0x70
    gd::Int marked_count = 0;            // +0x72
    gd::Int hook_opponent = AiTables::Hook::NONE;     // +0x74: hook A of the opponent's bank
    gd::Int hook_own = AiTables::Hook::NONE;          // +0x78: hook B of the CPU's own bank
    bool filter_no_lows = false;         // +0x7C: FUN_800616B4 installed for this frame
    gd::Int announced_frames = 0;        // +0x80
    gd::Int passed_frames = 0;           // +0x84
    gd::Int vs_stance = 0;               // +0x88: the opponent is Yoshimitsu in slot 0x456
    gd::Int move_block = 0;              // +0x8A
    gd::Int guard_suspend = 0;           // +0x8C
    gd::Int parried = 0;                 // +0x8E
    gd::Int punish = 0;                  // +0x8F
    gd::Int throw_next = 0;              // +0x90
    gd::Int throw_chance = 0;            // +0x91
    gd::Int evade_reason = 0;            // +0x92
    gd::Int guard_memory = 0;            // +0x93
    gd::Int evasion = 0;                 // +0x94
    gd::Int throw_steps = 0;             // +0x96
    gd::Int throw_list = 0;              // +0x98
    gd::Int throw_press_frame = 0;       // +0x9C
    gd::Ref<MoveRow> throw_move;         // +0xA0
    gd::PI32 throw_row;                  // +0xBC
    gd::Int strings_on = 0;              // +0xC0
    gd::Int strings_count = 0;           // +0xC2
    gd::Arr<gd::PI32> strings;           // +0xC4
    gd::Int no_strings = 0;              // +0xD4
    gd::Int planned_on = 0;              // +0xD8
    gd::Int planned_count = 0;           // +0xDA
    gd::Arr<gd::PI32> planned;           // +0xDC
    gd::Int links_on = 0;                // +0x1A0
    gd::Int links_count = 0;             // +0x1A2
    gd::Arr<gd::PI32> links;             // +0x1A4
    gd::Int link_skip = 0;               // +0x1CC
    gd::Int link_last = 0;               // +0x1D0
    gd::Int attacks_in_row = 0;          // +0x1D2
    gd::Int attack_limit = 0;            // +0x1D4
    gd::Arr<gd::Ref<MoveRow>> history;   // +0x1DC: the opponent's moves that hurt the CPU
    gd::Int history_at = 0;              // +0x1FC
    gd::Int reload_timer = 0;            // +0x1FE
    gd::Int opp_breath = 0;              // +0x200
    gd::Int opp_gon_special = 0;         // +0x201
    gd::Int adaptive = 0;                // +0x202
    gd::Int opp_move_changed = 0;        // +0x204
    gd::Int opp_down = 0;                // +0x208
    gd::Int opp_until = 0;               // +0x20A: frames until the opponent's attack is active (999: none)
    gd::Int opp_coming = 0;              // +0x20C
    gd::Int opp_unblockable = 0;         // +0x20E
    gd::Int opp_charging = 0;            // +0x210
    gd::Int coming_seen = 0;             // +0x212
    gd::Int opp_grabs = 0;               // +0x214
    gd::Int opp_string = -1;             // +0x216
    gd::Int opp_high = 0;                // +0x21A
    gd::Int input_mode = 0;              // +0x21E: 0 no input, 1 hold up while crouching, else AI
    gd::Int own_word = 0;                // +0x220: the CPU's move +0x08
    gd::Int own_state = 0;               // +0x224: the CPU's move +0x04
    gd::Int own_attack = 0;              // +0x228
    gd::Int opp_word = 0;                // +0x22C
    gd::Int opp_state = 0;               // +0x230
    gd::Int opp_attack = 0;              // +0x234
    gd::PI32 params;                     // +0x238: the difficulty words
    gd::PI32 own_bands{0, 0, 0, 0, 0, 0};   // +0x314
    gd::PI32 opp_bands{0, 0, 0, 0, 0, 0};   // +0x320
    gd::Int force_words = 0;             // +0x32C: Tekken Force's pattern (index into AiTables.force_words)

    AiRecord();

    // Difficulty word `i` (record +0x238 + 2·i).
    gd::Int word(gd::Int i) const { return params[i]; }

    // Byte `i` of the difficulty words (words 0-1 hold four bytes).
    gd::Int param_byte(gd::Int i) const { return gd::shr(gd::Int(params[i >> 1]) & 0xFFFF, 8 * (i & 1)) & 0xFF; }

    bool script_pending() const { return script_at >= 0; }
};

}  // namespace t3
