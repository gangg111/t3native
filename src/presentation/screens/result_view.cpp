// remake/presentation/screens/result_view.gd
#include "presentation/screens/result_view.h"

#include <algorithm>
#include <cmath>

#include "content/flow_data.h"
#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_progress.h"
#include "core/math/fx.h"
#include "gd/io.h"

namespace t3 {

namespace {

// String.lpad(n): spaces in front up to n characters.
std::string _lpad(const std::string& s, gd::Int n) {
    gd::Int len = static_cast<gd::Int>(s.size());
    return len >= n ? s : std::string(static_cast<size_t>(n - len), ' ') + s;
}

// String.replace(what, forwhat).
std::string _replace(std::string s, const std::string& what, const std::string& forwhat) {
    if (what.empty()) return s;
    size_t at = 0;
    while ((at = s.find(what, at)) != std::string::npos) {
        s.replace(at, what.size(), forwhat);
        at += forwhat.size();
    }
    return s;
}

}  // namespace

void ResultView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                       const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    _system = image;
    _time_attack = image->duplicate_image();
    _time_attack->upload_file(gd::path_join(screens_dir, "result_ta.tims"));
    _force = image->duplicate_image();
    _force->upload_file(gd::path_join(screens_dir, "result_force.tims"));
    pictures_dir = screens_dir;
    reload_pictures();
    // Native: the wide mode: _draw, the subtracted band and the stripes are background.
    wide_background = true;
    add_background_pass(gfx::Blend::SUB, [this] { _subtracted(); });
    add_background_pass(gfx::Blend::MIX, [this] { _stripes(); });
    add_pass(gfx::Blend::MIX, [this] { _middle(); });
    add_pass(gfx::Blend::ADD, [this] { _added(); });
    add_pass(gfx::Blend::MIX, [this] { _front(); });
}

// The portraits, from the texture pack in use.
void ResultView::reload_pictures() { _portraits = character_pictures(pictures_dir, "vs_portraits"); }

void ResultView::show_state(const gd::Ref<GameFlow>& game) {
    flow = game;
    _screen = game->state;
    frame_count = game->fight->vblank;
    vram = _screen == GameFlow::State::TIME_RESULT ? _time_attack : _force_result() ? _force : _system;
    fade_level = ram->s32(S + ResultScreens::BRIGHT);
    redraw();
}

bool ResultView::_force_result() { return _screen == GameFlow::State::FORCE_RESULT; }

bool ResultView::_team() { return _screen == GameFlow::State::TEAM_RESULT; }

void ResultView::_draw() {
    begin();
    black();
    if (flow == nullptr) return;
    if (_screen == GameFlow::State::TIME_RESULT || _force_result()) {
        if (!_force_result()) fill(wide_left(), wide_top(), wide_right() - wide_left(), wide_bottom() - wide_top(), psx(TIME_ATTACK_CLEAR));
        // Untyped Array records [sx, sy, w, h, vx, vy]: gd::Arr<gd::Int>.
        for (const gd::Arr<gd::Int>& part : gd::Arr<gd::Arr<gd::Int>>{
                 {0, 0, 0x80, 0x100, 0x200, 0}, {0x80, 0, 0x80, 0x100, 0x240, 0},
                 {0x100, 0, 0x70, 0x100, 0x280, 0}, {0, 0x100, 0x80, 0xE0, 0x200, 0x100},
                 {0x80, 0x100, 0x80, 0xE0, 0x240, 0x100}, {0x100, 0x100, 0x70, 0xE0, 0x280, 0x100}})
            picture(part[0], part[1], part[2], part[3], part[4], part[5], 0x7FE0, 0x80);
    } else {
        backdrop(0, _team() ? TEAM_BACKDROP : SURVIVAL_BACKDROP);
        // The banner's text is in the background slot (%p 0): under the subtracted band.
        gd::Int y = ram->s32(ResultScreens::BANNER_BASE + 4) - 0x344;
        while (y < 0x170) {
            ptext(0x800B9854LL, {0, 6, 2, y, 0xCB, ram->string(BANNER)});
            y += 0x344;
        }
    }
}

