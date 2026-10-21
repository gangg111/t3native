#pragma once
// remake/core/game/result_screens.gd
// The result screens of result.ovl (modes.md#result-screens), written from the verified ports in
// `tools/research/screens_sim.py`: team battle (FUN_800EF4AC, game state 12), time attack
// (FUN_800EFF4C, state 13), survival (FUN_800F0A78, state 14) and Tekken Force (FUN_800F1F08,
// state 15, `force_result` in the same file). Their state is kept as the overlay's bytes
// (0x800FAC48 … 0x800FAE7C), as the flow traces record it.
#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class ByteBlock;

class ResultScreens : public FlowPart {
public:
    static constexpr gd::Int BASE = 0x800FAC48LL;
    static constexpr gd::Int SIZE = 0x238;
    static constexpr gd::Int BANNER_BASE = 0x80102690LL;
    // Team battle.
    static constexpr gd::Int T_FIGHT = 0x00;
    static constexpr gd::Int T_LOST1 = 0x08;
    static constexpr gd::Int T_LOST2 = 0x0C;
    static constexpr gd::Int T_LEFT1 = 0x10;
    static constexpr gd::Int T_LEFT2 = 0x14;
    static constexpr gd::Int T_MSG = 0x18;
    static constexpr gd::Int T_SLIDE = 0x1C;
    static constexpr gd::Int T_LINE = 0x20;
    static constexpr gd::Int T_MEMBER1 = 0x30;              // s32 per fight: player 1's members lost before it
    static constexpr gd::Int T_MEMBER2 = 0xE0;
    // Time attack.
    static constexpr gd::Int A_ROWS = 0x190;
    static constexpr gd::Int A_ROW_ANIM = 0x194;
    static constexpr gd::Int A_RECORD = 0x198;
    static constexpr gd::Int A_PLAYER = 0x19C;
    static constexpr gd::Int A_KEY = 0x1A0;
    static constexpr gd::Int A_TOTAL = 0x1A4;
    static constexpr gd::Int A_SHOW = 0x1A8;
    static constexpr gd::Int A_WAIT = 0x1AC;
    static constexpr gd::Int A_FRAME = 0x1B0;
    // Survival.
    static constexpr gd::Int S_COUNT = 0x1B8;
    static constexpr gd::Int S_CHARS = 0x1C8;               // u8 per row: the characters beaten, most first
    static constexpr gd::Int S_ROW = 0x1E0;
    static constexpr gd::Int S_PLAYER = 0x1E4;
    static constexpr gd::Int S_RANK = 0x1E8;
    static constexpr gd::Int S_KEY = 0x1EC;
    static constexpr gd::Int S_FLAG = 0x1F0;
    static constexpr gd::Int S_WAIT = 0x1F4;
    static constexpr gd::Int S_TOTAL = 0x1F8;
    static constexpr gd::Int S_FRAME = 0x1FC;
    static constexpr gd::Int S_ANIM = 0x200;
    // Tekken Force.
    static constexpr gd::Int F_TEX = 0x208;                 // 4 × u16: the player's big picture slot (0x800FAC34 per side)
    static constexpr gd::Int F_COLOUR = 0x210;              // the picture's tint
    static constexpr gd::Int F_FLIP = 0x214;                // u8: mirrored
    static constexpr gd::Int F_FRAME = 0x218;
    static constexpr gd::Int F_SHOWN = 0x21C;               // frames since the fade-in ended: the lines appear by it
    static constexpr gd::Int F_BOSS = 0x220;                // the bosses beaten appear by it
    static constexpr gd::Int F_PHASE = 0x224;               // the FORCE count: 0 wait, 1 counting, 2 done
    static constexpr gd::Int F_FORCE = 0x228;               // the counted enemies
    static constexpr gd::Int F_TEX_SLOTS = 0x800FAC34LL;    // result.ovl
    static constexpr gd::Int F_SCORE_AT = 0x1E;             // YOUR SCORE (then HIGH SCORE, the keys or bosses, 30 frames apart)
    static constexpr gd::Int F_HIGH_AT = 0x3C;
    static constexpr gd::Int F_LIST_AT = 0x5A;
    static inline const gd::Arr<gd::Int> F_KEY_AT{0x5A, 0x78, 0x96};   // COPPER, SILVER, GOLD
    static inline const gd::Arr<gd::Int> F_BOSS_AT{0x14, 0x28, 0x3C};  // the second to fourth boss
    static constexpr gd::Int F_COUNT_AFTER = 0x4F;
    static constexpr gd::Int F_WAIT = 0x1248;
    static constexpr gd::Int F_FADE_MUSIC = 0xB4;
    static constexpr gd::Int SOUND_LINE = 0x4C6C;           // a result line appears
    static constexpr gd::Int FORCE_SCORE = 0x38;            // mode context: the score, the enemies, the player, a new record
    static constexpr gd::Int FORCE_COUNT = 0x3C;
    static constexpr gd::Int FORCE_PLAYER = 0x3E;
    static constexpr gd::Int FORCE_FLAGS = 0x40;            // 1 copper, 2 silver, 4 gold key; 8 the doctor saved; 0x10 bosses
    static constexpr gd::Int FORCE_DOCTOR = 8;
    static constexpr gd::Int FORCE_BOSSES = 0x10;
    static constexpr gd::Int BRIGHT = 0x230;                // s32 screen brightness (0x100 full)
    static constexpr gd::Int RESULT_WAIT = 0x1248;          // 4,680 frames
    static constexpr gd::Int TEAM_WAIT = 0x707;
    static constexpr gd::Int TIME_CAP = 359999;
    static constexpr gd::Int SOUND_AFTER_VOICE = 0x86DA;
    static constexpr gd::Int SEQ_DELAY = 15;
    static constexpr gd::Int MUSIC_RESULT = 6;
    static constexpr gd::Int SOUND_TICK = 0x55F5;           // each fight line, stage row and win counted
    static constexpr gd::Int SOUND_REVEAL = 0x50F4;         // the team message and the survival rank appear

