// remake/presentation/screens/ranking_view.gd
#include "presentation/screens/ranking_view.h"

#include "content/fight_tables.h"
#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "core/math/fx.h"
#include "gd/io.h"
#include "gd/json.h"

namespace t3 {

void RankingView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                        const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    for (gd::Int i = 0; i < 3; ++i) {
        gd::Ref<VramImage> v = image->duplicate_image();
        v->upload_file(gd::path_join(screens_dir, "ranking_" + gd::str(i) + ".tims"));
        _pages.append(v);
    }
    // The rows' lines, faces and bars are clipped below the header; their texts, in the front
    // slot, are not (the draw area is reset before the text slot is drawn).
    add_pass(gfx::Blend::MIX, [this] { _rows(false); }, ROWS_CLIP());
    add_pass(gfx::Blend::SUB, [this] { _strip(0x7FD0); });
    add_pass(gfx::Blend::ADD, [this] { _strip(0x7F50); });
    add_pass(gfx::Blend::MIX, [this] { _front(); });
}

void RankingView::show_state(const gd::Ref<GameFlow>& game) {
    flow = game;
    frame_count = game->fight->vblank;
    vram = _pages[gd::clampi(ram->u8(PAGE), 0, 2)];
    fade_level = game->ranking->fade >= 0 ? game->ranking->fade : NO_FADE;
    redraw();
}

gd::Int RankingView::_mode() { return ram->u16(T); }

void RankingView::_draw() {
    begin();
    if (flow == nullptr) return;
    gd::Int mode = _mode();
    if (mode == RankingScreen::TableMode::HIDDEN) {
        black();
    } else if (mode == RankingScreen::TableMode::NAME_ENTRY) {
        // The name's row on a blue band (blend mode 0).
        fill(0, ram->u16(T + RankingScreen::T_NAME_SCREEN_ROW) * 0x48 + 0x68, 0x170, 0x48, gfx::Color(0, 0, 0x10 / 255.0f, 0.5f));
    }
}

void RankingView::_header() {
    picture(0, 0x24, 0x80, 0x30, 0x180, 0, 0x7F50, 0x80);
    picture(0x80, 0x24, 0x70, 0x30, 0x180, 0x30, 0x7F50, 0x80);
}

// The header's shaded strip (POLY_GT4, added with CLUT 0x7F50 and subtracted with 0x7FD0).
void RankingView::_strip(gd::Int clut) {
    if (flow == nullptr || _mode() == RankingScreen::TableMode::HIDDEN) return;
    gd::Int tp = clut == 0x7F50 ? 0xA6 : 0xC6;
    gfx::Color a = psx(0x808080);
    gfx::Color b = psx(0x606060);
    gfx::Color c = psx(0x101010);
    textured(vram->sprite(0x70, 0x30, 0x10, 0x30, clut, tp), 0xF0, 0x24, 0x10, 0x30, a, b, a, b);
    textured(vram->sprite(0, 0x60, 0x70, 0x30, clut, tp), 0x100, 0x24, 0x70, 0x30, b, c, b, c);
}

void RankingView::_rows(bool texts) {
    if (flow == nullptr) return;
    gd::Int mode = _mode();
    if (mode < RankingScreen::TableMode::SLIDING || mode > RankingScreen::TableMode::NAME_ENTERED) return;
    gd::Int page = ram->u8(PAGE);
    bool entry = mode != RankingScreen::TableMode::SLIDING;
    bool blink = (frame_count & 3) != 0;
    gd::Int y0 = ram->s16(T + RankingScreen::T_Y);
    switch (page) {
        case 0:
        case 1: {
            gd::Int rows = ram->u16(T + RankingScreen::T_ROWS);
            for (gd::Int i = 0; i < rows; ++i) {
                gd::Int c = ram->u16(T + RankingScreen::T_CHARS + 2 * i);
                gd::Int style = entry && ram->u16(T + RankingScreen::T_NAME_ROW) == i && blink ? 1 : 0;
                if (page == 0) {
                    _time_row(i, c, RECORDS + 8 * c, y0 + 0x48 * i, style, texts);
                } else {
                    _survivor_row(i, SURVIVORS + 8 * c, y0 + 0x48 * i, style, texts);
                }
            }
            break;
        }
        case 2: {
            gd::Int rows = ram->u16(T + RankingScreen::T_ROWS);
            for (gd::Int i = 0; i < rows; ++i) {
                gd::Int c = ram->u16(T + RankingScreen::T_CHARS + 2 * i);
                gd::Int anim = i < ram->s32(T + RankingScreen::T_SHOWN) ? 0 : ram->u16(T + RankingScreen::T_ALPHA);
                _usage_row(i, c, ram->u16(T + RankingScreen::T_USAGE + 2 * c), ram->u16(T + RankingScreen::T_TOP_SHARE), anim,
                           y0 + 0x48 * i, texts);
            }
            break;
        }
        default:
            break;
    }
}

