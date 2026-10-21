// remake/presentation/screens/select_view.gd
#include "presentation/screens/select_view.h"

#include <algorithm>

#include "content/flow_data.h"
#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "core/math/fx.h"

namespace t3 {

void SelectView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                       const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    pictures_dir = screens_dir;
    reload_pictures();
    add_pass(gfx::Blend::ADD, [this] { _backlights(); });
    add_pass(gfx::Blend::MIX, [this] { _middle(); });
    add_texel_mask_pass([this] { _portraits(Bands::SHAPES); });
    add_pass(gfx::Blend::SUB, [this] { _portraits(Bands::SUBTRACTED); });
    add_pass(gfx::Blend::ADD, [this] { _glowing(); });
    add_pass(gfx::Blend::MIX, [this] { _cursors(); });
}

// The portraits, from the texture pack in use.
void SelectView::reload_pictures() {
    portraits = character_pictures(pictures_dir, "portraits");
}

void SelectView::show_state(const gd::Ref<GameFlow>& game) {
    select = game->select;
    frame_count = game->fight->vblank;
    redraw();
}

bool SelectView::_shown() {
    return select != nullptr && select->frames() >= CharacterSelect::DRAW_FROM;
}

gd::Int SelectView::rec(gd::Int p) {
    return CTX + CharacterSelect::RECORDS + CharacterSelect::RECORD_SIZE * p;
}

void SelectView::_draw() {
    begin();
    black();
    if (_shown()) _background();
}

// FUN_801102A0: the black top, the title pictures, and over them the diagonally scrolling tiles
// (clipped above the grid).
void SelectView::_background() {
    gd::Int limit = ram->s32(CTX + 0x128);
    fill(0, 0, SCREEN_X, limit, gfx::Color::black());
    picture(0x80, 0x3A, 0x6E, 0x20, 0x24D, 0x112, 0x7CA4, 0);
    picture(0x54, 0x62, 0xC6, 0xE, 0x242, 0x13A, 0x7CA4, 0);
    picture(0x80, 0x78, 0x70, 0x1E, 0x24D, 0x150, 0x7CA4, 0);
    gd::Int s = (-(frame_count << 7)) & 0xFFFFFFFFLL;
    gd::Int v = Fx::w32(s) >> 4;
    gd::Int frac = (0x40 - v) & 0x3F;
    gd::Int h = 0x40 - frac;
    gd::Int row = 7 - ((v - 1) >> 6);
    gd::Int y = static_cast<gd::Int>(DISPLAY().position.y);   // DISPLAY is a Rect2i in the GDScript
    while (y < gd::mini(0x155, limit)) {
        gd::Int pat = BG_ROWS + (row & 7) * 8;
        gd::Int x = 0;
        gd::Int du = 8;
        gd::Int w = 0x28;
        for (gd::Int i = 0; i < 8; ++i) {
            gd::Int uv = ram->u32(BG_TILES + 4 * ram->u8(pat));
            pat += 1;
            uv += du;
            uv += frac << 8;
            sprt(x, y, w, gd::mini(h, limit - y), uv & 0xFFFFFFFFLL, 0x18);
            x += w;
            w = 0x30;
            du = 0;
        }
        y += h;
        h = 0x40;
        frac = 0;
        row += 1;
    }
}

gfx::Color SelectView::_attr_colour(gd::Int p) {
    gd::Int key = ram->s32(rec(p) + CharacterSelect::R_SHOWN_KEY);
    gd::Int costume = ram->s32(rec(p) + CharacterSelect::R_SHOWN_COSTUME);
    return psx(select->g->data->attribute(key, costume) >> 8);
}

// FUN_8010FBFC: the glow behind each portrait, in the character's colour (added).
void SelectView::_backlights() {
    if (!_shown()) return;
    for (gd::Int p = 0; p < 2; ++p) {
        gfx::Color c = _attr_colour(p);
        gd::Int x = ram->u16(rec(p) + 0x54);
        gd::Int y = ram->s32(CTX + 0x120);
        gradient(x, y - 0x14, 0x7E, 0xE4, gfx::Color::black(), gfx::Color::black(), c, c);
        gradient(x, y + 0xD0, 0x7E, 0x48, c, c, psx(0xC0C0C0), psx(0xC0C0C0));
    }
}

