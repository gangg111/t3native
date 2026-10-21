// remake/presentation/screens/move_list_view.gd
#include "presentation/screens/move_list_view.h"

#include <cstdio>

#include "content/fight_content.h"
#include "content/game_ram.h"
#include "content/json_file.h"
#include "content/text_locale.h"
#include "content/texture_packs.h"
#include "content/vram_image.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/move_list_screen.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_state.h"
#include "core/game/game_flow.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

namespace {

// `"%X" % c` and `"%02d" % v`.
std::string _format(const char* fmt, gd::Int v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, static_cast<long long>(v));
    return buf;
}

}  // namespace

void MoveListView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                         const std::string& assets_root) {
    setup_psx(hud_data, game_ram, image);
    root = assets_root;
    add_pass(gfx::Blend::MIX, [this] { _rows_pass(); }, gfx::Rect2(0, LISTS_TOP, SCREEN().x, LISTS_HEIGHT));
}

void MoveListView::show_state(GameFlow* game) {
    flow = game;
    gd::Ref<SimEvents::Event> e = game->sim->events->first(SimEvents::Kind::MOVE_LIST);
    shown = e != nullptr;
    if (shown) {
        mask = e->a;
        exit_side = e->b;
    }
    visible = shown;
    redraw();
}

void MoveListView::_draw() {
    begin();
    if (!shown) return;
    for (gd::Int p = 0; p < 2; ++p) {
        if ((gd::shr(mask, p) & 1) != 0) _column(p);
    }
    ptext(STR_EXIT, {0, 0, (exit_side * COLUMN) | 6, EXIT_Y, 6, 5, 6, 5});
}

void MoveListView::_rows_pass() {
    if (!shown) return;
    for (gd::Int p = 0; p < 2; ++p) {
        if ((gd::shr(mask, p) & 1) != 0) _rows(p);
    }
}

// The column, its lines and its title (the lists' ordering-table entry 0, under the rows).
void MoveListView::_column(gd::Int p) {
    gd::Int x0 = COLUMN * p;
    fill(x0 | 1, 0x5C, 0xB6, 0x170, psx(BOX_EDGE));
    fill(x0 | 2, 0x1B3, 0xB4, 2, psx(LINE));
    fill(x0 | 2, 0x72, 0xB4, 2, psx(LINE));
    _style(p);
    if (_english) {
        _draw_string(_usa_title(), 0, gd::Arr<Arg>{Arg(x0 + USA_TITLE_X), Arg(gd::Int(0x61)), Arg(gd::Int(0))});
        return;
    }
    _position(x0 + 0x3C, 0x61);
    _draw_string(ram->bytes(STR_TITLE, 16), 0, gd::Arr<Arg>{Arg(gd::Int(0))});
}

// The six moves from the scrolled position: the name (on two lines when wider than 28
// half-cells), the command, the box; each row drawn before the one above it.
void MoveListView::_rows(gd::Int p) {
    gd::Ref<FighterState> f = flow->fight->fighters[p];
    gd::PBytes list = f->move_text;
    gd::Int count = MoveListScreen::count(f);
    if (count == 0) return;
    gd::Int off = flow->fight->command_offset[p];
    gd::Int shift = Fx::div_trunc(off, ROW_STEP) + (off > 0 ? 1 : 0);
    gd::Int y = off + 0x76 - ROW_STEP * shift;
    gd::Int idx = gd::Int(flow->fight->command_cursor[p]) - shift;
    idx = gd::posmod(idx, count);
    gd::Int s = _skip_strings(list, 1, 2 * idx);
    gd::Int rows = ROWS;
    if (f->char_id == Character::MOKUJIN) {
        y = 0xF6;
        rows = 1;
    }
    gd::Arr<gd::Arr<gd::Int>> laid;   // [y, idx, s] per row
    for (gd::Int r = 0; r < rows; ++r) {
        laid.append(gd::Arr<gd::Int>{y, idx, s});
        s = _skip_strings(list, s, 2);
        idx += 1;
        if (idx >= count) {
            s = 1;
            idx = 0;
        }
        y += ROW_STEP;
    }
    laid.reverse();
    for (const gd::Arr<gd::Int>& row : laid) _row(p, f->char_id, list, row[0], row[1], row[2]);
}

