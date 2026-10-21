#pragma once
// remake/core/game/mode_region.gd
// The fight's globals and the mode context at 0x800AFF00 (modes.md#match-flow): the frame
// counter, the rule bytes the mode start sets, the match result, and the mode context
// (0x800AFF50) the mode overlays use with a layout of their own per mode (the arcade ladder,
// team records, survival counters). `ctx_*` accessors take offsets from the mode context.
#include "core/game/byte_block.h"

namespace t3 {

class ModeRegion : public ByteBlock {
public:
    static constexpr gd::Int BASE = 0x800AFF00LL;
    static constexpr gd::Int SIZE = 0x110;
    static constexpr gd::Int CONTEXT = 0x50;    // 0x800AFF50
    static constexpr gd::Int AI_SELF = 0x24;    // 0x800AFF24: the fighter the AI drives this frame (its address)

    ByteField<0x04, U32> fighter_distance{this};     // 0x800AFF04 g_fighterDistance
    ByteField<0x14, U32> frame_counter{this};        // 0x800AFF14 main-loop frames
    ByteField<0x20, U16> chip_damage{this};          // 0x800AFF20 GUARD DAMAGE
    ByteField<0x50, U32> mode{this};                 // 0x800AFF50 g_gameMode
    ByteField<0x54, U8> pause_allowed{this};         // 0x800AFF54
    ByteField<0x55, U8> challengers{this};           // 0x800AFF55: a player may join (new challenger)
    ByteField<0x56, U8> team_members{this};          // 0x800AFF56
    ByteField<0x57, U8> team_hud{this};              // 0x800AFF57: the team members' HUD
    ByteField<0x58, U8> draw_wins{this};             // 0x800AFF58: a draw may decide the match
    ByteField<0x59, U8> carry_health{this};          // 0x800AFF59
    ByteField<0x5A, U8> team_round{this};            // 0x800AFF5A
    ByteField<0x5B, U8> collapse_on_ko{this};        // 0x800AFF5B
    ByteField<0x5C, U8> quick_select_mode{this};     // 0x800AFF5C: the mode uses quick select
    ByteField<0x5D, U8> character_change{this};      // 0x800AFF5D: CHARACTER CHANGE AT CONTINUE
    ByteField<0x5E, U8> quick_select_option{this};   // 0x800AFF5E: QUICK SELECT
    ByteField<0x5F, U8> challenger_quick{this};      // 0x800AFF5F: a challenger held L1 + R1
    ByteField<0x61, U8> mode_started{this};          // 0x800AFF61
    ByteField<0x62, U8> continuing{this};            // 0x800AFF62
    ByteField<0x63, U8> challenger{this};            // 0x800AFF63: a new challenger is entering
    ByteField<0x64, U8> vs_next_state{this};         // 0x800AFF64: the game state after the VS screen
    ByteField<0x65, U8> vs_next_sub{this};           // 0x800AFF65
    ByteField<0x66, U8> mode_overlay{this};          // 0x800AFF66: the mode overlay the mode needs
    ByteField<0x67, U8> loaded_mode_overlay{this};   // 0x800AFF67
    ByteField<0x68, U8> true_ogre_mask{this};        // 0x800AFF68
    ByteField<0x6C, U8> human_count{this};           // 0x800AFF6C
    ByteField<0x6D, U8> human_mask{this};            // 0x800AFF6D
    ByteField<0x6E, U8> human_index{this};           // 0x800AFF6E
    ByteField<0x6F, U8> cpu_index{this};             // 0x800AFF6F
    ByteField<0x70, U8> rounds_option{this};         // 0x800AFF70: rounds to win - 1
    ByteField<0x71, U8> match_result{this};          // 0x800AFF71: 0 undecided, 1/2 winner + 1, 3 draw
    ByteField<0x72, U8> match_winner{this};          // 0x800AFF72
    ByteField<0x73, U8> match_loser{this};           // 0x800AFF73
    ByteField<0x74, U32> fight_index{this};          // 0x800AFF74: the ladder fight (arcade, time attack)
    ByteField<0x78, U32> match_count{this};          // 0x800AFF78: VS and two-player arcade matches played
    ByteField<0x7C, S32> fight_clock{this};          // 0x800AFF7C: the fight's time in frames (RoundFlow counts it)
    ByteField<0x80, S32> clock_start{this};          // 0x800AFF80: the next fight's clock (a lost fight's, on continue)
    ByteField<0x84, S32> total_time{this};           // 0x800AFF84: arcade and time attack: the won fights' time

    explicit ModeRegion(gd::Int size = SIZE);

    gd::Int ctx8(gd::Int at) const { return u8(CONTEXT + at); }
    gd::Int ctx16(gd::Int at) const { return u16(CONTEXT + at); }
    gd::Int ctx_s16(gd::Int at) const { return s16(CONTEXT + at); }
    gd::Int ctx32(gd::Int at) const { return u32(CONTEXT + at); }
    gd::Int ctx_s32(gd::Int at) const { return s32(CONTEXT + at); }
    void ctx_put8(gd::Int at, gd::Int v) const { put8(CONTEXT + at, v); }
    void ctx_put16(gd::Int at, gd::Int v) const { put16(CONTEXT + at, v); }
    void ctx_put32(gd::Int at, gd::Int v) const { put32(CONTEXT + at, v); }
};

}  // namespace t3
