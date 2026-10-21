#pragma once
// remake/presentation/screens/move_list_view.gd: the in-fight move lists (FUN_800789E4 with the text
// engine MoveTextDraw 0x80077A0C; the verified port tools/research/command_list_sim.py) while the
// MOVE_LIST event says they are shown (MoveListScreen: the pause menu's COMMAND page, practice's
// COMMAND LIST).
//
// Each side's list is a 182-pixel column (player 2's from x 184): a dark column with two light
// lines and the title (glyphs 6-10 of the side's atlas), six moves 64 pixels apart in boxes
// clipped between the lines, and PUSH BUTTON TO EXIT under the column of the side that leaves.
// Meanwhile the game gives the scene's ordering table an empty draw area (the pause page keeps
// its top 32 lines): the paused scene is not redrawn and stays in the frame buffers as it was
// drawn during the page's delay frames, so the lists lie over the frozen fight.
//
// The move strings (formats/arc-archives.md, "Move-text rendering (Japan Rev.1)") are glyphs of
// the character's own atlas (four 12 x 11 glyph sets in the bit planes of one picture, see
// `move_glyphs.png`), direction arrows and button diagrams (title.ovl's picture at tpage 6).
// With English game texts a side whose USA list was converted is drawn as the USA release
// draws it ("Move-text rendering (USA)": MoveTextDraw 0x80077724 of SLUS_004.02): ASCII in a
// 6 x 12 font and pre-rendered words from one shared atlas (`usa/move_glyphs.png`), its own
// codes for arrows, buttons, spaces and line breaks, and names measured in cells.
#include <string>
#include <variant>

#include "gd/json.h"
#include "presentation/screens/fight_canvas.h"

namespace t3 {

class GameFlow;

class MoveListView : public FightCanvas {
public:
    static constexpr gd::Int LISTS_TOP = 0x74;              // the rows' draw area: y 116 ... 435
    static constexpr gd::Int LISTS_HEIGHT = 0x13F;
    static constexpr gd::Int COLUMN = 0xB8;
    static constexpr gd::Int ROWS = 6;
    static constexpr gd::Int ROW_STEP = 64;
    static constexpr gd::Int NAME_WIDTH = 0x1C;             // half-cells of a name's first line
    static constexpr gd::Int TEXT_COLOUR = 0x707070;
    static constexpr gd::Int EXIT_Y = 0x1B8;
    static constexpr gd::Int STR_EXIT = 0x80027E70LL;       // "%p%f%H%V%cPUSH %cBUTTON %cTO %cEXIT"
    static constexpr gd::Int STR_TITLE = 0x80098DF0LL;      // "\xFFp" and glyphs 6-10
    static constexpr gd::Int FONTS = 0x80027D1CLL;          // 4 x (s16 advance x, advance y, u16 w, h, tpage)
    static constexpr gd::Int ARROWS = 0x80027D44LL;         // 9 x (u16 uv, u16 flip flags: 1 horizontal, 2 vertical)
    static constexpr gd::Int ICON_TPAGE = 6;
    static constexpr gd::Int ARROW_HELD_CLUT = 0x7FB3;
    static constexpr gd::Int ARROW_CLUT = 0x7FB4;
    static constexpr gd::Int BUTTON_BACK_CLUT = 0x7FB1;
    static constexpr gd::Int ATLAS_V = 0x10;                // the atlas's first texel row in its texture page
    static constexpr gd::Int ATLAS_ROWS = 66;               // a plane's rows in move_glyphs.png
    static constexpr gd::Int ESCAPE = 0xFF;
    static constexpr gd::Int USA_TITLE_X = 0x27;            // the USA title's pen x in the column
    static constexpr gd::Int USA_ARROWS = 0x80;             // USA codes: arrows 0x80-0x90 (0x90 neutral), buttons 0x91-0xA0
    static constexpr gd::Int USA_BUTTONS = 0x91;
    static constexpr gd::Int USA_LINE_BREAK = 0xFC;
    static constexpr gd::Int USA_WIDE_SPACE = 0xFB;
    static constexpr gd::Int BOX_EDGE = 0x602010;
    static constexpr gd::Int BOX_FILL = 0xB89246;
    static constexpr gd::Int BOX_FRAME = 0xD0D0D0;
    static constexpr gd::Int LINE = 0xE0E0E0;
    // Hwoarang's move groups: (first move, last move or -1, edge, fill).
    static inline const gd::Arr<gd::Arr<gd::Int>> HWOARANG_GROUPS{gd::Arr<gd::Int>{6, 13, 0x204000, 0x5FA255},
                                                                  gd::Arr<gd::Int>{14, -1, 0x103040, 0x2090A0}};

    // The text engine's arguments (an untyped Array of ints and, for the `s` escape, nested strings).
    using Arg = std::variant<gd::Int, gd::PBytes>;

    // Native: the lists fill columns 0-367, the frame shows 8-375 (HudFrame): a window as wide as the
    // 4:3 frame (a tall one) cut the left list's first 8 columns. Moved onto the shown columns.
    static constexpr float SHOWN_SHIFT = 8.0f;