// The layout's pictures, the grid of faces, the name plates, the time limit, the NEW arrows and
// the portraits' fully bright bands.
void SelectView::_middle() {
    if (!_shown()) return;
    _pictures();
    _grid_faces();
    _name_plates();
    gd::Int d = ram->s32(CTX + 0x18);
    if (d != 0) timer(gd::div(ram->s32(CTX + 0x14) + d - 1, d), 0xB8, ram->u32(CTX + 0x140), ram->u32(CTX + 0x144));
    _new_arrows();
    _portraits(Bands::OPAQUE);
}

// The portraits' fading bands and the sparks (added).
void SelectView::_glowing() {
    if (!_shown()) return;
    _portraits(Bands::FADING);
    _sparks();
}

// FUN_80110154: the gradient under the grid, then the layout's static pictures over it.
void SelectView::_pictures() {
    gd::Int two_rows = ram->u32(CTX + 4);
    gd::Int c = two_rows != 0 ? 0xD0 : 0;
    gd::Int y0 = ram->s32(CTX + 0x128) + 0x10;
    gradient(0, y0, SCREEN_X, SCREEN_Y - y0, gfx::Color::black(), gfx::Color::black(), psx(c), psx(c));
    gd::Int e = IMAGE_LISTS[two_rows != 0 ? 1 : 0];
    gd::Arr<gd::Int> entries;
    while (ram->u16(e) != 0xFFFF) {
        entries.append(e);
        e += 16;
    }
    // Linked in list order: the last picture is drawn first.
    entries.reverse();
    for (gd::Int at : entries)
        picture(ram->u16(at + 2), ram->u16(at + 4), ram->u16(at + 6), ram->u16(at + 8), ram->u16(at + 10), ram->u16(at + 12),
                ram->u16(at + 14), ram->u16(at));
}

// FUN_8010F760: the big portraits in three bands, brightening in over 32 frames, mirrored to face
// the middle, from the layout's first texel row (v 0; the CLUT moves on every 64 rows, which the
// pictures carry). `kind`: which of the bands' draws (Bands) this pass makes.
void SelectView::_portraits(gd::Int kind) {
    if (!_shown()) return;
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rp = rec(p);
        gd::Int key = ram->s32(rp + CharacterSelect::R_SHOWN_KEY);
        gd::Int costume = ram->s32(rp + CharacterSelect::R_SHOWN_COSTUME);
        if (key >= CharacterSelect::NO_CELL) continue;
        gd::Int attr = select->g->data->attribute(key, costume) & 0xFF;
        if ((attr & 0x1F) >= portraits.size()) continue;
        bool subtracted = attr == SUBTRACTED_PORTRAIT;
        if (subtracted != (kind == Bands::SUBTRACTED)) continue;
        gd::Int slide = ram->s32(rp + CharacterSelect::R_CHOSEN_ANIM);
        gd::Int k = slide != 0 ? 0x20 - slide : 0x20;
        gd::Int x = ram->u16(rp + 0x54);
        gd::Int y = ram->s32(CTX + 0x120);
        bool flip = ram->u32(rp + CharacterSelect::R_FLIP) == 1;
        gfx::TextureRef tex = portraits[attr & 0x1F];
        gd::Int bands = PORTRAIT_BANDS + (attr >> 6) * 12;
        gd::Int w_prev = ram->s32(BAND_SHADES);
        gd::Int row = 0;
        for (gd::Int band = 0; band < 3; ++band) {
            gd::Int b0 = Fx::w32(w_prev * k) >> 5;
            w_prev = ram->s32(BAND_SHADES + 4 + 4 * band);
            gd::Int b1 = Fx::w32(w_prev * k) >> 5;
            gd::Int h = ram->s32(bands + 4 * band);
            bool opaque = b0 == b1 && b0 >= FULL && !subtracted;
            bool shown = b0 != b1 || b0 != 0;
            if (h > 0 && shown && opaque == (kind == Bands::OPAQUE))
                _band(tex, x, y, h, row, flip, _band_shade(b0, opaque), _band_shade(b1, opaque));
            y += h;
            row += h;
        }
    }
}