gd::Int ResultView::_rows_shift() { return 5 - (ram->u8(MODE + 0x65) + ram->u8(MODE + 0x72) + 1) / 3; }

// The subtracted parts: the banner's band, time attack's row shading.
void ResultView::_subtracted() {
    if (flow == nullptr || _force_result()) return;
    if (_screen == GameFlow::State::TIME_RESULT) {
        _time_rows(true);
    } else {
        fill(wide_left(), 0xCB, wide_right() - wide_left(), 0x2A, psx(0x606060));
    }
}

// FUN_800F18FC's stripes (native: their own background pass, out to the window's edges in the
// wide mode).
void ResultView::_stripes() {
    if (flow == nullptr || _screen == GameFlow::State::TIME_RESULT || _force_result()) return;
    gd::Int left = static_cast<gd::Int>(std::floor(wide_left())), right = static_cast<gd::Int>(std::ceil(wide_right()));
    gd::Int x = ram->s32(ResultScreens::BANNER_BASE) - 0x180;
    for (gd::Int k = 0; k < 7; ++k) {
        gd::Int w = ram->u32(BANNER_TILES + 4 * k);
        gd::Int xx = x;
        if (widened())
            while (xx > left - 0x30) xx -= 0x180;
        while (xx < std::max<gd::Int>(right, 0x170)) {
            if (xx > std::min<gd::Int>(left, 0) - 0x30)
                ft4(xx, 0x5F, 0x30, 0x50, w & 0xFF, (w >> 8) & 0xFF, 0x18, 0x28, w >> 16, 0x6E, modulation(psx(0x606060)));
            xx += 0x180;
        }
        x += 0x30;
    }
}

// The members and the fight grid's boxes.
void ResultView::_middle() {
    if (flow == nullptr || _screen == GameFlow::State::TIME_RESULT) return;
    if (_force_result()) {
        _force_portrait();
        return;
    }
    if (_team()) {
        _team_grid();
        _team_members(MODE + 0x66, ram->u32(S + ResultScreens::T_LOST2), 0x28, _rows_shift() * 0x1E + 0xC0, true);
        _team_members(MODE + 0x59, ram->u32(S + ResultScreens::T_LOST1), 0x28, 100, false);
    }
}

// FUN_800EE100's colour bars (additive).
void ResultView::_added() {
    if (flow == nullptr || !_team()) return;
    for (gd::Int k = 0; k < 2; ++k) {
        gd::Int e = _label(k);
        gd::Int shift = k == 1 ? _rows_shift() * 0x1E : 0;
        gd::Int top = ram->u16(e + 0xE) + shift;
        gd::Int bottom = ram->u16(e + 0x14) + shift;
        gd::Int left = ram->u16(e + 0xC);
        gd::Int mid = ram->u16(e + 0x10);
        gd::Int right = ram->u16(e + 0x12);
        gfx::Color edge = psx(ram->u32(e + 0x18));
        gfx::Color middle = psx(ram->u32(e + 0x1C));
        gfx::Color end = psx(ram->u32(e + 0x20));
        gradient(left, top, mid - left, bottom - top, edge, middle, edge, middle);
        gradient(mid, top, right - mid, bottom - top, middle, end, middle, end);
    }
}

gd::Int ResultView::_label(gd::Int k) {
    gd::Int j = ram->u8(MODE + 0x1C) == 1 && k == ram->u8(MODE + 0x1F) ? k + 2 : k;
    return TEAM_LABELS + 0x24 * j;
}

