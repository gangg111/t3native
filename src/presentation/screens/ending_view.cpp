// remake/presentation/screens/ending_view.gd
#include "presentation/screens/ending_view.h"

#include <initializer_list>

#include "content/flow_data.h"
#include "content/game_ram.h"
#include "content/json_file.h"
#include "content/text_locale.h"
#include "content/vram_image.h"
#include "core/fight/fight_state.h"
#include "core/game/ending_screen.h"
#include "core/game/game_flow.h"
#include "core/game/theater_screen.h"
#include "gd/io.h"

namespace t3 {

namespace {

// A {key: value, ...} literal of ints, given as key, value, key, value ...
gd::Dict<gd::Int, gd::Int> _int_dict(std::initializer_list<gd::Int> pairs) {
    gd::Dict<gd::Int, gd::Int> d;
    for (auto it = pairs.begin(); it != pairs.end(); it += 2) d.set(*it, *(it + 1));
    return d;
}

// "%2d" % v: right-aligned in two characters.
std::string _width_2(gd::Int v) {
    std::string s = gd::str(v);
    return s.length() < 2 ? std::string(2 - s.length(), ' ') + s : s;
}

}  // namespace

void EndingView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                       const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    _roll = image->duplicate_image();
    _roll->upload_file(gd::path_join(screens_dir, "staff.tims"));
    _theater = image->duplicate_image();
    _theater->upload_file(gd::path_join(screens_dir, "theater.tims"));
    add_pass(gfx::Blend::ADD, [this] { _staff_roll(); });
    add_pass(gfx::Blend::MIX, [this] { _theater_page(); });
}

void EndingView::show_state(GameFlow* game) {
    flow = game;
    frame_count = game->fight->vblank;
    vram = game->theater->kind != 0 ? _theater : _roll;
    _idle = game->held(0) != 0 || game->held(1) != 0 ? 0 : _idle + 1;
    redraw();
}

void EndingView::_draw() {
    begin();
    black();
}

// ---- the staff roll ---------------------------------------------------------------------------

void EndingView::_staff_roll() {
    if (flow == nullptr || flow->theater->kind != 0 || flow->sub != 3) return;
    gd::Ref<StaffRoll> roll = flow->ending->roll;
    for (const gd::Ref<StaffRoll::Line>& l : roll->lines) {
        if (l->glow) _glow(l);
        _roll_text(l);
    }
    if (roll->logo_bright >= 0) {
        float k = static_cast<float>(static_cast<double>(roll->logo_bright) / 128.0);
        gfx::Color tint(k, k, k);
        ft4(0x36, 0xDC, 0x100, 0x2D, 0, 0x80, 0xFF, 0x2C, LOGO_CLUT, 0xA, tint);
        ft4(0x136, 0xDC, 5, 0x2D, 0, 0x80, 4, 0x2C, LOGO_CLUT, 0xB, tint);
    }
}

// FUN_8011484C: a credit line as glyphs 12 pixels high, modulated by the line's brightness.
void EndingView::_roll_text(const gd::Ref<StaffRoll::Line>& l) {
    gd::Arr<gd::Int> f = ROLL_FONTS.at(l->wide);
    gd::Int cols = f[0];
    gd::Int v0 = f[1];
    gd::Int cell = f[2];
    gd::Int space = f[3];
    gd::PI32 widths = l->wide ? flow->data->staff_font_wide : flow->data->staff_font_narrow;
    float k = static_cast<float>(static_cast<double>(l->bright) / 128.0);
    gfx::Color tint(k, k, k);
    gd::Int x = l->x;
    gd::Int n_chars = static_cast<gd::Int>(l->text.length());
    for (gd::Int i = 0; i < n_chars; ++i) {
        gd::Int c = static_cast<unsigned char>(l->text[static_cast<size_t>(i)]);   // unicode_at (ASCII)
        if (c == 0x20) {
            x += space;
            continue;
        }
        gd::Int n = c < 0x31 ? c - 0x21 : c - 0x22;
        if (n < 0 || n >= widths.size()) continue;
        gd::Int w = widths[n];
        ft4(x, l->y, w, 0xC, (gd::mod(n, cols) * cell) & 0xFF, (gd::div(n, cols) * 0xC + v0) & 0xFF, w, 0xC, ROLL_CLUT,
            ROLL_TPAGE, tint);
        x += w;
    }
}