void MoveListView::_row(gd::Int p, gd::Int character, const gd::PBytes& list, gd::Int y, gd::Int idx, gd::Int s) {
    gd::Int x0 = COLUMN * p;
    gd::Int edge = BOX_EDGE;
    gd::Int fill_colour = BOX_FILL;
    bool boxed = true;
    if (character == Character::HWOARANG) {
        for (const gd::Arr<gd::Int>& group : HWOARANG_GROUPS) {
            gd::Int first = group[0];
            gd::Int last = group[1];
            if (idx >= first && (last < 0 || idx <= last)) {
                edge = group[2];
                fill_colour = group[3];
                if (idx == first) {
                    fill_colour = BOX_EDGE;
                    boxed = false;
                }
            }
        }
    }
    if (boxed) {
        fill(x0 | 2, y - 3, 0xB4, 0x42, psx(BOX_FRAME));
        fill(x0 | 3, y - 2, 0xB2, 0x3F, psx(edge));
        gradient(x0 | 3, y - 2, 0xB2, 0x1A, psx(edge), psx(edge), psx(fill_colour), psx(fill_colour));
    } else {
        fill(x0 | 2, y - 2, 0xB4, 0x3F, psx(edge));
        fill(x0 | 3, y - 2, 0xB2, 0x1A, psx(fill_colour));
    }
    _style(p);
    // The command, then the name over it (the game links the name first).
    gd::Int command_at = _skip_strings(list, s, 1);
    _position(x0 + 6, y + 0x27);
    _draw_string(list, command_at, gd::Arr<Arg>{});
    if (_name_width(list, s) >= NAME_WIDTH + 1) {
        gd::Vector2i cut = _name_end(list, s, NAME_WIDTH);
        _position(x0 + 6, y);
        _draw_string(list.slice(s, cut.x), 0, gd::Arr<Arg>{});
        _position(x0 + 6, y + 0xC);
        _draw_string(list, cut.y, gd::Arr<Arg>{});
    } else {
        _position(x0 + 6, y + 6);
        _draw_string(list, s, gd::Arr<Arg>{});
    }
}

void MoveListView::_style(gd::Int p) {
    _colour = modulation(psx(TEXT_COLOUR));
    _font = p;
    gd::Ref<FighterState> f = flow->fight->fighters[p];
    _english = flow->content->move_text_english(f->costume_slot) && _load_usa();
}

void MoveListView::_position(gd::Int x, gd::Int y) {
    _x = x;
    _margin_x = x;
    _y = y;
    _line_y = y;
}

gd::Int MoveListView::_advance_x() {
    return _english ? _usa_font(0) : ram->s16(FONTS + 10 * _font);
}

gd::Int MoveListView::_advance_y() {
    return _english ? _usa_font(1) : ram->s16(FONTS + 10 * _font + 2);
}

// Field k (advance x, advance y, glyph w, h, texture page) of the USA font in use.
gd::Int MoveListView::_usa_font(gd::Int k) {
    const gd::Json& fonts = _usa["fonts"];
    return JsonFile::number(fonts[gd::clampi(_font, 0, fonts.size() - 1)][k]);
}

// The converted USA list data, loaded on first use; false when missing.
bool MoveListView::_load_usa() {
    if (_usa.is_object() && !_usa.members().empty()) return true;
    gd::Ref<TextLocale> locale = ram->locale;
    std::string path = locale != nullptr ? locale->usa_file("move_text.json") : "";
    std::string atlas = locale != nullptr ? locale->usa_file("move_glyphs.png") : "";
    if (path.empty() || atlas.empty()) return false;
    _usa = JsonFile::read(path);
    _usa_atlas = TexturePacks::texture(atlas);   // load(atlas) as Texture2D
    return true;
}

gd::PBytes MoveListView::_usa_title() {
    gd::PBytes out;
    for (gd::Int b : JsonFile::ints(_usa["title"])) out.append(b);
    return out;
}

gd::Int MoveListView::_name_width(const gd::PBytes& list, gd::Int at) {
    return _english ? _usa_width(list, at) : _text_width(list, at);
}

// Where a name's first line ends and its second line starts (Vector2i).
gd::Vector2i MoveListView::_name_end(const gd::PBytes& list, gd::Int at, gd::Int width) {
    if (_english) return _usa_end(list, at, width);
    gd::Int end = _width_end(list, at, width);
    return gd::Vector2i(end, end);
}

