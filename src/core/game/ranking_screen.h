#pragma once
// remake/core/game/ranking_screen.gd
// The ranking (modes.md#ranking-screen): its loading (RankingLoad 0x800504A8, game state 16)
// and the pages (ranking.ovl FUN_800C2A24, game state 17), written from the verified port
// `tools/research/screens_sim.py` (ranking_frame, name_entry, the page tables, the backdrop camera).
// The pages cycle time attack, survivors and usage; after a new time attack or survival record
// the name is entered on its page. Behind the table a stage turns (a stage without fighters).
//
// The table (0x800CBE68): +0 mode (0 hidden, 1 fading in, 2 rows sliding, 3 name entry, 4 name
// entered), +2 y, +4 target y, +6 hold frames, +8 frames per group, +0xA alpha, +0xC rows,
// +0xE u16 per row the character, +0x50 s32 first row of the group, +0x54 rows shown, +0x58 u16
// per character the usage share (‰), +0x84 the top share, +0x86 the name's row on screen, +0x88
// the name's row (0xFFFF none). The name record (0x800D36F8): char[4], +4 u16 position, +6 u8
// pressed, +7 u8 moved, +8 u16 cursor, +0xA u16 character, +0xC u16 player.
#include <string>

#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;
class ByteBlock;

class RankingScreen : public FlowPart {
public:
    static constexpr gd::Int TABLE_BASE = 0x800CBE68LL;
    static constexpr gd::Int TABLE_SIZE = 0x8C;
    static constexpr gd::Int T_MODE = 0;                    // table fields (see above)
    static constexpr gd::Int T_Y = 2;
    static constexpr gd::Int T_TARGET_Y = 4;
    static constexpr gd::Int T_HOLD = 6;
    static constexpr gd::Int T_GROUP_FRAMES = 8;
    static constexpr gd::Int T_ALPHA = 0xA;
    static constexpr gd::Int T_ROWS = 0xC;
    static constexpr gd::Int T_CHARS = 0xE;
    static constexpr gd::Int T_FIRST_ROW = 0x50;
    static constexpr gd::Int T_SHOWN = 0x54;
    static constexpr gd::Int T_USAGE = 0x58;
    static constexpr gd::Int T_TOP_SHARE = 0x84;
    static constexpr gd::Int T_NAME_SCREEN_ROW = 0x86;
    static constexpr gd::Int T_NAME_ROW = 0x88;
    static constexpr gd::Int NAME_BASE = 0x800D36F8LL;
    static constexpr gd::Int NAME_SIZE = 0x10;
    static constexpr gd::Int N_POS = 4;                     // name record fields (see above)
    static constexpr gd::Int N_PRESSED = 6;
    static constexpr gd::Int N_MOVED = 7;
    static constexpr gd::Int N_CURSOR = 8;
    static constexpr gd::Int N_CHARACTER = 0xA;
    static constexpr gd::Int N_PLAYER = 0xC;
    static constexpr gd::Int BACKDROP_BASE = 0x800D3708LL;
    static constexpr gd::Int BACKDROP_SIZE = 0x30;
    static constexpr gd::Int TIME_CAP = 359999;
    static constexpr gd::Int ROW_HEIGHT = 0x48;
    static constexpr gd::Int ROWS_TOP = 0xA8;
    static constexpr gd::Int GROUP = 5;
    static constexpr gd::Int GROUP_FRAMES = 150;
    static constexpr gd::Int NAME_FRAMES = 3000;
    static constexpr gd::Int SURVIVORS = 10;
    static constexpr gd::Int NO_ROW = 0xFFFF;
    static constexpr gd::Int STAGES = 13;
    static constexpr gd::Int ORBIT_START = 0xC00;
    static constexpr gd::Int ORBIT_RADIUS = 5000;
    static constexpr gd::Int ORBIT_STEP = 3;
    static constexpr gd::Int EYE_HEIGHT = -0x578;

    // Not the RULES.md unscoped enum: TableMode and Sub share NAME_ENTRY and NAME_ENTERED, so each
    // is wrapped in a struct of the same name (INTERFACES.md): `TableMode::HIDDEN`, `Sub::LEAVE`.
    // The table's mode (+0).
    struct TableMode {
        enum : gd::Int { HIDDEN = 0, FADING_IN = 1, SLIDING = 2, NAME_ENTRY = 3, NAME_ENTERED = 4 };
    };

