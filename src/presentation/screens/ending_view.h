#pragma once
// remake/presentation/screens/ending_view.gd: ending.ovl's screens after the movie: the staff roll
// (draw_sim roll_glow, roll_text, roll_logo: additive glyphs from the roll's font sheets, a glow behind
// the lines that fade in and out, the Namco logo) and THEATER MODE (theater_sim.theater_draw: the movie
// grid or the sound list, the buttons, the selected entry, the panels and the page title that gives way
// to PUSH START).
//
// The Theater's disc mode is out of scope (TheaterScreen): its DISC buttons are not drawn.
#include <string>

#include "core/game/staff_roll.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;
class TheaterScreen;

class EndingView : public PsxCanvas {
public:
    // The roll's fonts (FUN_8011484C): columns, v0, cell, space. Headings (StaffRoll.Line.wide) use
    // font 1 with the wide widths, names font 0 with the narrow widths.
    static inline const gd::Dict<bool, gd::Arr<gd::Int>> ROLL_FONTS = [] {
        gd::Dict<bool, gd::Arr<gd::Int>> d;
        d.set(true, gd::Arr<gd::Int>{0x15, 0, 0xC, 8});
        d.set(false, gd::Arr<gd::Int>{0xE, 0x24, 0x12, 0xC});
        return d;
    }();
    static constexpr gd::Int ROLL_CLUT = 0x1828;
    static constexpr gd::Int ROLL_TPAGE = 0x2A;
    static constexpr gd::Int LOGO_CLUT = 0x2C28;
    static constexpr gd::Int PICTURES = 0x800BA08CLL;
    static constexpr gd::Int CELL_FRAME = 0x800BA368LL;
    static constexpr gd::Int TIMS = 0x800BA374LL;
    static constexpr gd::Int PANELS = 0x800BA730LL;
    static constexpr gd::Int STRIPS = 0x800BA838LL;
    static constexpr gd::Int STRIP_PIECES = 0x800BA790LL;
    static constexpr gd::Int BANNERS = 0x8011DAA8LL;
    static constexpr gd::Int IDLE_FRAMES = 0x1E0;

    GameFlow* flow = nullptr;
    gd::Int frame_count = 0;
    gd::Int _idle = 0;
    gd::Ref<VramImage> _roll;
    gd::Ref<VramImage> _theater;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    void show_state(GameFlow* game);
    void _draw() override;

    // ---- the staff roll
    void _staff_roll();
    // FUN_8011484C: a credit line as glyphs 12 pixels high, modulated by the line's brightness.
    void _roll_text(const gd::Ref<StaffRoll::Line>& l);
    // FUN_80113A3C: the glow behind a line (25 Gouraud quads, additive).
    void _glow(const gd::Ref<StaffRoll::Line>& l);

    // ---- THEATER MODE
    void _theater_page();
    void _frame4(gd::Int x, gd::Int y, gd::Int w, gd::Int h, gd::Int side, gd::Int top, gd::Int colour);
    void _panel_frame(gd::Int k, gd::Int dx, gd::Int dy);
    // A panel: its Gouraud quad, semi-transparent with flag 2 (bug #36 not reproduced: the game
    // ORs the flag into the first corner's red), then its frame.
    void _panel(gd::Int k);
    void _sprite(gd::Int x, gd::Int y, gd::Int rec, gd::Int u, gd::Int w, gd::Int h, gd::Int tpage);
    // An 8-bit 48 x 64 movie picture (0: the locked picture).
    void _picture(gd::Int x, gd::Int y, gd::Int k);
    void _cell_frame(gd::Int x, gd::Int y);
    // FUN_8010FB24: TIM k (the disc logos 0-5, the sound picture 7) as one sprite.
    void _tim_sprite(gd::Int x, gd::Int y, gd::Int k);
    // Help strip k: pieces of a text picture in a row at y 400, centred; in English the USA
    // release's line k (ending.ovl FUN_80100834: font 0, colour 6, centred at 9 pixels a letter).
    void _strip(gd::Int k);
    void _help_strip(gd::Int focus, gd::Int page);
    gd::Int _pulse();
    // FUN_801106A8: a label in a white frame over a dark blue box, or a pulsing one when selected
    // (bug #37 not reproduced: the game starts the box 2 pixels above the frame).
    void _button(gd::Int focus, gd::Int which, gd::Int x, gd::Int y, const std::string& label);
    // FUN_80110A58: 6 x 4 pictures, the buttons and the selected movie's names, drawn in the
    // reverse of their linking: the names first, the cursor's frame last.
    void _movie_grid(const gd::Ref<TheaterScreen>& t);
    // The selected movie's picture, name and title lines (or ??? while locked), or the focused
    // button's help strip.
    void _movie_names(const gd::Ref<TheaterScreen>& t);
    // FUN_801112DC: 14 lines of the track list, the buttons, the selected track and the disc logos,
    // drawn in the reverse of their linking: the logos first, the list's frame last.
    void _sound_list(const gd::Ref<TheaterScreen>& t);
    // FUN_80112554: the page title; after 480 idle frames it alternates every 256 frames with
    // PUSH START TO PLAY, sliding for the last 32 frames of each half.
    void _banner(gd::Int page);
};

}  // namespace t3