// The text slot: drawn last-linked first.
void ResultView::_front() {
    if (flow == nullptr) return;
    switch (_screen) {
        case GameFlow::State::TEAM_RESULT: {
            _team_grid_texts();
            _team_lines();
            for (gd::Int k = 0; k < 2; ++k) {
                gd::Int e = _label(k);
                gd::Int shift = k == 1 ? _rows_shift() * 0x1E : 0;
                print_fmt("%c%f%H%V" + ram->string(ram->u32(e)), {ram->u32(e + 8), 1, ram->u16(e + 4), ram->u16(e + 6) + shift, 1});
            }
            _team_message();
            ptext(STR_TEAM_TITLE, {6, 1, 0x43, 0x1C});
            break;
        }
        case GameFlow::State::TIME_RESULT:
            _time_rows(false);
            _your_time();
            ptext(STR_TA_TITLE, {6, 1, 0x43, 0x20});
            break;
        case GameFlow::State::FORCE_RESULT:
            _force_texts();
            break;
        case GameFlow::State::SURVIVAL_RESULT:
            _survivors();
            if (ram->s32(S + ResultScreens::S_FLAG) > 1) {
                _survival_wins(ram->s32(S + ResultScreens::S_TOTAL), 0x78, 0x160);
                ptext(STR_TOTAL, {6, 1, 0x54, 0x148});
                if (ram->u32(S + ResultScreens::S_WAIT) == 0) _survival_rank(ram->s32(S + ResultScreens::S_RANK), 400);
            }
            ptext(STR_SURV_TITLE, {6, 1, 0x56, 0x1C});
            break;
        default:
            break;
    }
}

// ---- team battle

// FUN_800EE3C0: YOU WIN! / YOU LOSE! / DRAW GAME / PLAYER-n WINS!, sliding in.
void ResultView::_team_message() {
    if (ram->u32(S + ResultScreens::T_MSG) == 0) return;
    gd::Int left1 = ram->s32(S + ResultScreens::T_LEFT1);
    gd::Int left2 = ram->s32(S + ResultScreens::T_LEFT2);
    gd::Int msg = 0;
    gd::Int way = -1;
    if (ram->u8(MODE + 0x1C) == 1) {
        if (left1 == left2) {
            msg = 2;
        } else {
            gd::Int mine = ram->s32(S + ResultScreens::T_LEFT1 + 4 * ram->u8(MODE + 0x1F));
            gd::Int theirs = ram->s32(S + ResultScreens::T_LEFT1 + 4 * ram->u8(MODE + 0x1E));
            msg = (mine < theirs ? 1 : 0) ^ 1;
        }
        way = ram->u8(MODE + 0x1E) == 0 ? -1 : 1;
    } else if (left1 == left2) {
        msg = 2;
    } else if (left2 < left1) {
        msg = 3;
    } else {
        msg = 4;
        way = 1;
    }
    std::string s = ram->string(ram->u32(TEAM_MESSAGES + 8 * msg));
    gd::Int n = static_cast<gd::Int>(s.size());
    gd::Int slide = ram->s32(S + ResultScreens::T_SLIDE);
    gd::Int x = slide == 0 ? 0xB8 : Fx::div_trunc(slide * 0x170, 0x1E) * way + 0xB8;
    x -= (n * 0x16) >> 1;
    gd::Int yo = _rows_shift() * 0xF;
    if (slide == 0) {
        gd::Int col = left1 == left2 ? 0xC0C0C0 : (left2 < left1 ? 0xFF : 0xC0FF);
        gfx::Color c = psx(col);
        fill(x - 8, yo + 0x9C, n * 0x16 + 0x10, 0x2E, gfx::Color(c.r, c.g, c.b, 0.5f));
    }
    text(s, 2, ram->u32(TEAM_MESSAGES + 8 * msg + 4), x, yo + 0x9E);
}

// FUN_800EE694: a team's eight member slots (portrait, greyed when defeated, or an empty box).
void ResultView::_team_members(gd::Int names, gd::Int lost, gd::Int x, gd::Int y, bool second) {
    gd::Int count = ram->u8(names + 0xC);
    gd::Int n = gd::mini(count, 8);
    lost = Fx::w32(lost);
    for (gd::Int i = 0; i < 8; ++i) {
        gd::Int ch = gd::mini(ram->u8(names + i), 0x58);
        gd::Int flags = 0;
        if (i < lost) {
            flags = 0x60;
        } else if (i == lost && i < n) {
            flags = second ? 1 : 2;
        }
        if (i < count) {
            portrait(x, y, ch, flags, frame_count);
        } else {
            fill(x, y, 0x24, 0x40, psx(0x303030));
            gradient(x + 1, y + 2, 0x22, 0x3C, gfx::Color::black(), gfx::Color::black(), psx(0xC0C0C), psx(0xC0C0C));
        }
        x += 0x24;
    }
}