void RankingView::_front() {
    if (flow == nullptr || _mode() == RankingScreen::TableMode::HIDDEN) return;
    _header();
    _rows(true);
    if (_mode() >= RankingScreen::TableMode::NAME_ENTRY) {
        gd::Int pos = ram->u16(NAME + RankingScreen::N_POS);
        if (pos < 3 && ram->u8(PAGE) < 2)
            fill(pos * 0xD + 0x129, ram->u16(T + RankingScreen::T_NAME_SCREEN_ROW) * 0x48 + 0xA0, 0x12, 4, psx(0xFF));
        gd::Int left = ram->u16(T + RankingScreen::T_HOLD);
        ptext(STR_2D, {1, 6, 1, 0x13C, 0x30, left == 0 ? 0 : (left + 0x3B) / 0x3C});
    }
}

std::string RankingView::_name_of(gd::Int character) { return flow->fight->tables->character(character * 4)["name"].str(); }

std::string RankingView::_rank(gd::Int rank, gd::Int colour, gd::Int y) {
    gd::Int suffix = rank <= 3 ? gd::mini(rank % 10, 3) : 3;
    gd::Int n = rank + 1 > 100 ? 99 : rank + 1;
    std::string sfx = ram->string(ram->u32(ORDINALS + 4 * suffix));
    ptext(STR_RANK, {1, colour, 1, 0xC, y - 0x22, n, sfx});
    return sfx;
}

void RankingView::_row_lines(gd::Int y) {
    fill(8, y - 0xE, 0x3E, 8, psx(0x909090));
    fill(0x46, y - 8, 0x132 - 8, 2, psx(0x909090));
}

// FUN_800C133C: 1ST  <face> NAME  mm'ss"hh  entered name.
void RankingView::_time_row(gd::Int rank, gd::Int character, gd::Int rec, gd::Int y, gd::Int style, bool texts) {
    if ((y & 0xFFFFFFFFLL) >= 0x228) return;
    gd::Int s = STYLE_TIME + 5 * style;
    if (!texts) {
        _row_lines(y);
        portrait(0x4C, y - 0x3A, character << 2, 8);
        return;
    }
    _rank(rank, ram->u8(s), y);
    ptext(STR_S, {1, ram->u8(s + 1), 0, 0x6F, y - 0x38, _name_of(character)});
    gd::Int c = ram->u8(s + 2);
    gd::Int t = gd::mini(ram->s32(rec), TIME_CAP);
    ptext(STR_TICKS, {1, c, 1, y - 0x26, 0xB6, 0xD9});
    ptext(STR_TIME, {1, c, 1, y - 0x22, 0x9E, t / 60 / 60, 0xBF, (t / 60) % 60, 0xE3, (t % 60) * 100 / 60});
    ptext(STR_S, {1, ram->u8(s + 3), 1, 299, y - 0x22, ram->string(rec + 4).substr(0, 4)});
}

