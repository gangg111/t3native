#pragma once
// remake/core/game/character_select.gd
// The character select screen (select.ovl FUN_8011056C, game state 9; modes.md#character-select),
// written from the verified port `tools/research/select_sim.py` and the decompiled overlay (the
// sounds). The screen's context (0x80118C48) and its portrait grid (0x80129CE8) are kept as bytes.
//
// Context: +0x00 mode, +0x04 layout (0: one row, 1: two rows), +0x08 frame counter, +0x0C
// cursor-moved flag, +0x10 NEW blink timer, +0x14 / +0x18 time limit and its step, +0x1C unlocked
// mask, +0x20 unlocked count, +0x24 / +0xA0 the players' records (0x7C bytes), +0x11C layout.
// Grid cells (12 bytes): +0 index, +1…+4 the cell up, down, left, right (0x16 none), +5 shown,
// +6 s16 character (0x16 locked), +8 / +0xA s16 x, y.
#include "gd/gd.h"
#include "core/game/flow_part.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;
class ByteBlock;

class CharacterSelect : public FlowPart {
public:
    static constexpr gd::Int BASE = 0x80118C48LL;
    static constexpr gd::Int SIZE = 0x14C;
    static constexpr gd::Int CELLS_BASE = 0x80129CE8LL;
    static constexpr gd::Int CELL_COUNT = 22;
    static constexpr gd::Int CELL_SIZE = 12;
    static constexpr gd::Int NO_CELL = 0x16;
    static constexpr gd::Int DUMMY_CELL = -1;               // 0x800B9650: an all-0x16 cell for an empty row (read-only)
    static constexpr gd::Int CELL_STEP = 0x23;              // the portraits' horizontal spacing
    static constexpr gd::Int RECORDS = 0x24;
    static constexpr gd::Int RECORD_SIZE = 0x7C;
    static constexpr gd::Int LAYOUT = 0x11C;
    static constexpr gd::Int LAYOUT_SIZE = 0x30;
    static constexpr gd::Int NEW_BLINK = 0xB4;
    static constexpr gd::Int CHOSEN_WAIT = 0x5A;
    static constexpr gd::Int CHOSEN_ANIM = 0x1C;
    static constexpr gd::Int DRAW_FROM = 3;                 // the screen draws (and Select + Start leaves) from frame 3
    static constexpr gd::Int SOUND_MOVE = 0x55F5;
    static constexpr gd::Int SOUND_CHOOSE = 0x50F4;
    static constexpr gd::Int SOUND_JOIN = 0x49A1;
    static constexpr gd::Int MUSIC_SELECT = 1;
    static constexpr gd::Int SPARK_COUNT = 64;
    static constexpr gd::Int SPARK_HEIGHT = 0xA0;
    static constexpr gd::Int SPARK_BRIGHT = 0x60;
    static constexpr gd::Int LAYOUT_SPARK_Y = 0x124;        // context: the sparks' starting y
    static constexpr gd::Int START_COSTUME_BUTTONS = PadState::START | PadState::TRIANGLE;
    // Record fields.
    static constexpr gd::Int R_STATE = 0x00;
    static constexpr gd::Int R_CELL = 0x04;
    static constexpr gd::Int R_ANIM = 0x08;
    static constexpr gd::Int R_FLIP = 0x10;
    static constexpr gd::Int R_SHOWN_KEY = 0x14;
    static constexpr gd::Int R_SHOWN_COSTUME = 0x18;
    static constexpr gd::Int R_KEY = 0x1C;
    static constexpr gd::Int R_COSTUME = 0x20;
    static constexpr gd::Int R_TIMER = 0x24;
    static constexpr gd::Int R_BASE = 0x28;
    static constexpr gd::Int R_RECORD5 = 0x2C;
    static constexpr gd::Int R_CHOSEN_ANIM = 0x30;
    static constexpr gd::Int R_CHOSEN_KEY = 0x3C;
    static constexpr gd::Int R_CHOSEN_COSTUME = 0x40;
    static constexpr gd::Int R_CLASH = 0x44;
    static constexpr gd::Int R_LAYOUT = 0x54;
    static constexpr gd::Int R_FACING = 0x60;               // u16 in the layout words: 1 faces right
    static constexpr gd::Int R_DEFAULT_CELL = 0x78;         // u16 in the layout words
    // Player states: set-up, choosing, waiting for the other side, choosing for the other side (its
    // pad drives), CPU side (a challenger may join), no fighter, chosen, chosen by the other pad.
    enum : gd::Int { SET_UP, CHOOSING, WAITING, CHOOSING_OTHER, CPU, NONE, CHOSEN, CHOSEN_BY_OTHER };