// USA FUN_80079208: a string's width in cells (a line break counts 100, 0xFB ten, the words
// their widths, bytes from 0xA1 two, the rest one).
gd::Int MoveListView::_usa_width(const gd::PBytes& list, gd::Int at) {
    gd::Int n = 0;
    while (at < list.size() && gd::Int(list[at]) != 0) {
        n += _usa_weight(list[at], true);
        at += 1;
    }
    return n;
}

gd::Int MoveListView::_usa_weight(gd::Int c, bool line_break_counts) {
    if (c == USA_LINE_BREAK) return line_break_counts ? 100 : 0;
    if (c == USA_WIDE_SPACE) return 10;
    if (c >= 0xA1) return 2;
    if (c < 0x20) return _usa["word_widths"][c].to_int();
    return 1;
}

// USA FUN_80079170: the first line is copied until `width` cells are used or a line break; the
// second line starts after the break (skipped) or where the copy stopped.
gd::Vector2i MoveListView::_usa_end(const gd::PBytes& list, gd::Int at, gd::Int width) {
    while (width > 0 && at < list.size()) {
        gd::Int c = list[at];
        if (c == USA_LINE_BREAK) return gd::Vector2i(at, at + 1);
        width -= _usa_weight(c, false);
        at += 1;
    }
    return gd::Vector2i(at, at);
}

// FUN_800795E4: past `n` zero-terminated strings.
gd::Int MoveListView::_skip_strings(const gd::PBytes& list, gd::Int at, gd::Int n) {
    while (n > 0 && at < list.size()) {
        while (at < list.size() && gd::Int(list[at]) != 0) at += 1;
        at += 1;
        n -= 1;
    }
    return at;
}

gd::Int MoveListView::_weight(gd::Int c) {
    return c == 0xFE || (c >= 1 && c <= 5) ? 1 : 2;
}

// FUN_80079664: a string's width in half-cells (joiners and half spaces 1, the rest 2).
gd::Int MoveListView::_text_width(const gd::PBytes& list, gd::Int at) {
    gd::Int n = 0;
    while (at < list.size() && gd::Int(list[at]) != 0) {
        n += _weight(list[at]);
        at += 1;
    }
    return n;
}

// FUN_80079610: where copying `width` half-cells of the string stops.
gd::Int MoveListView::_width_end(const gd::PBytes& list, gd::Int at, gd::Int width) {
    while (width > 0 && at < list.size()) {
        width -= _weight(list[at]);
        at += 1;
    }
    return at;
}

// MoveTextDraw (0x80077A0C) of the string at `at`: glyphs of the font's atlas, arrows and
// button diagrams; 0xFF escapes n (new line), s (a nested string of glyphs), H / V (pen x / y),
// h / v (in advances), i (colour), p (ordering-table entry: the drawing order here) and t (font).
void MoveListView::_draw_string(const gd::PBytes& bytes, gd::Int at, const gd::Arr<Arg>& args) {
    bool escape = false;
    if (_english) {
        _draw_usa(bytes, at, args);
        return;
    }
    while (at < bytes.size()) {
        gd::Int c = bytes[at];
        at += 1;
        if (c == 0) break;
        if (c == ESCAPE && !escape) {
            escape = true;
            continue;
        }
        if (escape && c != ESCAPE) {
            escape = false;
            _escape(c, args, false);
            continue;
        }
        escape = false;
        if (c >= 0xD1 && c <= 0xE0) {
            _button(c - 0xD1);
        } else if (c >= 0xBD && c <= 0xCC) {
            _arrow(c - 0xBD);
        } else if (c == 6) {
            _arrow(0x10);
        } else if (c == 0xFD) {
            _x += _advance_x();
        } else if (c == 0xFE) {
            _x += Fx::div_trunc(_advance_x(), 2);
        } else if (c == 1) {
            _x -= Fx::div_trunc(_advance_x(), 2);
            _glyph(0);
        } else if (c == 2) {
            _glyph(1);
            _x -= Fx::div_trunc(_advance_x(), 2);
        } else if (c == 3) {
            _x -= Fx::div_trunc(_advance_x(), 4);
            _glyph(2);
            _x -= Fx::div_trunc(_advance_x(), 4);
        } else if (c == 4 || c == 5) {
            _x += 2 - Fx::div_trunc(_advance_x(), 4);
            _glyph(c - 1);
            _x -= 2 + Fx::div_trunc(_advance_x(), 4);
        } else {
            _glyph(c - 1);
        }
    }
}

