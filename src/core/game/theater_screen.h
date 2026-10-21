#pragma once
// remake/core/game/theater_screen.gd
// THEATER MODE (ending.ovl, game state 19 with 0x800AE158 set; modes.md#theater), written from
// the verified port `tools/research/theater_sim.py` (entry_status, movie_input, sound_input) and the
// decompiled state handler FUN_8010E8C0. The movie page (a grid of the Tekken 3 movies) and the
// sound player (the Tekken 3 tracks, arranged or arcade). The Tekken 1 and 2 disc mode is out of
// scope: the DISC button is never offered. Without movies the Theater opens on the sound page;
// without music the sound page is not offered.
#include <string>

#include "gd/gd.h"
#include "core/game/flow_part.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;

class TheaterScreen : public FlowPart {
public:
    static constexpr gd::Int PAGE_MOVIES = 1;
    static constexpr gd::Int PAGE_ARRANGE = 2;
    static constexpr gd::Int PAGE_ARCADE = 3;
    static constexpr gd::Int COLUMNS = 6;
    static constexpr gd::Int ROWS_SHOWN = 4;
    static constexpr gd::Int LINES_SHOWN = 14;
    static constexpr gd::Int SOUND_MOVE = 0x546C;
    static constexpr gd::Int SOUND_OK = 0x4CEB;
    static constexpr gd::Int CHOOSE = PadState::CONFIRM;
    // The clear bits naming a shared ending slot: the first name, the second (NAMES in theater_sim).
    static inline gd::Dict<std::string, gd::PI32> SHARED_SLOTS = [] {
        gd::Dict<std::string, gd::PI32> d;
        d.set("0", gd::PI32{0x10, 0x80000});
        d.set("1", gd::PI32{0x800, 0x800});
        d.set("2", gd::PI32{0x10000, 0x10000});
        d.set("3", gd::PI32{0x4000, 0x100000});
        return d;
    }();
    // Not the RULES.md unscoped enum: Focus and Choice share EXIT and DISC, so each enum is wrapped
    // in a struct of the same name (INTERFACES.md): `Focus::LIST`, `Choice::PLAY`.
    struct Focus {
        enum : gd::Int { LIST, EXIT, SWITCH, DISC, BGM };
    };
    struct Choice {
        enum : gd::Int { NONE, PLAY, EXIT = 4, DISC = 5 };
    };

    gd::Int kind = 0;                        // 0x800AE158: 0 arcade ending, 1 movies, 2 arranged sound, 3 arcade sound
    gd::Int disc = 3;                        // 0x800AE159
    gd::Int movie = 0;                       // 0x800AE15C: the movie to play
    gd::Int track = 0;                       // 0x800AE15D: the sound list entry to play
    gd::Int cursor = 0;                      // 0x800AE15E
    gd::Int top = 0;                         // 0x800AE15F: the first row (grid) or line (list) shown
    gd::Int focus = Focus::LIST;             // 0x800AE162
    bool music_available = true;

    // FlowPart's _init(flow): TheaterScreen has no _init of its own.
    explicit TheaterScreen(GameFlow* flow);

    // FUN_8010FC7C: the movie a list entry plays, −1 while locked (−2 marks an empty cell).
    gd::Int entry_status(gd::Int i);
    // FUN_801109D4: 1 when no Tekken 3 movie is locked.
    gd::Int all_movies();
    // FUN_8010F634: an entry's name; names "0"–"3" stand for shared ending slots, named by the clears.
    std::string movie_name(gd::Int i);
    // FUN_80111A08.
    void reset();
    // Sub-state 0 of Theater: its first page.
    void open();
    // Sub-states 1 (the pages) and 10 (leaving).
    void step();
    // The movie has ended: back to the grid.
    void after_movie();
    gd::Int _input();
    gd::Vector2i _pads();
    // FUN_80111A68: the movie grid.
    gd::Int _movie_input();
    // FUN_80111FEC: the sound list.
    gd::Int _sound_input();
};

}  // namespace t3