// FUN_800C16C0: 1ST  <face> NAME  n WIN(S)  entered name.
void RankingView::_survivor_row(gd::Int rank, gd::Int e, gd::Int y, gd::Int style, bool texts) {
    if ((y & 0xFFFFFFFFLL) >= 0x228) return;
    gd::Int wins = gd::mini(ram->u16(e + 2), 999);
    gd::Int s = STYLE_SURV + 4 * style;
    gd::Int character = ram->u16(e);
    if (!texts) {
        _row_lines(y);
        portrait(0x4C, y - 0x3A, character << 2, 8);
        return;
    }
    _rank(rank, ram->u8(s), y);
    ptext(STR_S, {1, ram->u8(s + 1), 0, 0x6F, y - 0x38, _name_of(character)});
    gd::Int c = ram->u8(s + 2);
    if (wins < 2) {
        ptext(STR_3D, {1, c, 1, 0xAB, y - 0x22, wins});
        ptext(STR_WIN, {1, c, 1, 0xD9, y - 0x22});
    } else {
        ptext(STR_3D, {1, c, 1, 0x9E, y - 0x22, wins});
        ptext(STR_WINS, {1, c, 1, 0xCC, y - 0x22});
    }
    ptext(STR_S, {1, ram->u8(s + 3), 1, 299, y - 0x22, ram->string(e + 4).substr(0, 4)});
}

// FUN_800C19C4: 1ST  <face> NAME  share %  and a bar relative to the most used character.
void RankingView::_usage_row(gd::Int rank, gd::Int character, gd::Int share, gd::Int top, gd::Int anim, gd::Int y, bool texts) {
    if ((y & 0xFFFFFFFFLL) > 0x227) return;
    gd::Int shown = 0;
    gd::Int bar = 0;
    if (top != 0) {
        shown = Fx::w32(share * anim) >> 6;
        bar = Fx::div_trunc(Fx::w32(Fx::w32(share * anim) * 1000), top) >> 6;
    }
    gd::Int s = STYLE_USE;
    if (!texts) {
        _row_lines(y);
        _usage_bar(character, bar, 0x111, y - 0x22);
        portrait(0x4C, y - 0x3A, character << 2, 8);
        return;
    }
    _rank(rank, ram->u8(s), y);
    ptext(STR_S, {1, ram->u8(s + 1), 0, 0x6F, y - 0x38, _name_of(character)});
    shown = gd::mini(shown, 1000);
    if (shown == 1000 || shown == 0) {
        ptext(shown == 1000 ? STR_100 : STR_0, {1, ram->u8(s + 2), 1, 0x11B, y - 0x22, 0, 0x145, y - 0x1E});
    } else {
        ptext(STR_PCT, {1, ram->u8(s + 2), 1, 0x119, y - 0x22, shown / 10, 0, 0x134, y - 0x1E, 0x13B, shown % 10});
    }
}

// FUN_800C1154: a usage bar, 129 x share / 1000 long, in the character's colours.
void RankingView::_usage_bar(gd::Int character, gd::Int share, gd::Int x, gd::Int y) {
    share = gd::mini(Fx::w32(share), 1000);
    gd::Int w = share != 0 ? Fx::s16(Fx::div_trunc(Fx::w32(share * 0x81), 1000)) : 0;
    if (w == 0) return;
    gd::Int e = BAR_COLOURS + 0x14 * character;
    gd::Int left = x - w;
    gfx::Color face = psx(ram->u32(e + 4));
    gfx::Color face2 = psx(ram->u32(e + 8));
    gfx::Color top = psx(ram->u32(e + 0xC));
    gfx::Color top2 = psx(ram->u32(e + 0x10));
    gfx::Color edge = psx(ram->u32(e));
    float fx = static_cast<float>(x), fy = static_cast<float>(y), fl = static_cast<float>(left);
    quad(gfx::Vec2(fl - 6, fy - 8), gfx::Vec2(fx - 8, fy - 8), gfx::Vec2(fl, fy), gfx::Vec2(fx, fy), top, top2, top, top2);
    quad(gfx::Vec2(fl - 6, fy - 8), gfx::Vec2(fl, fy), gfx::Vec2(fl - 6, fy + 0x10), gfx::Vec2(fl, fy + 0x18), edge, edge, edge, edge);
    gradient(left, y, w, 0x18, face, face2, face, face2);
}

}  // namespace t3