// FUN_800F131C: a 16 x 29 face, with flag 0x40 the lost mark over it.
void ResultView::_small_face(gd::Int x, gd::Int y, gd::Int key, gd::Int flags) {
    if ((flags & 0x80) == 0) _small_face_picture(x, y, key, flags);
    if ((flags & 0x40) != 0) {
        ft4(x, y, 0xF, 0xF, 0, 0xE0, 0x1F, 0x1F, 0x7F1D, 0x1F);
        ft4(x, y + 0x10, 0xF, 0xF, 0x20, 0xE0, 0x1F, 0x1F, 0x7F1D, 0x1F);
    }
}

void ResultView::_small_face_picture(gd::Int x, gd::Int y, gd::Int key, gd::Int flags) {
    gd::Int i = key == 0x59 ? 0x17 : key >> 2;
    gd::Int uvc = ram->u32(FACE_UV + 8 * i);
    gd::Int tp = ram->u16(FACE_UV + 8 * i + 4);
    if ((flags & 0x20) != 0) uvc = (uvc & 0xFFFF) | 0x7D500000LL;
    gd::Int clut = uvc >> 16;
    gd::Int u = uvc & 0xFF;
    gd::Int v = (uvc >> 8) & 0xFF;
    if (i == 0x14) {
        ft4(x, y, 0xF, 0xB, u, v, 0x1F, 0x17, clut, tp);
        ft4(x, y + 0xC, 0xF, 0xB, (u + 0x20) & 0xFF, v, 0x1F, 0x17, clut, tp);
        ft4(x, y + 0x18, 0xF, 4, (u + 0x40) & 0xFF, v, 0x1F, 9, clut, tp);
    } else if (i < 0x16) {
        ft4(x, y, 0xF, 0x1C, u, v, 0x1F, 0x39, clut, tp);
    } else {
        ft4(x, y, 0xF, 4, u, v, 0x1F, 9, clut, tp);
        ft4(x, y + 5, 0xF, 0xB, (u + 0x20) & 0xFF, v, 0x1F, 0x17, clut, tp);
        ft4(x, y + 0x11, 0xF, 0xB, (u + 0x40) & 0xFF, v, 0x1F, 0x17, clut, tp);
    }
}

gd::Int ResultView::_fights() { return gd::mini(Fx::w32(ram->u32(S + ResultScreens::T_FIGHT)), ram->u8(MODE + 0x58)); }

// FUN_800EE870: one box per fight (both faces, result colour), empty boxes after.
void ResultView::_team_grid() {
    gd::Int names = MODE + 0x59;
    gd::Int limit = _fights();
    gd::Int a = 0;
    gd::Int b = 0;
    gd::Int shift = _rows_shift() * 0x1E;
    gd::Int col = 0;
    gd::Int f1 = 0;
    gd::Int f2 = 0;
    gd::Int fight_count = gd::maxi(limit, 0);
    for (gd::Int i = 0; i < fight_count; ++i) {
        gd::Int code = ram->u16(MODE + 0x38 + 2 * i);
        switch (code) {
            case 1:
                col = 0xFF;
                f1 = 2;
                f2 = 0x60;
                break;
            case 2:
                col = 0xC0FF;
                f1 = 0x60;
                f2 = 1;
                break;
            case 3:
                col = 0xC0C0C0;
                f1 = 0;
                f2 = 0;
                break;
            default:
                break;
        }
        gd::Int ch1 = gd::mini(ram->u8(names + a), 0x58);
        gd::Int ch2 = gd::mini(ram->u8(names + b + 0xD), 0x58);
        if (code == 2 || code == 3) a += 1;
        if (code == 1 || code == 3) b += 1;
        gd::Int xo = (i % 3) * 0x60;
        gd::Int yo = (i / 3) * 0x1E + shift;
        _grid_box(xo, yo, col);
        _small_face(xo + 0x47, yo + 0x126, ch1, f1);
        _small_face(xo + 0x6F, yo + 0x126, ch2, f2);
    }
    gd::Int j = gd::maxi(limit, 0);
    while (j < ram->u8(MODE + 0x65) + ram->u8(MODE + 0x72) - 1) {
        gd::Int xo = (j % 3) * 0x60;
        gd::Int yo = (j / 3) * 0x1E + shift;
        _grid_box(xo, yo, 0x404040);
        fill(xo + 0x47, yo + 0x126, 0x10, 0x1D, psx(0x201010));
        fill(xo + 0x6F, yo + 0x126, 0x10, 0x1D, psx(0x201010));
        j += 1;
    }
}

