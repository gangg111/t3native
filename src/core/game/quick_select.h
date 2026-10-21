#pragma once
// remake/core/game/quick_select.gd
// The resident character select of VS, team battle and Tekken Ball, and of QUICK SELECT and
// challengers (FUN_80055878, game state 10; modes.md#character-select), written from the
// decompiled FUN_800532E8 / FUN_800530CC / FUN_80054294 and the verified port in
// `tools/research/select_sim.py`. The screen's context (0x800B9378) is kept as bytes: the mode, the
// fade-in, and one 0xAC-byte record per player (state, cursor, team, handicap, picks).
// Tekken Ball's hooks (volley.ovl): the ball preview (FUN_800B5B6C), the ball damage bar
// (FUN_800B5C4C) and the ball type chosen by the first side to finish (FUN_800B569C).
#include "gd/gd.h"
#include "core/game/flow_part.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;
class ByteBlock;

class QuickSelect : public FlowPart {
public:
    static constexpr gd::Int BASE = 0x800B9378LL;
    static constexpr gd::Int SIZE = 0x170;
    static constexpr gd::Int RECORDS = 0x18;
    static constexpr gd::Int RECORD_SIZE = 0xAC;
    static constexpr gd::Int LOCKED = 0x58;
    static constexpr gd::Int TAKEN = 0x59;
    static constexpr gd::Int COLUMNS = 7;
    static constexpr gd::Int ROWS = 3;
    static constexpr gd::Int CHOOSE = PadState::CONFIRM;
    static constexpr gd::Int SOUND_MOVE = 0x55F5;
    static constexpr gd::Int SOUND_PICK = 0x50F4;
    static constexpr gd::Int SOUND_JOIN = 0x49A1;
    static constexpr gd::Int MUSIC_SELECT = 1;
    static constexpr gd::Int START_COSTUME_BUTTONS = PadState::START | PadState::TRIANGLE;
    // Record fields.
    static constexpr gd::Int R_STATE = 0x00;
    static constexpr gd::Int R_PLAYER = 0x04;
    static constexpr gd::Int R_COLUMN = 0x08;
    static constexpr gd::Int R_ROW = 0x0C;
    static constexpr gd::Int R_AVAILABLE = 0x24;
    static constexpr gd::Int R_WANTED = 0x28;
    static constexpr gd::Int R_COUNT = 0x2C;
    static constexpr gd::Int R_ORDER = 0x34;
    static constexpr gd::Int R_MEMBERS = 0x38;
    static constexpr gd::Int R_HANDICAP = 0x58;
    static constexpr gd::Int R_LAST = 0x64;
    static constexpr gd::Int R_CLASH = 0x68;
    static constexpr gd::Int R_PREFS = 0x78;
    // Player states: set-up, the team size, choosing (team battle: Start ends the team early),
    // the handicap, waiting for the other side, choosing for the other side (its pad drives), CPU
    // side (a challenger may join; the team variant copies the size), no fighter, chosen, handicap
    // set, chosen by the other pad, and Tekken Ball's wait for the ball type and its choice.
    enum : gd::Int {
        SET_UP = 0, TEAM_SIZE = 1, TEAM_CHOOSING = 2, CHOOSING = 3, HANDICAP = 4, WAITING = 6,
        CHOOSING_OTHER = 7, CPU = 8, CPU_TEAM = 9, NONE = 0xA, CHOSEN = 0xB, HANDICAP_SET = 0xC,
        CHOSEN_BY_OTHER = 0xD, BALL_WAITING = 0xE, BALL_TYPE = 0xF,
    };

    gd::Ref<ByteBlock> ctx;                  // ByteBlock.new(SIZE), made in the constructor

    // FlowPart's _init(flow): QuickSelect has no _init of its own; the constructor makes `ctx`.
    explicit QuickSelect(GameFlow* flow);

    gd::Int mode();
    bool fading();
    gd::Int fade_level();
    gd::Int rec(gd::Int player);
    gd::Int r32(gd::Int player, gd::Int at);
    gd::Int rs32(gd::Int player, gd::Int at);
    gd::Int r16(gd::Int player, gd::Int at);
    void w32(gd::Int player, gd::Int at, gd::Int v);
    // The costume key in a grid cell (s16 x, s16 y, u8 key).
    gd::Int cell_key(gd::Int column, gd::Int row);
    void step();
    // FUN_800513FC: 2 for an active human, +1 when the pick is kept.
    gd::Int _kind(gd::Int player);
    // The cursor on the cell of `key` (else of the record's default key).
    void _find(gd::Int player, gd::Int key);
    // The common set-up: one member, the kept character (or the last pick) under the cursor.
    void _start_cursor(gd::Int player);
    // Select: the last picked member back (its character is available again). The picks are
    // members 0 to count - 1 (`pick` stores at `count`, then counts up).
    bool _undo(gd::Int player);
    // FUN_800532E8: one player's quick select; true when this side is done.
    // States: 0 set-up, 1 team size, 2 team members, 3 choosing, 4 VS handicap, 6/7 the other pad
    // chooses for this side, 8 CPU side (a challenger may join), 9 team size from the other side,
    // 10-13 done, 14 waiting for the ball side select, 15 the ball side select.
    gd::Int player_step(gd::Int player);
    // Team battle: left / right set the number of members (1-8); a choice button goes on to the
    // characters (the order by buttons 0x60).
    void _team_size_step(gd::Int player);
    // Choosing a character; in team battle Start ends the team early and Select takes back the last
    // pick (or returns to the team size). A finished side goes on by the mode.
    void _choose_step(gd::Int player, gd::Int state);
    // VS and Tekken Ball: left / right move the handicap bar (0-7, twice as fast for the ball); Select
    // takes back the pick, a face button sets it.
    void _handicap_step(gd::Int player);
    void _setup(gd::Int player);
    // The cursor of `player`'s record moved by `pad`'s repeating directions (a sound on a move).
    void _move(gd::Int player, gd::Int pad);
    // FUN_800530CC: the character under the cursor for a face button (Start or △ the Start costume
    // when unlocked, ✕ / ○ costume 1, □ 0); 0 once the side's picks are complete.
    gd::Int pick(gd::Int player, gd::Int pressed);
    // FUN_8004F334 on two (key, costume, decided, human) records at `a` and `b`: the same costume
    // twice changes one of them (the undecided or CPU side's, else `a`'s).
    static void costume_clash(const gd::Ref<ByteBlock>& block, gd::Int a, gd::Int b);
    // FUN_80054294: the picks into the players' choices (team battle: the member lists).
    void commit();
};

}  // namespace t3