    std::string root = "";
    GameFlow* flow = nullptr;
    bool shown = false;
    gd::Int mask = 0;
    gd::Int exit_side = 0;
    gd::Dict<gd::Int, gfx::TextureRef> _atlases;   // costume slot -> Texture2D
    gd::Json _usa;                                 // usa/move_text.json: fonts, word widths, title
    gfx::TextureRef _usa_atlas;
    bool _english = false;                         // the list being drawn is in the USA encoding
    // The text engine's pen (0x1F800380 while a string draws, kept at 0x800A39B0 between strings).
    gd::Int _x = 0;
    gd::Int _y = 0;
    gd::Int _margin_x = 0;
    gd::Int _line_y = 0;
    gfx::Color _colour = gfx::Color::white();
    gd::Int _font = 0;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
               const std::string& assets_root);
    void show_state(GameFlow* game);
    void _draw() override;
    gfx::Vec2 pt(float x, float y) const override { return FightCanvas::pt(x + SHOWN_SHIFT, y); }
    void _rows_pass();
    // The column, its lines and its title (the lists' ordering-table entry 0, under the rows).
    void _column(gd::Int p);
    // The six moves from the scrolled position: the name (on two lines when wider than 28
    // half-cells), the command, the box; each row drawn before the one above it.
    void _rows(gd::Int p);
    void _row(gd::Int p, gd::Int character, const gd::PBytes& list, gd::Int y, gd::Int idx, gd::Int s);
    void _style(gd::Int p);
    void _position(gd::Int x, gd::Int y);
    gd::Int _advance_x();
    gd::Int _advance_y();
    // Field k (advance x, advance y, glyph w, h, texture page) of the USA font in use.
    gd::Int _usa_font(gd::Int k);
    // The converted USA list data, loaded on first use; false when missing.
    bool _load_usa();
    gd::PBytes _usa_title();
    gd::Int _name_width(const gd::PBytes& list, gd::Int at);
    // Where a name's first line ends and its second line starts (Vector2i).
    gd::Vector2i _name_end(const gd::PBytes& list, gd::Int at, gd::Int width);
    // USA FUN_80079208: a string's width in cells (a line break counts 100, 0xFB ten, the words
    // their widths, bytes from 0xA1 two, the rest one).
    gd::Int _usa_width(const gd::PBytes& list, gd::Int at);
    gd::Int _usa_weight(gd::Int c, bool line_break_counts);
    // USA FUN_80079170: the first line is copied until `width` cells are used or a line break; the
    // second line starts after the break (skipped) or where the copy stopped.
    gd::Vector2i _usa_end(const gd::PBytes& list, gd::Int at, gd::Int width);
    // FUN_800795E4: past `n` zero-terminated strings.
    static gd::Int _skip_strings(const gd::PBytes& list, gd::Int at, gd::Int n);
    static gd::Int _weight(gd::Int c);
    // FUN_80079664: a string's width in half-cells (joiners and half spaces 1, the rest 2).
    static gd::Int _text_width(const gd::PBytes& list, gd::Int at);
    // FUN_80079610: where copying `width` half-cells of the string stops.
    static gd::Int _width_end(const gd::PBytes& list, gd::Int at, gd::Int width);
    // MoveTextDraw (0x80077A0C) of the string at `at`: glyphs of the font's atlas, arrows and
    // button diagrams; 0xFF escapes n (new line), s (a nested string of glyphs), H / V (pen x / y),
    // h / v (in advances), i (colour), p (ordering-table entry: the drawing order here) and t (font).
    void _draw_string(const gd::PBytes& bytes, gd::Int at, const gd::Arr<Arg>& args);
    // An escape (after 0xFF): n (new line), s (a nested string: glyph byte - 1, in the USA engine
    // ASCII), H / V (pen x / y), h / v (in advances), i (colour), p (ordering-table entry: the drawing
    // order here) and t (font).
    void _escape(gd::Int c, const gd::Arr<Arg>& args, bool usa);
    // USA MoveTextDraw (0x80077724 of SLUS_004.02): as the Japanese engine with the same escapes,
    // but ASCII from 0x21, pre-rendered words (0x01-0x1F, 0xA1-0xC3), arrows 0x80-0x90, button
    // diagrams 0x91-0xA0, spaces 0x20 / 0xFE (one cell), 0xFD (two), 0xFB (ten) and 0xFC a line break.
    void _draw_usa(const gd::PBytes& bytes, gd::Int at, const gd::Arr<Arg>& args);
    // An ASCII letter of the USA atlas: u = 6 * (c & 15), v = 12 * (c >> 6), bit plane (c >> 4) & 3.
    void _usa_letter(gd::Int c);
    // `width` texels of the USA atlas from (u, v) in bit plane `plane` at the pen, which moves on by
    // `advance`.
    void _usa_cell(gd::Int u, gd::Int v, gd::Int plane, gd::Int width, gd::Int advance);
    // Glyph `g` of the font's atlas (12 x 11 cells, ten a row, the bit plane (g / 10) & 3); the
    // pen moves on by the advance.
    void _glyph(gd::Int g);
    // FUN_800778A8: a 20 x 32 direction arrow (index & 8: held; 0x10 the neutral star), one of three
    // pictures flipped, from 12 pixels above the pen.
    void _arrow(gd::Int index);
    // A button diagram for `buttons` (1 LP, 2 RP, 4 LK, 8 RK): the four buttons' outline, and over
    // it the pressed ones through the CLUT of the mask.
    void _button(gd::Int buttons);
    // The glyph atlas of font 0 or 1: the side's fighter's costume.
    gfx::TextureRef _atlas(gd::Int font);
};

}  // namespace t3