void ResultView::_grid_box(gd::Int xo, gd::Int y, gd::Int col) {
    fill(xo + 0x28, y + 0x126, 0x5F, 0x1D, gfx::Color::black());
    gradient(xo + 0x28, y + 0x135, 0x5F, 0xE, gfx::Color::black(), gfx::Color::black(), psx(col), psx(col));
}

void ResultView::_team_grid_texts() {
    gd::Int limit = gd::maxi(_fights(), 0);
    gd::Int shift = _rows_shift() * 0x1E;
    gd::Int total = ram->u8(MODE + 0x65) + ram->u8(MODE + 0x72) - 1;
    gd::Int boxes = gd::maxi(limit, total);
    for (gd::Int i = 0; i < boxes; ++i) {
        gd::Int xo = (i % 3) * 0x60;
        gd::Int yo = (i / 3) * 0x1E;
        ptext(STR_VS_ROW, {i < limit ? 6 : 0x1A, 0, xo + 0x2C, shift + yo + 0x132, i + 1});
    }
}

// FUN_800EF0D8: the lines joining the members of each fight (the current one grows).
void ResultView::_team_lines() {
    gd::Int fight = _fights();
    gd::Int shift = _rows_shift() * 0x1E;
    for (gd::Int k = fight - 1; k > -1; k -= 1) {
        gd::Int a = ram->s32(S + ResultScreens::T_MEMBER1 + 4 * k);
        gd::Int b = ram->s32(S + ResultScreens::T_MEMBER2 + 4 * k);
        gd::Int code = ram->s16(MODE + 0x38 + 2 * k);
        gd::Int col = code == 1 ? 0xF8 : (code == 2 ? 0xE0F8 : 0xE0E0E0);
        _line4(a * 0x24 + 0x39, 0xA4, b * 0x24 + 0x39, shift + 0xC0, col);
    }
    gd::Int anim = ram->s32(S + ResultScreens::T_LINE);
    if (anim == 0) return;
    gd::Int x1 = ram->s32(S + ResultScreens::T_MEMBER1 + 4 * fight) * 0x24 + 0x39;
    gd::Int x2 = ram->s32(S + ResultScreens::T_MEMBER2 + 4 * fight) * 0x24 + 0x39;
    gd::Int y1 = 0xA4;
    gd::Int y2 = shift + 0xC0;
    gd::Int t = gd::mini(anim, 8);
    gd::Int colour = 0;
    switch (ram->u16(MODE + 0x38 + 2 * fight)) {
        case 1:
            x2 = x1 + (((x2 - x1) * t) >> 3);
            colour = 0xF8;
            y2 = (((shift + 0x1C) * t) >> 3) + 0xA4;
            break;
        case 2:
            x1 = x2 + (((x1 - x2) * t) >> 3);
            colour = 0xE0F8;
            y1 = y2 + (((0xA4 - y2) * t) >> 3);
            break;
        case 3: {
            gd::Int mid = ((x2 - x1) >> 1) + x1;
            x1 = mid + (((x1 - mid) * t) >> 3);
            x2 = mid + (((x2 - mid) * t) >> 3);
            gd::Int ym = ((shift + 0x1C) >> 1) + 0xA4;
            y1 = ym + (((0xA4 - ym) * t) >> 3);
            colour = 0xE0E0E0;
            y2 = ym + (((y2 - ym) * t) >> 3);
            break;
        }
        default:
            break;
    }
    _line4(x1, y1, x2, y2, colour);
}

// A three-pixel-wide slanted band (POLY_F4 from (x1, y1) to (x2, y2)).
void ResultView::_line4(gd::Int x1, gd::Int y1, gd::Int x2, gd::Int y2, gd::Int col) {
    gfx::Color c = psx(col);
    quad(gfx::Vec2(static_cast<float>(x1), static_cast<float>(y1)), gfx::Vec2(static_cast<float>(x1 + 3), static_cast<float>(y1)),
         gfx::Vec2(static_cast<float>(x2), static_cast<float>(y2)), gfx::Vec2(static_cast<float>(x2 + 3), static_cast<float>(y2)), c, c,
         c, c);
}