// An escape (after 0xFF): n (new line), s (a nested string: glyph byte - 1, in the USA engine
// ASCII), H / V (pen x / y), h / v (in advances), i (colour), p (ordering-table entry: the drawing
// order here) and t (font).
void MoveListView::_escape(gd::Int c, const gd::Arr<Arg>& args, bool usa) {
    // match char(c): an if-chain over the one-character strings.
    std::string ch(1, static_cast<char>(c));
    if (ch == "n") {
        _line_y += _advance_y();
        _x = _margin_x;
        _y = _line_y;
    } else if (ch == "s") {
        gd::PBytes nested = std::get<gd::PBytes>(args.pop_front());
        for (gd::Int b : nested) {
            if (b == 0) break;
            if (usa) {
                _usa_letter(b);
            } else {
                _glyph(b - 1);
            }
        }
    } else if (ch == "H" || ch == "h") {
        gd::Int v = std::get<gd::Int>(args.pop_front());
        if (c == 0x68) v *= _advance_x();
        _x = v;
        _margin_x = v;
    } else if (ch == "V" || ch == "v") {
        gd::Int v = std::get<gd::Int>(args.pop_front());
        if (c == 0x76) v *= _advance_y();
        _y = v;
        _line_y = v;
    } else if (ch == "i") {
        _colour = modulation(psx(std::get<gd::Int>(args.pop_front())));
    } else if (ch == "p") {
        args.pop_front();
    } else if (ch == "t") {
        _font = std::get<gd::Int>(args.pop_front());
    } else {
        Log::warning("move text: unknown escape 0x" + _format("%llX", c));
    }
}

// USA MoveTextDraw (0x80077724 of SLUS_004.02): as the Japanese engine with the same escapes,
// but ASCII from 0x21, pre-rendered words (0x01-0x1F, 0xA1-0xC3), arrows 0x80-0x90, button
// diagrams 0x91-0xA0, spaces 0x20 / 0xFE (one cell), 0xFD (two), 0xFB (ten) and 0xFC a line break.
void MoveListView::_draw_usa(const gd::PBytes& bytes, gd::Int at, const gd::Arr<Arg>& args) {
    bool escape = false;
    while (at < bytes.size()) {
        gd::Int c = bytes[at];
        at += 1;
        if (c == 0) break;
        if (c == ESCAPE && !escape) {
            escape = true;
            continue;
        }
        if (escape && c != ESCAPE) {
            escape = false;
            _escape(c, args, true);
            continue;
        }
        escape = false;
        gd::Int adv = _advance_x();
        if (c >= USA_BUTTONS && c < USA_BUTTONS + 16) {
            _button(c - USA_BUTTONS);
        } else if (c >= USA_ARROWS && c <= USA_ARROWS + 0x10) {
            _arrow(c - USA_ARROWS);
        } else if (c == USA_LINE_BREAK) {
            _escape(0x6E, args, true);
        } else if (c == 0x20 || c == 0xFE) {
            _x += adv;
        } else if (c == 0xFD) {
            _x += 2 * adv;
        } else if (c == USA_WIDE_SPACE) {
            _x += 10 * adv;
        } else if (c < 0x20) {
            gd::Int w = adv * JsonFile::number(_usa["word_widths"][c]);
            _usa_cell(0, ((c + 5) >> 2) * 12, (c + 1) & 3, w, w);
        } else if (c >= 0xA1) {
            gd::Int w = adv * JsonFile::number(_usa["word_widths_high"][c]);
            if (c <= 0xA3) {
                _usa_cell(0, ((c - 0x7C) >> 2) * 12, (c - 0x80) & 3, w, w);
            } else {
                _usa_cell(0x3C, ((c - 0x9C) >> 2) * 12, (c - 0xA4) & 3, w, w);
            }
        } else {
            _usa_letter(c);
        }
    }
}

