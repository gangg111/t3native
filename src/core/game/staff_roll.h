#pragma once
// remake/core/game/staff_roll.gd
// The staff roll after an arcade ending (ending.ovl FUN_80112798; modes.md#arcade-ending-and-
// staff-roll), written from the verified port `tools/research/screens_sim.py` (staff_roll) and the
// decompiled overlay (its sound and music). Each frame lists the lines to draw (`lines`) with
// their brightness and glow, and the Namco logo's brightness in the last phase.
#include <string>

#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class FlowData;
class GameFlow;

class StaffRoll {
public:
    // The roll's phases: the start sound, the music's delay and loading, the titles, the scrolling
    // rows, the wait after them and the logo.
    enum Phase : gd::Int { WAIT, MUSIC_DELAY, MUSIC_LOAD, TITLE_DELAY, TITLES, SCROLL, END_WAIT, LOGO };

    static constexpr gd::Int SOUND_START = 0xC7C0;
    static constexpr gd::Int START_DELAY_FRAMES = 30;       // phase frame counts
    static constexpr gd::Int MUSIC_DELAY_FRAMES = 240;
    static constexpr gd::Int TITLE_DELAY_FRAMES = 60;
    static constexpr gd::Int TITLES_FRAMES = 239;
    static constexpr gd::Int END_WAIT_FRAMES = 9060;
    static constexpr gd::Int MUSIC_ROLL = 4;
    static constexpr gd::Int SKIP_BUTTONS = PadState::CONFIRM;
    static constexpr gd::Int ROW_HEIGHT = 0x12;
    static constexpr gd::Int SCREEN_HEIGHT = 0x1E0;
    static constexpr gd::Int CENTRE = 0x98;
    static constexpr gd::Int TITLE_Y = 0xDC;
    static constexpr gd::Int END_HOLD = 0x26C;
    static constexpr gd::Int END_FADE = 0x50;
    static constexpr gd::Int LOGO_FRAMES = 0xB3;
    static inline const gd::Vector3i FULL_GLOW{0xFD, 0xDB, 0xB6};
    static inline const gd::Vector3i BASE_RGB{-3, -0x25, -0x4A};

    // A line drawn this frame: its text, left x, y, font (true: the heading font), brightness and,
    // when it glows, the glow's colour offsets.
    class Line {
    public:
        std::string text = "";
        gd::Int x = 0;
        gd::Int y = 0;
        bool wide = false;
        gd::Int bright = 0;
        bool glow = false;
        gd::Vector3i rgb;                    // Vector3i.ZERO
        gd::Int width = 0;                   // the placed text's width (the glow's extent)
    };

    gd::Ref<FlowData> data;
    gd::Int phase = Phase::WAIT;             // 0x80194620
    gd::Int clock = 0;                       // 0x8011E5F0
    gd::Int skip = 0;                        // 0x8011E5F4 (kept from the overlay's load)
    gd::Int end = 0;                         // 0x8011E5F8: 1 the list's end is on screen, 2 and 3 fading, 4 done
    gd::Int offset = 0;                      // 0x80194618: the first row's y
    gd::Int first = 0;                       // 0x8019461C: the first row shown
    gd::Int indent = 0x20;                   // 0x8019463C
    gd::Int half = 0;                        // 0x80194644: alternate frames rise 1 and 2 pixels
    gd::Int bright = 0;                      // 0x80194648 (u8)
    gd::Int base_bright = 0x78;              // 0x80194624
    gd::Vector3i rgb = BASE_RGB;             // 0x80194626: the glow's colour offsets (s8)
    gd::Int ramp = 0;                        // 0x80194658
    gd::Int hold = 0;                        // 0x80194660
    gd::Int fade = 0;                        // 0x8019465C
    gd::Int kind = 0;                        // the last row kind read (the caller's $s3 in the game)
    gd::Arr<gd::Ref<Line>> lines;
    gd::Int logo_bright = -1;                // Phase.LOGO: the logo's brightness, −1 not shown
    gd::Int _x0 = 0;
    gd::Int _y = 0;
    gd::Int _width = 0;
    gd::Int _ball_new = 0;                   // Tekken Ball is unlocked: its heading replaces another
    gd::Int _anna_start = 0;                 // Anna's Start costume is unlocked: one more name

    explicit StaffRoll(const gd::Ref<FlowData>& flow_data);

    // A new roll (the overlay just loaded): the skip and end flags start clear.
    void reset();
    // FUN_80112798(1): the set-up; returns the skip flag.
    gd::Int start();
    // FUN_80112798(0): one frame; true when the roll is over or skipped.
    bool step(GameFlow* g);
    // FUN_801144B0: a string's width in pixels.
    gd::Int text_width(const std::string& text, bool wide);
    void _place(const std::string& text, bool wide, gd::Int y);
    void _line(const std::string& text, bool wide, bool glow);
    void _glow_in(gd::Int k);
    // Phase 4: the title block, glowing in and fading out.
    void _titles(gd::Int c);
    // A row's colour entering at the bottom (y 360–440).
    void _ramp_near_bottom(gd::Int y);
    // A row's colour leaving at the top (y 20–100).
    void _ramp_near_top(gd::Int y);
    // The row's brightness by its place on the screen; true when it glows.
    bool _row_colour(gd::Int y_row, gd::Int y, gd::Int bottom, gd::Int top_lo, gd::Int top_len);
    // The end of the list: the hold (620 frames), the fade out and the glow out (80 each), counted
    // once per frame (bug #27 not reproduced: the game counts once per heading row on screen).
    // False when the roll is over (the phase changed).
    bool _end_tick();
    // Phase.SCROLL: the rows scroll up; false returns from the frame early (the phase changed).
    bool _scroll();
};

}  // namespace t3