// ---- time attack

gd::Int ResultView::_stage_time(gd::Int k, gd::Int last) { return k < 10 ? ram->u32(MODE + 0x40 + 8 * k) : last; }

// The stage rows (the one being added grows): `shading` draws their subtracted gradients.
void ResultView::_time_rows(bool shading) {
    gd::Int rows = ram->s32(S + ResultScreens::A_ROWS);
    gd::Int anim = ram->s32(S + ResultScreens::A_ROW_ANIM);
    gd::Int last = 0;
    gd::Int row_count = gd::maxi(rows, 0);
    for (gd::Int k = 0; k < row_count; ++k) {
        last = _stage_time(k, last);
        _stage_row(k, last, (ram->u8(LADDER + 4 * k) << 2) | ram->u8(LADDER + 4 * k + 1), 0x40, shading);
    }
    if (anim != 0) {
        gd::Int k = gd::maxi(rows, 0);
        last = _stage_time(k, last);
        gd::Int t = Fx::w32(last * anim);
        if (t < 0) t += 0xF;
        _stage_row(k, (t >> 4) & 0xFFFFFFFFLL, (ram->u8(LADDER + 4 * k) << 2) | ram->u8(LADDER + 4 * k + 1), 0, shading);
    }
}

// FUN_800EFA9C: STAGE n, the stage time and the opponent's face.
void ResultView::_stage_row(gd::Int stage, gd::Int frames, gd::Int key, gd::Int flags, bool shading) {
    gd::Int x = ram->s16(TA_ROW_POS + 4 * stage);
    gd::Int y = ram->s16(TA_ROW_POS + 4 * stage + 2);
    if (shading) {
        gradient(x + 0xD, y + 0x18, 0x80, 0x28, psx(0xDEDEDE), gfx::Color::black(), psx(0xDEDEDE), gfx::Color::black());
        return;
    }
    _small_face(x + 0xF, y + 0x1E, key, flags);
    frames = gd::mini(Fx::w32(frames), TIME_CAP);
    ptext(STR_TIME, {6, 1, y + 0x24, x + 0x22, frames / 60 / 60, x + 0x42, (frames / 60) % 60, x + 0x66, (frames % 60) * 100 / 60});
    ptext(STR_TICKS, {6, 1, y + 0x20, x + 0x39, x + 0x5C});
    ptext(stage + 1 < 10 ? STR_STAGE_1X : STR_STAGE_2D, {6, 0, x + 0x22, y + 0x10, stage + 1});
}

// FUN_800EFD14: YOUR TIME, the total and the player's face; NEW RECORD blinks.
void ResultView::_your_time() {
    if (ram->u32(S + ResultScreens::A_SHOW) == 0) return;
    if (ram->u32(S + ResultScreens::A_WAIT) == 0 && ram->u32(S + ResultScreens::A_RECORD) != 0)
        ptext(STR_NEW_RECORD, {(frame_count & 3) != 0 ? 5 : 6, 1, 0x62, 0x1A8});
    portrait(0x121, 0x170, ram->u32(S + ResultScreens::A_KEY), 0, frame_count);
    gd::Int t = gd::mini(ram->s32(S + ResultScreens::A_TOTAL), TIME_CAP);
    ptext(STR_TOTAL_TIME, {7, 2, 0x5E, 0x17C, t / 60 / 60, 0x97, (t / 60) % 60, 0xD7, (t % 60) * 100 / 60});
    ptext(STR_TOTAL_TICKS, {7, 2, 0x80, 0x17C, 0xBD, 200});
    ptext(STR_YOUR_TIME, {2, 1, 0x25, 0x160});
}

// ---- survival