// An ASCII letter of the USA atlas: u = 6 * (c & 15), v = 12 * (c >> 6), bit plane (c >> 4) & 3.
void MoveListView::_usa_letter(gd::Int c) {
    gd::Int g = c - 0x20;
    gd::Int width = _usa_font(2);
    gd::Int advance = _advance_x();
    _usa_cell((g & 15) * 6, (g >> 6) * 12, (g & 0x30) >> 4, width, advance);
}

// `width` texels of the USA atlas from (u, v) in bit plane `plane` at the pen, which moves on by
// `advance`.
void MoveListView::_usa_cell(gd::Int u, gd::Int v, gd::Int plane, gd::Int width, gd::Int advance) {
    gd::Int h = _usa_font(3);
    if (_usa_atlas != nullptr && width > 0) {
        gd::Int rows = JsonFile::number(_usa["atlas_rows"]);
        target->draw_texture_rect_region(_usa_atlas, r(_x, _y, width, h), gfx::Rect2(u, plane * rows + v, width, h), _colour);
    }
    _x += advance;
}

// Glyph `g` of the font's atlas (12 x 11 cells, ten a row, the bit plane (g / 10) & 3); the
// pen moves on by the advance.
void MoveListView::_glyph(gd::Int g) {
    gfx::TextureRef atlas = _atlas(_font);
    if (atlas != nullptr) {
        gd::Int u = g % 10 * 12;
        gd::Int v = (g / 40 * 11 + ATLAS_V) & 0xFF;
        gd::Int plane = (g / 10) & 3;
        gd::Int w = ram->u16(FONTS + 10 * _font + 4);
        gd::Int h = ram->u16(FONTS + 10 * _font + 6);
        target->draw_texture_rect_region(atlas, r(_x, _y, w, h), gfx::Rect2(u, plane * ATLAS_ROWS + v - ATLAS_V, w, h),
                                         _colour);
    }
    _x += _advance_x();
}

// FUN_800778A8: a 20 x 32 direction arrow (index & 8: held; 0x10 the neutral star), one of three
// pictures flipped, from 12 pixels above the pen.
void MoveListView::_arrow(gd::Int index) {
    gd::Int t = index == 0x10 ? 8 : index & 7;
    gd::Int uv = ram->u16(ARROWS + 4 * t);
    gd::Int flags = ram->u16(ARROWS + 4 * t + 2);
    gd::Int clut = (index & 8) != 0 ? ARROW_HELD_CLUT : ARROW_CLUT;
    gfx::TextureRef tex = vram->sprite(uv & 0xFF, uv >> 8, 0x13, 0x1F, clut, ICON_TPAGE);
    if (tex != nullptr) {
        gfx::Rect2 dest = r(_x, _y - 12, 0x13, 0x1F);
        if ((flags & 1) != 0) dest.size.x = -dest.size.x;
        if ((flags & 2) != 0) dest.size.y = -dest.size.y;
        target->draw_texture_rect(tex, dest, false, _colour);
    }
    _x += 0x14;
}

// A button diagram for `buttons` (1 LP, 2 RP, 4 LK, 8 RK): the four buttons' outline, and over
// it the pressed ones through the CLUT of the mask.
void MoveListView::_button(gd::Int buttons) {
    gd::Int clut = ((0x1FC + ((buttons & 8) != 0 ? 1 : 0)) << 6) | 0x30 | (buttons & 7);
    sprt(_x, _y - 12, 0x14, 0x20, (BUTTON_BACK_CLUT << 16) | 0xE014, ICON_TPAGE, _colour);
    sprt(_x, _y - 12, 0x14, 0x20, (clut << 16) | 0xE000, ICON_TPAGE, _colour);
    _x += 0x14;
}

// The glyph atlas of font 0 or 1: the side's fighter's costume.
gfx::TextureRef MoveListView::_atlas(gd::Int font) {
    gd::Int slot = flow->fight->fighters[gd::clampi(font, 0, 1)]->costume_slot;
    if (!_atlases.has(slot)) {
        std::string path = gd::path_join(root, "characters/costume_" + _format("%02lld", slot) + "/move_glyphs.png");
        // load(path) as Texture2D if ResourceLoader.exists(path) else null
        _atlases[slot] = gd::file_exists(path) ? TexturePacks::texture(path) : nullptr;
    }
    return _atlases.at(slot);
}

}  // namespace t3