    // How `name_entry` runs a frame: from the pad, the time ran out, or skipped with Start.
    enum NameInput : gd::Int { PAD = 0, TIMEOUT = 1, SKIP = 2 };

    // The page sub-states of game state 17 (`g.sub`).
    struct Sub {
        enum : gd::Int {
            START = 0, SETUP = 1, FADE_IN_START = 2, FADE_IN = 3, GROUP_START = 4, SLIDE = 5, HOLD = 6,
            SHOW = 7, FADE_OUT_START = 8, FADE_OUT = 9, NAME_SCROLL_START = 0xA, NAME_SCROLL = 0xB,
            NAME_ENTRY = 0xC, NAME_ENTERED = 0xD, LEAVE = 0xE,
        };
    };

    gd::Ref<ByteBlock> table;                // ByteBlock.new(TABLE_SIZE), made in the constructor
    gd::Ref<ByteBlock> name;                 // ByteBlock.new(NAME_SIZE)
    gd::Ref<ByteBlock> backdrop;             // ByteBlock.new(BACKDROP_SIZE)
    gd::Int fade = -1;                       // the fade drawn this frame (FUN_8004E2E8 level), −1 none

    // FlowPart's _init(flow); the constructor makes `table`, `name` and `backdrop`.
    explicit RankingScreen(GameFlow* flow);

    // ---- loading (game state 16)
    void load_step();
    // FUN_800C390C: the stage behind the ranking (the next of the cycle, skipping four) and its camera.
    void _backdrop_setup();
    static gd::Int _round12(gd::Int v);
    // FUN_800C3AAC: the camera turns around the stage (its eye at +0x14 … +0x1C); FUN_80046458 makes
    // it the game's camera, and the background (FUN_8006DAB4) turns the panorama with it.
    void _backdrop_camera();

    // ---- the pages (game state 17)
    void step();
    // Start leaves for the main menu, taking a pending name as it stands.
    void _skip_to_menu();
    // Without a pending name, player 2's stick picks the page: right time attack, left survivors,
    // up usage.
    void _page_from_pad();
    // The page's table; a pending name (bit 0 time attack, bit 1 survival) opens its page with the
    // name entry, a name for neither only shows the usage page.
    void _set_up_page();
    void _fade_in(bool first);
    void _fade_out(bool first);
    // Holds the finished table for `frames` frames, then fades out.
    void _wait(gd::Int frames);
    // The rows slide in from below, the last group first.
    void _group_start();
    void _slide();
    // A group's rows fade in and hold, then the next group slides (or the table shows).
    void _hold();
    // The table scrolls so the new record's row shows (as the third of five where it can).
    void _name_scroll_start();
    void _name_scroll();
    // One frame of the name entry, which ends when the name is complete or the time runs out.
    void _name_entry_frame();
    // The drawing half: the backdrop camera turns while the table shows.
    void _draw();
    // The entered name into the new records (time attack, survivors).
    void _store_name(gd::Int pending);
    // The name so far (up to its terminator).
    std::string entered_name();

    // ---- the page tables
    // FUN_800C1D1C: time attack: the ten original characters and every other with a record, fastest
    // first; the row of the record just set (−1 none).
    gd::Int _time_page();
    // FUN_800C1E8C and the page set-up: the survivors re-sorted by wins; the row of the entry just
    // made (−1 none).
    gd::Int _survivor_page();
    // FUN_800C1FA8: usage: the ten original characters and every other played, most used first,
    // with their shares.
    void _usage_page();
    // FUN_8004D008: part · 1000 / total, scaled down to avoid overflow.
    static gd::Int permille(gd::Int part, gd::Int total);

    // ---- the name entry (FUN_800C3438, FUN_800C3480)
    void _name_init(gd::Int player, gd::Int character);
    void _put_text(const std::string& text);
    // The character's name (its first three letters).
    void _default_name();
    void _finish_name();
    // FUN_800C3480: one frame of input, the time ran out, or take the character's name (NameInput);
    // true when the name is complete.
    bool name_entry(gd::Int mode);
};

}  // namespace t3