// FUN_800F0858: each beaten character's portrait and win count.
void ResultView::_survivors() {
    gd::Int rows = ram->s32(S + ResultScreens::S_ROW);
    gd::Int row_count = gd::maxi(rows, 0);
    for (gd::Int i = 0; i < row_count; ++i) {
        gd::Int ch = ram->u8(S + ResultScreens::S_CHARS + i);
        gd::Int wins = i >= rows - 1 ? ram->u32(S + ResultScreens::S_ANIM) : ram->u16(MODE + 0x60 + 2 * ch);
        gd::Int x = ram->s16(SURVIVOR_POS + 4 * i);
        gd::Int y = ram->s16(SURVIVOR_POS + 4 * i + 2);
        portrait(x + 2, y + 2, ch << 2, 0x40, frame_count);
        if (ram->u32(S + ResultScreens::S_FLAG) != 0) {
            // The full count (bug #28 not reproduced: the game prints it modulo 100 and shows the
            // hundreds only by the digits' colour).
            wins = Fx::w32(wins);
            std::string shown = gd::str(wins);
            text(_lpad(shown, 2), 0, ram->u8(SURVIVOR_COLOURS),
                 x + 0x14 - 9 * gd::maxi(static_cast<gd::Int>(shown.size()) - 2, 0), y + 0x44);
        }
    }
}

// FUN_800F04C4: the total and WIN / WINS.
void ResultView::_survival_wins(gd::Int wins, gd::Int x, gd::Int y) {
    gd::Int value = gd::mini(wins, 99'999'999);
    text(_lpad(gd::str(value), 3), 2, 0, x + 0x1A, y);
    ptext(wins < 2 ? STR_WIN : STR_WINS, {6, 1, x + 0x60, y + 0xE});
}

// FUN_800F068C: YOU ARE THE nth / GREATEST SURVIVOR!, or YOU NEED MORE PRACTICE!
void ResultView::_survival_rank(gd::Int rank, gd::Int y) {
    if (rank == 0) {
        text(ram->string(STR_PRACTICE), 1, (frame_count & 0x10) != 0 ? 5 : 2, 0x22, y + 0x1A);
        return;
    }
    gd::Int suffix = gd::mini(rank, 4);
    if (rank > 100) rank = 99;
    gd::Int tens = rank / 10;
    std::string line = _replace(ram->string(STR_YOU_ARE), "%c", "");
    std::string number = (tens == 0 ? std::string(" ") : gd::str(tens)) + gd::str(rank - tens * 10);
    // "YOU ARE THE%c" then the number and ordinal in the blinking colour.
    text(line, 1, 5, 0x56, y);
    text(number + ram->string(ram->u32(ORDINALS + 4 * suffix)), 1, (frame_count & 2) != 0 ? 6 : 2,
         0x56 + static_cast<gd::Int>(line.size()) * 0xD, y);
    text(ram->string(STR_GREATEST), 1, 5, 0x43, y + 0x1C);
}

// ---- Tekken Force

// FUN_800F1C00 at (0x10, 100): the player's big picture (mirrored when it faces left) under a
// gradient from its tint, framed.
void ResultView::_force_portrait() {
    gd::Ref<GameGlobals> gl = flow->globals;
    gd::Int side = flow->region->ctx8(ResultScreens::FORCE_PLAYER) & 1;
    gd::Int word = flow->data->attribute(gl->player_char[side], gl->player_costume[side]);
    gd::Int picture = word & 0x1F;
    gd::Int x = 0x12;
    gd::Int y = 100 + 6;
    if (picture < _portraits.size()) {
        gfx::Rect2 dest = r(x, y, 0x7E, 0xD4);
        if (ram->u8(S + ResultScreens::F_FLIP) != 0) {
            // A negative size flips the picture in place.
            dest.size.x = -dest.size.x;
        }
        target->draw_texture_rect_region(_portraits[picture], dest, gfx::Rect2(0, 0x14, 0x7E, 0xD4));
    }
    gfx::Color tint = psx(ram->u32(S + ResultScreens::F_COLOUR));
    gradient(x, y, 0x7E, 0x6A, gfx::Color(0, 0, 0, 0), gfx::Color(0, 0, 0, 0), gfx::Color(tint.r, tint.g, tint.b, 0.35f),
             gfx::Color(tint.r, tint.g, tint.b, 0.35f));
    gradient(x, y + 0x6A, 0x7E, 0x6A, gfx::Color(tint.r, tint.g, tint.b, 0.35f), gfx::Color(tint.r, tint.g, tint.b, 0.35f),
             gfx::Color(0.75f, 0.75f, 0.75f, 0.35f), gfx::Color(0.75f, 0.75f, 0.75f, 0.35f));
    // Untyped Array records [x, y, w, h]: gd::Arr<gd::Int>.
    for (const gd::Arr<gd::Int>& e :
         gd::Arr<gd::Arr<gd::Int>>{{x - 2, y - 2, 0x82, 2}, {x - 2, y + 0xD4, 0x82, 2}, {x - 2, y, 2, 0xD4}, {x + 0x7E, y, 2, 0xD4}})
        fill(e[0], e[1], e[2], e[3], psx(0xF0F0F0));
}