    // The grid builder's per-row record (a stack struct in the game).
    class Row {
    public:
        gd::Int first = 0;
        gd::Int cur = 0;
        gd::Int last = 0;
        gd::Int x = 0xB;
        gd::Int y = 0;
        gd::Int row = 0;
        gd::Int phase = 0;
        gd::Int odd = 0;
        gd::Int middle = NO_CELL;
        gd::Int skip = 0;
        gd::Int remaining = 0;
        gd::Int half = 0;
        gd::Int total = 0;

        Row(gd::Int first_cell, gd::Int row_index, gd::Int row_y);
    };

    // A rising, fading streak (20 bytes in the game). Declared here, before `sparks` uses it (the
    // GDScript declares it after the members).
    class Spark {
    public:
        gd::Int active = 0;
        gd::Int started = 0;
        gd::Int x = 0;
        gd::Int y = 0;
        gd::Int top = 0;                     // the y it rises from
        gd::Int frame = 0;                   // the frame it started
        gd::Int bright = 0;
        gd::Int fade = 0;
    };

    gd::Ref<ByteBlock> ctx;                  // ByteBlock.new(SIZE), made in the constructor
    gd::Ref<ByteBlock> cells;                // ByteBlock.new(CELL_COUNT * CELL_SIZE), made in the constructor
    gd::Arr<gd::Ref<Spark>> sparks;          // context +0x14C: 64 rising streaks behind the portraits

    explicit CharacterSelect(GameFlow* flow);

    gd::Int rec(gd::Int player);
    gd::Int r32(gd::Int player, gd::Int at);
    gd::Int rs32(gd::Int player, gd::Int at);
    void w32(gd::Int player, gd::Int at, gd::Int v);
    gd::Int frames();
    // A cell's byte (the dummy cell reads 0x16 everywhere).
    gd::Int cell8(gd::Int cell, gd::Int at);
    gd::Int cell_key(gd::Int cell);
    void _cell_put8(gd::Int cell, gd::Int at, gd::Int v);
    void step();
    // FUN_8010F1B4: every 4th frame a new streak rises from alternating portraits (the frame
    // generator places it and sets its fade); FUN_8010F020 moves and fades each one.
    void _sparks();

    // ---- the grid (FUN_8010DC04, FUN_8010DA30, FUN_8010D810)

    // The portrait grid from the unlocked characters (cells 10 and 21 are hidden extras that only
    // start the NEW blink).
    void grid_build();
    // The cell's link toward the other row (up from the bottom row, down from the top).
    gd::Int _vertical(const gd::Ref<Row>& row, gd::Int cell);
    // FUN_8010D810: place one cell of a row and return the cell to pair with next.
    gd::Int _row_step(const gd::Ref<Row>& row, gd::Int cell);
    // FUN_8010DA30: centre the shorter row under the longer one and link the rows vertically.
    void _pair_rows(const gd::Ref<Row>& a, const gd::Ref<Row>& b);

    // ---- the players (FUN_8010E250)

    // One player's selection; true when this side is done.
    bool player_step(gd::Int player);
    // FUN_8010DF20: the starting cursor from the kept character (or the last choice).
    void _start(gd::Int player);
    gd::Int _find_cell(gd::Int key);
    // The cursor follows `pad`'s direction to the neighbouring cell (repeat for left and right,
    // repeat or press for up and down).
    void _move(gd::Int player, gd::Int pad);
    // FUN_8010E0D8: a face button (or the time running out) picks the costume; false once chosen.
    // □ and ✕ costume 0, ○ and △ costume 1, Start or L1 the Start costume when it is unlocked.
    // `pad`: whose pad chooses (the other side's in CHOOSING_OTHER), whose characters and costumes count.
    bool _choose(gd::Int player, gd::Int pressed, gd::Int pad);
    // Native: whether character `key` is pad `pad`'s to pick (outside a NETWORK MODE session: always).
    bool _owns(gd::Int pad, gd::Int key) const;
    // FUN_8010E948: the chosen characters into the fight variables and the select memory.
    void commit();
    // FUN_8010EA2C: every other frame one side's big portrait follows its cursor (the side's
    // picture id goes into the progress block, as the VS screen's does).
    void portrait_update();
};

}  // namespace t3