// FUN_80113A3C: the glow behind a line (25 Gouraud quads, additive).
void EndingView::_glow(const gd::Ref<StaffRoll::Line>& l) {
    gd::Int r = gd::Int(l->rgb.x) & 0xFF;
    gd::Int g = gd::Int(l->rgb.y) & 0xFF;
    gd::Int b = gd::Int(l->rgb.z) & 0xFF;
    gfx::Color full = gfx::Color::c8(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
    gfx::Color half = gfx::Color::c8(static_cast<int>(r >> 1), static_cast<int>(g >> 1), static_cast<int>(b >> 1));
    gfx::Color none = gfx::Color::black();
    gd::Int left = StaffRoll::CENTRE - l->width / 2;
    gd::Int y = l->y;
    gd::Int x0 = l->x;
    gd::Int x1 = l->x + l->width;
    gd::Int a1 = left - 0x10;
    gd::Int a17 = left + 0x18;
    gd::Int a2 = left + 0x30;
    gd::Int a13 = x0 + 0x10;
    gd::Int a3 = x0 + 0x10 + l->width - 0x28;
    gd::Int a4 = x1;
    gd::Int a10 = x1 - 0x18;
    gd::Int a5 = x1 + 0x28;
    gd::Int a11 = left - 0x20;
    gd::Int a12 = left + 0x24;
    gd::Int a6 = x1 - 0xC;
    gd::Int a7 = x1 + 0x38;
    gd::Int b22 = left + 0x16;
    gd::Int c2 = x1 + 2;
    // [x left, x right, y top, y bottom, colours top-left, top-right, bottom-left, bottom-right]
    // (an untyped record Array: a small struct).
    struct GlowQuad {
        gd::Int xl, xr, yt, yb;
        gfx::Color c0, c1, c2, c3;
    };
    gd::Arr<GlowQuad> quads{
        GlowQuad{a1, a17, -2, 1, none, half, none, half}, GlowQuad{a17, a2, -2, 1, half, half, half, full},
        GlowQuad{a13, a3, -2, 1, half, half, full, full}, GlowQuad{a4, a10, -2, 1, half, half, half, full},
        GlowQuad{a5, a4, -2, 1, none, half, none, half}, GlowQuad{a11, a12, 1, 4, none, full, none, full},
        GlowQuad{a12, a2, 1, 4, full, full, full, full}, GlowQuad{a13, a3, 1, 4, full, full, full, full},
        GlowQuad{a10, a6, 1, 4, full, full, full, full}, GlowQuad{a6, a7, 1, 4, full, none, full, none},
        GlowQuad{a11, b22, 4, 7, none, half, none, full}, GlowQuad{b22, a2, 4, 7, half, full, full, full},
        GlowQuad{a13, a3, 4, 7, full, full, full, full}, GlowQuad{c2, a10, 4, 7, half, full, full, full},
        GlowQuad{a7, c2, 4, 7, none, half, none, full}, GlowQuad{a11, a12, 7, 10, none, full, none, full},
        GlowQuad{a12, a2, 7, 10, full, full, full, full}, GlowQuad{a13, a3, 7, 10, full, full, full, full},
        GlowQuad{a10, a6, 7, 10, full, full, full, full}, GlowQuad{a6, a7, 7, 10, full, none, full, none},
        GlowQuad{a17, a1, 10, 13, half, none, half, none}, GlowQuad{a2, a17, 10, 13, full, half, half, half},
        GlowQuad{a13, a3, 10, 13, full, full, half, half}, GlowQuad{a10, a4, 10, 13, full, half, half, half},
        GlowQuad{a4, a5, 10, 13, half, none, half, none}};
    for (const GlowQuad& q : quads) {
        gd::Int xl = q.xl;
        gd::Int xr = q.xr;
        gd::Int yt = y + q.yt;
        gd::Int yb = y + q.yb;
        quad(gfx::Vec2(xl, yt), gfx::Vec2(xr, yt), gfx::Vec2(xl, yb), gfx::Vec2(xr, yb), q.c0, q.c1, q.c2, q.c3);
    }
}

// ---- THEATER MODE -----------------------------------------------------------------------------

void EndingView::_theater_page() {
    if (flow == nullptr || flow->theater->kind == 0 || flow->sub != 1) return;
    gd::Ref<TheaterScreen> t = flow->theater;
    _panel(0);
    _panel(1);
    if (t->kind == TheaterScreen::PAGE_MOVIES) {
        _movie_grid(t);
    } else {
        _sound_list(t);
    }
    _banner(t->kind);
}

void EndingView::_frame4(gd::Int x, gd::Int y, gd::Int w, gd::Int h, gd::Int side, gd::Int top, gd::Int colour) {
    gfx::Color c = psx(colour);
    fill(x, y, w, top, c);
    fill(x, y, side, h, c);
    fill(x, y + h - top, w, top, c);
    fill(x + w - side, y, side, h, c);
}

void EndingView::_panel_frame(gd::Int k, gd::Int dx, gd::Int dy) {
    gd::Int r = PANELS + 0x20 * k;
    _frame4(ram->s16(r) + dx, ram->s16(r + 2) + dy, ram->s16(r + 4), ram->s16(r + 6), ram->u8(r + 0xA), ram->u8(r + 0xB),
            ram->u32(r + 0x1C));
}

// A panel: its Gouraud quad, semi-transparent with flag 2 (bug #36 not reproduced: the game
// ORs the flag into the first corner's red), then its frame.
void EndingView::_panel(gd::Int k) {
    gd::Int r = PANELS + 0x20 * k;
    gd::Int flags = ram->u16(r + 8);
    if ((flags & 1) != 0) {
        float alpha = (flags & 2) != 0 ? 0.5f : 1.0f;
        gd::Arr<gfx::Color> c;
        for (gd::Int i = 0; i < 4; ++i) {
            gfx::Color col = psx(ram->u32(r + 0xC + 4 * i));
            col.a = alpha;
            c.append(col);
        }
        gradient(ram->u16(r), ram->u16(r + 2), ram->u16(r + 4), ram->u16(r + 6), c[0], c[1], c[2], c[3]);
    }
    if (ram->u8(r + 0xA) != 0) _panel_frame(k, 0, 0);
}

void EndingView::_sprite(gd::Int x, gd::Int y, gd::Int rec, gd::Int u, gd::Int w, gd::Int h, gd::Int tpage) {
    gd::Int clut = ((ram->s16(rec + 2) << 6) | ((ram->s16(rec) >> 4) & 0x3F)) & 0xFFFF;
    sprt(x, y, w, h, (u & 0xFF) | ((ram->s16(rec + 6) & 0xFF) << 8) | (clut << 16), tpage);
}

// An 8-bit 48 x 64 movie picture (0: the locked picture).
void EndingView::_picture(gd::Int x, gd::Int y, gd::Int k) {
    gd::Int r = PICTURES + 12 * k;
    gd::Int px = ram->s16(r + 4);
    gd::Int py = ram->s16(r + 6);
    _sprite(x, y, r, (px & 0x7F) << 1, 0x30, 0x40, ((py & 0x100) >> 4) | ((px & 0x380) >> 6) | 0x80);
}

void EndingView::_cell_frame(gd::Int x, gd::Int y) {
    gd::Int px = ram->s16(CELL_FRAME + 4);
    gd::Int py = ram->s16(CELL_FRAME + 6);
    _sprite(x, y, CELL_FRAME, 0x60, 0x30, 0x40, ((py & 0x100) >> 4) | ((px & 0x3C0) >> 6));
}

// FUN_8010FB24: TIM k (the disc logos 0-5, the sound picture 7) as one sprite.
void EndingView::_tim_sprite(gd::Int x, gd::Int y, gd::Int k) {
    gd::Int r = TIMS + 14 * k;
    gd::Int px = ram->s16(r + 4);
    gd::Int py = ram->s16(r + 6);
    gd::Int mode = ram->u16(r + 0xC) & 1;
    _sprite(x, y, r, gd::shl(px & 0x3F, mode != 0 ? 1 : 2), ram->u16(r + 8), ram->u16(r + 0xA),
            (mode << 7) | ((py & 0x100) >> 4) | ((px & 0x380) >> 6));
}

// Help strip k: pieces of a text picture in a row at y 400, centred; in English the USA
// release's line k (ending.ovl FUN_80100834: font 0, colour 6, centred at 9 pixels a letter).
void EndingView::_strip(gd::Int k) {
    std::string line = ram->locale != nullptr ? ram->locale->theater_line(k) : "";
    if (!line.empty()) {
        text(line, 0, 6, (0x170 - 9 * static_cast<gd::Int>(line.length())) >> 1, 400);
        return;
    }
    gd::Int x = (0x170 - 12 * ram->s16(STRIPS + 4 * k)) >> 1;
    gd::Int r = STRIP_PIECES + 12 * ram->s16(STRIPS + 4 * k + 2);
    while (true) {
        gd::Int clut = (((ram->s16(r + 8) * 16 + 0x200) >> 4) & 0x3F) | 0x7F80;
        gd::Int w = ram->u16(r + 4);
        sprt(x, 400, w, ram->u16(r + 6), ram->u8(r) | (ram->u8(r + 2) << 8) | (clut << 16), 0x16);
        x += ram->s16(r + 4);
        gd::Int more = ram->s16(r + 0xA);
        r += 12;
        if (more == 0) break;
    }
}

void EndingView::_help_strip(gd::Int focus, gd::Int page) {
    gd::Dict<gd::Int, gd::Int> table = page == TheaterScreen::PAGE_MOVIES ? _int_dict({1, 7, 2, 5, 3, 0})
                                                                          : _int_dict({1, 7, 2, 4, 3, 1, 4, 6});
    if (table.has(focus)) _strip(table.at(focus));
}

gd::Int EndingView::_pulse() {
    return scale_colour(0xFF4020, (triangle(frame_count << 4) >> 1) + 0x80);
}

// FUN_801106A8: a label in a white frame over a dark blue box, or a pulsing one when selected
// (bug #37 not reproduced: the game starts the box 2 pixels above the frame).
void EndingView::_button(gd::Int focus, gd::Int which, gd::Int x, gd::Int y, const std::string& label) {
    bool selected = focus == which;
    gd::Int x0 = x - 8;
    gd::Int y0 = y - 6;
    gd::Int w = 16 + 9 * static_cast<gd::Int>(label.length());
    gd::Int h = 12 + 16;
    fill(x0, y0, w, h, psx(selected ? _pulse() : 0x800000));
    _frame4(x0, y0, w, h, 2, 2, 0xFFFFFF);
    text(label, 0, selected ? 5 : 10, x, y);
}

// FUN_80110A58: 6 x 4 pictures, the buttons and the selected movie's names, drawn in the
// reverse of their linking: the names first, the cursor's frame last.
void EndingView::_movie_grid(const gd::Ref<TheaterScreen>& t) {
    gd::Arr<gd::Json> movies = flow->data->theater_movies;
    gd::Int count = movies.size();
    const gd::Json& texts = flow->data->theater_texts;
    _movie_names(t);
    if (flow->ending->all_movies != 0) {
        _button(t->focus, TheaterScreen::Focus::EXIT, 0xFC, 0x158, texts["exit"].str());
        _button(t->focus, TheaterScreen::Focus::SWITCH, 0xA2, 0x158, texts["sound"].str());
    } else {
        _button(t->focus, TheaterScreen::Focus::EXIT, 0xA2, 0x158, texts["exit"].str());
    }
    for (gd::Int row = 3; row > -1; --row) {
        gd::Int y = 0x48 + 0x40 * row;
        for (gd::Int col = 5; col > -1; --col) {
            gd::Int i = col + (row + t->top) * 6;
            if (i >= count) continue;
            const gd::Json& e = movies[i];
            gd::Int x = 0x28 + 0x30 * col;
            if (t->entry_status(i) >= 0) {
                if (JsonFile::number(e["picture"]) >= 0) _picture(x, y, JsonFile::number(e["picture"]));
                _cell_frame(x, y);
            } else if (JsonFile::number(e["movie"]) >= 0) {
                _picture(x, y, 0);
                _cell_frame(x, y);
            }
        }
    }
    if (t->focus == TheaterScreen::Focus::LIST && (flow->fight->vblank & 0x1C) != 0)
        _panel_frame(2, t->cursor % 6 * 0x30 + 0x26, (t->cursor / 6 - t->top) * 0x40 + 0x45);
}

// The selected movie's picture, name and title lines (or ??? while locked), or the focused
// button's help strip.
void EndingView::_movie_names(const gd::Ref<TheaterScreen>& t) {
    gd::Arr<gd::Json> movies = flow->data->theater_movies;
    if (t->focus != TheaterScreen::Focus::LIST || t->cursor >= movies.size()) {
        _help_strip(t->focus, t->kind);
        return;
    }
    const gd::Json& e = movies[t->cursor];
    gd::Int picture = JsonFile::number(e["picture"]);
    if (t->entry_status(t->cursor) < 0) {
        if (picture > 0) text(flow->data->theater_texts["locked"].str(), 1, 6, 0xC1, 0x182);
        return;
    }
    const gd::Json& lines = e["lines"];
    gd::PI32 ys = lines.size() == 1 ? gd::PI32{0x1A6} : gd::PI32{0x19D, 0x1AF};
    gd::Int n = gd::mini(lines.size(), 2);
    for (gd::Int k = 0; k < n; ++k) {
        std::string s = lines[k].str();
        text(s, 0, 5, ((0xE6 - 9 * static_cast<gd::Int>(s.length())) >> 1) + 0x64, ys[k]);
    }
    std::string name = t->movie_name(t->cursor);
    text(name, 1, 6, ((0xE9 - 13 * static_cast<gd::Int>(name.length())) >> 1) + 0x60, 0x182);
    if (picture > 0) _picture(0x1E, 0x180, picture);
}

// FUN_801112DC: 14 lines of the track list, the buttons, the selected track and the disc logos,
// drawn in the reverse of their linking: the logos first, the list's frame last.
void EndingView::_sound_list(const gd::Ref<TheaterScreen>& t) {
    gd::Arr<gd::Json> tracks = flow->data->theater_tracks;
    const gd::Json& texts = flow->data->theater_texts;
    _tim_sprite(0xF2, 0x15A, t->disc * 2 - 1);
    _tim_sprite(0xF2, 0x5A, t->disc * 2 - 2);
    if (t->focus == TheaterScreen::Focus::LIST && t->cursor < tracks.size()) {
        std::string name = tracks[t->cursor]["name"].str();
        gd::Int x = (0x170 - 13 * static_cast<gd::Int>(name.length())) >> 1;
        text(texts[t->kind == TheaterScreen::PAGE_ARRANGE ? "arrange" : "arcade"].str(), 0, 5, 0x104, 0x1A6);
        _tim_sprite(x - 0xE, 0x182, 7);
        text(name, 1, 6, x + 0x10, 0x186);
    } else {
        _help_strip(t->focus, t->kind);
    }
    _button(t->focus, TheaterScreen::Focus::EXIT, 0xFA, 0x122, texts["exit"].str());
    _button(t->focus, TheaterScreen::Focus::BGM, 0xFA, 0xE6, texts["bgm"].str());
    if (flow->movies_available) _button(t->focus, TheaterScreen::Focus::SWITCH, 0xFA, 0xC8, texts["theater"].str());
    for (gd::Int i = TheaterScreen::LINES_SHOWN - 1; i > -1; --i) {
        gd::Int idx = t->top + i;
        if (idx >= tracks.size()) continue;
        bool hit = t->focus == TheaterScreen::Focus::LIST && idx == t->cursor;
        if (hit) fill(0x19, 0x4F + 0x14 * i, 0xCC, 0x12, psx(_pulse()));
        gd::Int colour = hit ? 5 : (t->kind == TheaterScreen::PAGE_ARRANGE ? 0xB : 0xA);
        text(_width_2(idx + 1) + ":" + tracks[idx]["name"].str(), 0, colour, 0x1E, 0x50 + 0x14 * i);   // "%2d:%s"
    }
    _frame4(0x18, 0x48, 0xCE, 0x122, 2, 2, 0xFFFFFF);
}

// FUN_80112554: the page title; after 480 idle frames it alternates every 256 frames with
// PUSH START TO PLAY, sliding for the last 32 frames of each half.
void EndingView::_banner(gd::Int page) {
    gd::Int t = _idle >= IDLE_FRAMES ? _idle - IDLE_FRAMES : 0;
    gd::Int x = (t & 0x100) != 0 ? 0x228 : 0xB8;
    if ((t & 0xE0) == 0xE0) {
        gd::Int d = ((t & 0x1F) * 23) >> 1;
        x += (t & 0x100) != 0 ? -d : d;
    }
    std::string title = ram->string(ram->u32(BANNERS + 4 * (page - 1) + 12 * flow->theater->disc));
    print_fmt("%c%f%H%V" + title,
              {gd::Int(6), gd::Int(1), x - ((static_cast<gd::Int>(title.length()) * 13) >> 1), gd::Int(0x22),
               gd::Int(page == 2 ? 1 : 6)});
    std::string hint = ram->string(ram->u32(BANNERS + 8));
    print_fmt("%c%f%H%V" + hint,
              {gd::Int(6), gd::Int(0), x - ((static_cast<gd::Int>(hint.length()) * 9) >> 1) - 0x170, gd::Int(0x22), gd::Int(5),
               gd::Int(6), gd::Int(5)});
}

}  // namespace t3