// FUN_800F1F08's texts, each appearing by the frames shown (with the sounds the step plays).
void ResultView::_force_texts() {
    gd::Ref<ModeRegion> region = flow->region;
    gd::Int shown = ram->s32(S + ResultScreens::F_SHOWN);
    gd::Int frame = ram->s32(S + ResultScreens::F_FRAME);
    print_fmt("%c%f%H%VTEKKEN FORCE", {0, 2, 0x34, 0x1C});
    gd::Int y = 0x78;
    if (shown > 0x1D) {
        print_fmt("%f%c%H%VYOUR SCORE:", {1, 5, 0x9E, 0x78});
        y = 0xC6;
        print_fmt("%f%c%H%V%8D", {1, 6, 0xF9, 0x92, region->ctx32(ResultScreens::FORCE_SCORE)});
    }
    if (shown > 0x3B) {
        print_fmt("%f%c%H%VHIGH SCORE:", {1, 5, 0x9E, y});
        print_fmt("%f%c%H%V%8D", {1, 6, 0xF9, y + 0x1A, gd::Int(flow->progress->force_hi_score)});
        y += 0x34;
        if (region->ctx8(0x3F) != 0) print_fmt("%f%c%H%VNEW RECORD!!", {1, (frame & 2) != 0 ? 6 : 2, 0xC5, y});
    }
    gd::Int flags = region->ctx8(ResultScreens::FORCE_FLAGS);
    if ((flags & ResultScreens::FORCE_BOSSES) == 0) {
        if ((flags & ResultScreens::FORCE_DOCTOR) != 0) {
            if (shown > 0x59)
                print_fmt("%f%c%H%VYOU SAVED %cDR.BOSKONOVITCH!", {1, 8, 0xF, y + 0xB6, (frame & 2) != 0 ? 6 : 2});
            return;
        }
        if (shown > 0x59) print_fmt("%f%c%H%VKEYS:", {1, 5, 0x9E, y + 0x4E});
        for (gd::Int k = 0; k < 3; ++k) {
            if ((flags & gd::shl(1, k)) != 0 && shown > FORCE_KEY_AFTER[k])
                print_fmt("%f%c%H%V" + FORCE_KEYS[k], {1, FORCE_KEY_COLOURS[k], 0xDF, y + 0x4E + 0x1A * k});
        }
        if (shown >= 0xB4 && (shown & 0x30) != 0) print_fmt("%f%c%H%VTO BE CONTINUED...", {1, 8, 0x43, y + 0xB6});
        return;
    }
    gd::Int bosses = ram->s32(S + ResultScreens::F_BOSS);
    if (shown > 0x59) print_fmt("%f%c%H%VBOSS:", {1, 5, 0x27, y + 0x68});
    for (gd::Int k = 0; k < 4; ++k) {
        if ((k == 0 && shown > 0x59) || (k > 0 && bosses > FORCE_BOSS_AFTER[k]))
            portrait(0x34 + 0x24 * k, y + 0x82, region->ctx8(0x41 + k), 0, frame);
    }
    if (ram->s32(S + ResultScreens::F_PHASE) >= 1) {
        print_fmt("%f%c%H%VFORCE:", {1, 5, 0xDF, y + 0x68});
        print_fmt("%f%c%H%V%4D", {1, 6, 0x12D, y + 0x82, ram->s32(S + ResultScreens::F_FORCE)});
    }
}

}  // namespace t3