// A band edge's colour: FUN_8004E254 scales 0x7F7F7F by the brightness (/0x100), a texture
// modulation of 0x80 = 1.
gfx::Color SelectView::_band_shade(gd::Int brightness, bool opaque) {
    if (opaque) return gfx::Color::white();
    gfx::Color c = modulation(psx(scale_colour(0x7F7F7F, brightness)));
    return gfx::Color(std::min(c.r, 1.0f), std::min(c.g, 1.0f), std::min(c.b, 1.0f));
}

void SelectView::_band(const gfx::TextureRef& tex, float x, float y, gd::Int h, gd::Int row, bool flip, const gfx::Color& top,
                       const gfx::Color& bottom) {
    gfx::Vec2 size(static_cast<float>(tex->width()), static_cast<float>(tex->height()));
    float u0 = 0.0f;
    float u1 = PORTRAIT_W / size.x;
    if (flip) {
        float t = u0;
        u0 = u1;
        u1 = t;
    }
    float v0 = row / size.y;
    float v1 = (row + h) / size.y;
    std::vector<gfx::Vec2> points{pt(x, y), pt(x + PORTRAIT_W, y), pt(x + PORTRAIT_W, y + h), pt(x, y + h)};
    std::vector<gfx::Vec2> uvs{gfx::Vec2(u0, v0), gfx::Vec2(u1, v0), gfx::Vec2(u1, v1), gfx::Vec2(u0, v1)};
    target->draw_polygon(points, std::vector<gfx::Color>{top, top, bottom, bottom}, uvs, tex);
}

// FUN_8010FD64: the grid of small faces (the two-row layout shows the HUD portraits).
void SelectView::_grid_faces() {
    gd::Int dy = ram->s32(CTX + 0x12C);
    gd::Int two_rows = ram->u32(CTX + 4);
    for (gd::Int i = CharacterSelect::CELL_COUNT - 1; i > -1; i -= 1) {
        gd::Int c = CharacterSelect::CELLS_BASE + CharacterSelect::CELL_SIZE * i;
        gd::Int key = ram->s16(c + 6);
        if (ram->u8(c + 5) == 0 || key >= 0x16) continue;
        gd::Int x = ram->s16(c + 8);
        gd::Int y = ram->s16(c + 0xA) + dy;
        if (two_rows != 0)
            portrait(x, y, key << 2, 8);
        else
            sprt(x, y, 0x20, 0x44, ram->u32(SMALL_FACES + 8 * key), ram->u16(SMALL_FACES + 8 * key + 4));
    }
}

namespace {

// `halves`' records ([ordering slot, link order reversed, x, y, uv, tint]); Array.sort() orders the
// arrays element by element, and (slot, order) is unique, so it decides alone.
struct SelectHalf {
    gd::Int slot;
    gd::Int order;
    gd::Int x;
    gd::Int y;
    gd::Int uv;
    gfx::Color tint;
    bool operator<(const SelectHalf& o) const { return slot != o.slot ? slot < o.slot : order < o.order; }
};

// `tags`' records: [x, y, uv, tint].
struct SelectTag {
    gd::Int x;
    gd::Int y;
    gd::Int uv;
    gfx::Color tint;
};

}  // namespace

