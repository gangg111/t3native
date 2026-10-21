#pragma once
// remake/presentation/screens/ranking_view.gd: the ranking pages (ranking.ovl FUN_800C2A24's
// drawing, draw_sim.ranking_screen_draw): the page header picture with its shaded strip, and the rows
// (rank, face, name, time / wins / usage share and bar, entered name) clipped below the header,
// sliding up; during name entry the new row blinks on a blue band with the cursor under the letter.
// The stage turns behind (StageBackdrop).
#include <string>

#include "core/game/ranking_screen.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;

class RankingView : public PsxCanvas {
public:
    static constexpr gd::Int T = RankingScreen::TABLE_BASE;
    static constexpr gd::Int NAME = RankingScreen::NAME_BASE;
    static constexpr gd::Int STR_RANK = 0x800B9540LL;
    static constexpr gd::Int STR_S = 0x800B9550LL;
    static constexpr gd::Int STR_TICKS = 0x800B9560LL;
    static constexpr gd::Int STR_TIME = 0x800B9570LL;
    static constexpr gd::Int STR_100 = 0x800B958CLL;
    static constexpr gd::Int STR_0 = 0x800B95A4LL;
    static constexpr gd::Int STR_PCT = 0x800B95BCLL;
    static constexpr gd::Int STR_3D = 0x800B95DCLL;
    static constexpr gd::Int STR_WIN = 0x800B95ECLL;
    static constexpr gd::Int STR_WINS = 0x800B95FCLL;
    static constexpr gd::Int STR_2D = 0x800B960CLL;
    static constexpr gd::Int ORDINALS = 0x800CBE18LL;
    static constexpr gd::Int STYLE_TIME = 0x800CBE28LL;
    static constexpr gd::Int STYLE_SURV = 0x800CBE34LL;
    static constexpr gd::Int STYLE_USE = 0x800CBE3CLL;
    static constexpr gd::Int BAR_COLOURS = 0x800B9378LL;
    static constexpr gd::Int RECORDS = 0x8009832CLL;
    static constexpr gd::Int SURVIVORS = 0x800983DCLL;
    static constexpr gd::Int PAGE = 0x800984DCLL;
    static constexpr gd::Int TIME_CAP = 359'999;
    static gfx::Rect2 ROWS_CLIP() { return gfx::Rect2(0, 0x60, 0x170, 0x168); }

    gd::Ref<GameFlow> flow;
    gd::Int frame_count = 0;
    gd::Arr<gd::Ref<VramImage>> _pages;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& screens_dir);
    void show_state(const gd::Ref<GameFlow>& game);
    gd::Int _mode();
    void _draw() override;
    void _header();
    // The header's shaded strip (POLY_GT4, added with CLUT 0x7F50 and subtracted with 0x7FD0).
    void _strip(gd::Int clut);
    void _rows(bool texts);
    void _front();
    std::string _name_of(gd::Int character);
    std::string _rank(gd::Int rank, gd::Int colour, gd::Int y);
    void _row_lines(gd::Int y);
    // FUN_800C133C: 1ST  <face> NAME  mm'ss"hh  entered name.
    void _time_row(gd::Int rank, gd::Int character, gd::Int rec, gd::Int y, gd::Int style, bool texts);
    // FUN_800C16C0: 1ST  <face> NAME  n WIN(S)  entered name.
    void _survivor_row(gd::Int rank, gd::Int e, gd::Int y, gd::Int style, bool texts);
    // FUN_800C19C4: 1ST  <face> NAME  share %  and a bar relative to the most used character.
    void _usage_row(gd::Int rank, gd::Int character, gd::Int share, gd::Int top, gd::Int anim, gd::Int y, bool texts);
    // FUN_800C1154: a usage bar, 129 x share / 1000 long, in the character's colours.
    void _usage_bar(gd::Int character, gd::Int share, gd::Int x, gd::Int y);
};

}  // namespace t3