    gd::Ref<ByteBlock> state;                // ByteBlock.new(SIZE), made in the constructor
    gd::Ref<ByteBlock> banner;               // ByteBlock.new(8): 0x80102690: the scrolling banner's x, y
    gd::Int seq_state = 0;                   // 0x800A37A8: the victory voice's sequence
    gd::Int seq_delay = 0;                   // 0x800A37A9

    // FlowPart's _init(flow); the constructor makes `state` and `banner`.
    explicit ResultScreens(GameFlow* flow);

    gd::Int brightness();
    gd::Int _get32(gd::Int at);
    void _put(gd::Int at, gd::Int v);
    gd::Int _ctx8(gd::Int at);
    void _fade_in(gd::Int next_sub);
    void _fade_out(gd::Int next_sub);
    // FUN_800F18FC's state: the scrolling tiles and banner.
    void _banner_scroll();

    // ---- the victory voice (FUN_80075A90, FUN_80075B1C, FUN_80075B4C)
    void _seq_reset();
    // ResultWinVoice: the winner's victory voice (none for Tiger), then the sequence goes on.
    void _result_sounds(gd::Int player);
    // After the voice (the SPU's voice 2 is never reported busy here) and 15 frames, sound 0x86DA.
    void _seq_tick();

    // ---- team battle (game state 12)
    void team_step();
    // Team battle's result: the members' outcomes and the teams' sizes.
    void _team_setup();
    // The joining line of the current fight grows; at its end the loser's member is crossed out.
    void _team_line();
    // The result holds: face buttons switch the message, Select shows it again, the wait ends it.
    void _team_wait(bool first);

    // ---- time attack (game state 13)
    void time_attack_step();
    // FUN_800F28E8: the ten stage times of the run, capped.
    gd::Int _total_time();

    // ---- survival (game state 14)
    void survival_step();
    // Survival's result: the characters beaten, most wins first, and the rank.
    void _survival_setup();
    // The rows count up one win every 4 frames, a tick each, row by row.
    void _survival_count(bool first);

    // ---- Tekken Force (game state 15)
    // FUN_800F1F08: TEKKEN FORCE, the score and the high score, then the keys found (or the doctor
    // saved, or the bosses beaten and the enemies counted); Start or 4,680 frames end it, the
    // ranking follows. The lines' sounds are the drawing's.
    void force_step();
    // The drawing's counters and sounds: each line's sound as it appears, the bosses one by one,
    // then the FORCE count ticking up every other frame.
    void _force_lines();
};

}  // namespace t3