// FUN_8010FEA0: each side's cursor frame (pulsing while choosing) in two halves, each side's own
// half in the higher slot, and its tag above.
void SelectView::_cursors() {
    if (!_shown()) return;
    gd::Arr<SelectHalf> halves;
    gd::Arr<SelectTag> tags;
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rp = rec(p);
        gd::Int kind = ram->u32(rp + 8);
        if (kind == 0) continue;
        gd::Int level = 0x80;
        gd::Int layer = 0;
        if (kind == 1 || kind == 3) {
            level = triangle(frame_count << 4);
            layer = 2;
        }
        gd::Int c = CharacterSelect::CELLS_BASE + CharacterSelect::CELL_SIZE * ram->u32(rp + 4);
        if (ram->u8(c + 5) == 0) continue;
        gd::Int x = ram->s16(c + 8) - 2;
        gd::Int y = ram->s16(c + 0xA) + ram->s32(CTX + 0x12C);
        gd::Int k = scale_colour(0xFFFFFF, level) & 0xFF;
        gfx::Color tint(k / 128.0f, k / 128.0f, k / 128.0f);
        bool chosen = ((kind - 3) & 0xFFFFFFFFLL) < 2;
        gd::Int uv = chosen ? ram->u32(rp + 0x70) : ram->u32(rp + 0x6C);
        tags.append(SelectTag{x + ram->u16(rp + 0x7A), y + ram->s32(CTX + 0x13C), uv, tint});
        uv = ram->u32(CTX + 4) == 0 ? ram->u32(rp + 0x64) : ram->u32(rp + 0x68);
        if (chosen) uv = (uv & 0xFFFF) | 0x7CA90000;
        y += ram->s32(CTX + 0x138);
        for (gd::Int i = 0; i < 2; ++i) {
            halves.append(SelectHalf{layer * 4 + (i == p ? 4 : 0), -(p * 2 + i), x, y, uv, tint});
            x += 0x12;
            uv = (uv & 0xFFFFFF00LL) | ((uv + 0x12) & 0xFF);
        }
    }
    std::stable_sort(halves.begin(), halves.end());
    for (const SelectHalf& hv : halves) sprt(hv.x, hv.y, 0x12, ram->u32(CTX + 0x134), hv.uv, 0x19, hv.tint);
    for (const SelectTag& t : tags) sprt(t.x, t.y, 0x14, 0x10, t.uv, 0xE, t.tint);
}

// FUN_8010EDFC: each side's name plate (and the start prompt of a side that can join).
void SelectView::_name_plates() {
    gd::Int y = ram->s32(CTX + 0x124);
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rp = rec(p);
        gd::Int x = ram->u16(rp + 0x56);
        gd::Int kind = ram->s32(rp + 0xC);
        if (kind == 2) coin(p, x, 0x22, ram->u32(CTX + 0x148), frame_count);
        if (kind == 1 || kind == 2) {
            gd::Int w = ram->u32(rp + CharacterSelect::R_RECORD5);
            if (w != 0) {
                gd::Int n = ram->u32(rp + CharacterSelect::R_BASE);
                gd::Int uv = ((n << 3) & 0x80) | ((n & 0xF) << 12) | 0x7CA50000;
                sprt(x - (w >> 1), y, w, 0x10, uv, 0xB);
            }
        }
    }
}

// FUN_8010EC9C: blinking arrows at the grid's ends while a character is new.
void SelectView::_new_arrows() {
    if (ram->u32(CTX + 0x10) == 0) return;
    gd::Int y = ram->s32(CTX + 0x128) - 0xA0;
    gd::Int v = triangle(frame_count << 4);
    gd::Int d = frame_count & 0x1F;
    if (d >= 0x11) d = 0x1F - d;
    d >>= 1;
    gfx::Color tint(v / 128.0f, v / 128.0f, v / 128.0f);
    sprt(0xC - d, y, 0xA, 0x10, 0x7E1500E0, 0x19, tint);
    sprt(0x15A + d, y, 0xA, 0x10, 0x7E1500EA, 0x19, tint);
}

// FUN_8010F020: the rising streaks (added).
void SelectView::_sparks() {
    for (const gd::Ref<CharacterSelect::Spark>& spark : select->sparks) {
        if (spark->active != 1) continue;
        float a = gd::clampi(spark->bright, 0, 0xFF) / 255.0f;
        gfx::Color top(a, a, a);
        gradient(spark->x, spark->y, 1, 0xA0, top, top, gfx::Color::black(), gfx::Color::black());
    }
}

}  // namespace t3
