#pragma once
// remake/core/game/main_menu.gd
// The main menu (title.ovl FUN_800DBBD0, game state 4; menu_sim.main_menu, verified): the list of
// modes, the cursor (up / down with repeat, wrapping; VS MODE is skipped with one controller),
// the choice (Start or a face button) and the time-out back to the title after 480 frames.
#include "gd/gd.h"
#include "content/flow_data.h"     // FlowData::MenuEntry
#include "core/game/flow_part.h"
#include "core/game/game_mode.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;

class MainMenu : public FlowPart {
public:
    static constexpr gd::Int TIMEOUT = 0x1E1;
    static constexpr gd::Int SCROLL_STEP = 0x1C;
    static constexpr gd::Int CHOOSE_BUTTONS = PadState::CONFIRM;
    static constexpr gd::Int SOUND_CURSOR = 0x546C;
    static constexpr gd::Int SOUND_CHOOSE = 0x4D87;
    static constexpr gd::Int MODE_BALL = GameMode::BALL;
    static constexpr gd::Int MODE_FORCE = GameMode::FORCE;
    static constexpr gd::Int MODE_OPTIONS = 9;
    static constexpr gd::Int MODE_THEATER = 10;
    // Native: NETWORK MODE's entry (GameFlow.network_entry), in colour 11, which no mode uses.
    static constexpr gd::Int MODE_NETWORK = 11;
    static constexpr gd::Int NETWORK_COLOUR = 11;
    static constexpr gd::Int SUB_NETWORK = 4;

    class Entry {
    public:
        gd::Ref<FlowData::MenuEntry> entry;
        // GDScript `new` (a C++ keyword): renamed `new_`.
        bool new_ = false;                   // +7: the NEW mark
    };

    gd::Arr<gd::Ref<Entry>> entries;         // 0x800EC590: the entries shown
    gd::Int scroll = 0;                      // 0x800EC588: the list's slide offset
    gd::Int glow = 0;                        // 0x800EC58C
    gd::Int chosen = 0;                      // 0x800EC580
    gd::Int players = 0;                     // 0x800EC574: the pads that chose

    // FlowPart's _init(flow): MainMenu has no _init of its own.
    explicit MainMenu(GameFlow* flow);

    gd::Int cursor();
    // The drawing's own state (menu_sim._main_menu_draw): the slide decays by a quarter each frame
    // (rounded up), and the choice bars glow in once the list rests.
    void draw_step();
    void step();
    void _step();
    // Sub-state 0: the controllers reset, the players and the key configuration, the entries (Tekken
    // Ball and Theater once unlocked, NEW while their counter is below 3).
    void _open();
    // FUN_800DE7B4: a player's button layout (the save block's actions per pad button) into the
    // key table the pads are read with.
    void button_map(gd::Int player);
};

}  // namespace t3
