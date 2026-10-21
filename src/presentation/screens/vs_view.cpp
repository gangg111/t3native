// remake/presentation/screens/vs_view.gd
#include "presentation/screens/vs_view.h"

#include "core/fight/fight_state.h"
#include "core/game/game_flow.h"
#include "gd/io.h"

namespace t3 {

namespace {

// String.replace(what, forwhat): every occurrence, left to right (the runtime has no string replace).
std::string _replace(const std::string& s, const std::string& what, const std::string& forwhat) {
    if (what.empty()) return s;
    std::string out;
    size_t at = 0;
    while (true) {
        size_t found = s.find(what, at);
        if (found == std::string::npos) break;
        out += s.substr(at, found - at);
        out += forwhat;
        at = found + what.size();
    }
    out += s.substr(at);
    return out;
}

}  // namespace

void VsView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image,
                   const std::string& screens_dir) {
    setup_psx(hud_data, game_ram, image);
    _system = image;
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<VramImage> v = image->duplicate_image();
        v->upload_file(gd::path_join(screens_dir, "makuma_" + gd::str(i) + ".tims"));
        backgrounds.append(v);
    }
    pictures_dir = screens_dir;
    reload_pictures();
    // The caption bar's tail: subtracted, then added (VS_BAR_MODES 0x40, 0x20).
    add_pass(gfx::Blend::SUB, [this] { _caption_tail(); });
    add_pass(gfx::Blend::ADD, [this] { _caption_tail(); });
    add_pass(gfx::Blend::ADD, [this] { _glows(); });
    add_pass(gfx::Blend::MIX, [this] { _front(); });
}

// The portraits, from the texture pack in use.
void VsView::reload_pictures() {
    portraits = character_pictures(pictures_dir, "vs_portraits");
}

void VsView::show_state(const gd::Ref<GameFlow>& game) {
    vs = game->vs_screen;
    frame_count = game->fight->vblank;
    gd::Int b = vs->background - BACKGROUND_BASE;
    vram = b >= 0 && b < backgrounds.size() ? backgrounds[b] : _system;
    redraw();
}

gd::Arr<gd::Ref<VsScreen::VsObject>> VsView::_objects(gd::Int kind) {
    gd::Arr<gd::Ref<VsScreen::VsObject>> out;
    if (vs == nullptr) return out;
    for (const gd::Ref<VsScreen::VsObject>& o : vs->objects) {
        if (o->kind == kind) out.append(o);
    }
    return out;
}

void VsView::_draw() {
    begin();
    black();
    if (vs == nullptr || vs->objects.is_empty()) return;
    if (!_objects(VsScreen::Kind::BACKGROUND).is_empty()) _background();
    if (vs->slide == 0) {
        for (const gd::Ref<VsScreen::VsObject>& o : _objects(VsScreen::Kind::CAPTION_BAR))
            fill(o->x, o->y, o->dx - 0x6E, 0x24, psx(o->colour));
    }
}

// FUN_80051C9C: the 7 x 6 grid of background cells (a black tile while sliding).
void VsView::_background() {
    if (vs->slide != 0) return;
    gd::Int cell = 0;
    gd::Int v_last = -1;
    gd::Int y_top = 0x14;
    for (gd::Int row = 0; row < 7; ++row) {
        gd::Int va = (v_last + 1) & 0xFF;
        v_last += 0x20;
        gd::Int vb = v_last & 0xFF;
        gd::Int y_bot = ram->u32(ROW_EDGES + 4 * row) >> 16;
        gd::Int x_left = 0;
        gd::Int u_left = 0;
        gd::Int u_last = -1;
        for (gd::Int col = 0; col < 6; ++col) {
            gd::Int clut = 0x7D00 | ((cell & 0x30) << 2) | (cell & 0xF);
            cell += 1;
            gd::Int x_right = ram->u16(COL_EDGES + 2 * col);
            gd::Int u_right = (u_last + 0x20) & 0xFF;
            ft4(x_left, y_top, x_right - x_left, y_bot - y_top, u_left & 0xFF, va, u_right - (u_left & 0xFF) + 1,
                vb - va + 1, clut, 0xD);
            u_left = u_last + 0x21;
            x_left = x_right;
            u_last += 0x20;
        }
        y_top = y_bot;
    }
}

// FUN_80051E74: the caption bar's gradient tail (drawn at rest).
void VsView::_caption_tail() {
    if (vs == nullptr || vs->slide != 0) return;
    for (const gd::Ref<VsScreen::VsObject>& o : _objects(VsScreen::Kind::CAPTION_BAR)) {
        gfx::Color c = psx(o->colour);
        gradient(o->x + o->dx - 0x6E, o->y, 0x6E, 0x24, c, gfx::Color::black(), c, gfx::Color::black());
    }
}

// The additive glows: team battle's bars (arcade.ovl FUN_800B3708) and each portrait's tint.
void VsView::_glows() {
    if (vs == nullptr) return;
    if (vs->slide == 0 && !_objects(VsScreen::Kind::TEAM_BARS).is_empty()) {
        gradient(0x8E, 0x60, 0xE2, 0x58, psx(0xE0E0E0), gfx::Color::black(), psx(0x707070), gfx::Color::black());
        gradient(0, 0x15E, 0xE2, 0x58, gfx::Color::black(), psx(0x707070), gfx::Color::black(), psx(0xE0E0E0));
    }
    for (const gd::Ref<VsScreen::VsObject>& o : _objects(VsScreen::Kind::PORTRAIT)) {
        gd::Vector2i at = o->at(vs->slide);
        gfx::Color c = psx(o->colour);
        gradient(at.x + 2, at.y + 6, 0x7E, 0x6A, gfx::Color::black(), gfx::Color::black(), c, c);
        gradient(at.x + 2, at.y + 0x70, 0x7E, 0x6A, c, c, psx(0xC0C0C0), psx(0xC0C0C0));
    }
}

// The portraits, icons, frames and texts, the last objects first.
void VsView::_front() {
    if (vs == nullptr) return;
    gd::Arr<gd::Ref<VsScreen::VsObject>> objects = vs->objects.duplicate();
    objects.reverse();
    for (const gd::Ref<VsScreen::VsObject>& o : objects) {
        gd::Vector2i at = o->at(vs->slide);
        switch (o->kind) {
            case VsScreen::Kind::PORTRAIT:
                _portrait(o, at);
                break;
            case VsScreen::Kind::ICON:
                portrait(at.x, at.y, o->arg, o->flags, frame_count);
                break;
            case VsScreen::Kind::FRAME:
                if (vs->slide == 0) fill(at.x, at.y, o->dx, o->dy, psx(0xE0E0E0));
                break;
            default:
                break;
        }
    }
    for (const gd::Ref<VsScreen::VsObject>& o : objects) {
        if (o->kind == VsScreen::Kind::NAME || (o->kind == VsScreen::Kind::TEXT && vs->slide == 0)) {
            gd::Vector2i at = o->kind == VsScreen::Kind::NAME ? o->at(vs->slide) : gd::Vector2i(o->x, o->y);
            std::string s = _replace(o->text, "%d", gd::str(o->arg));
            text(s, o->font, o->colour, at.x, at.y);
        }
    }
}

// FUN_800520A0: a 126 x 212 portrait, mirrored to face the middle, in its frame.
void VsView::_portrait(const gd::Ref<VsScreen::VsObject>& o, gd::Vector2i at) {
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int e = FRAME_RECTS + 8 * i;
        gd::Int wh = ram->u32(e + 4);
        fill(at.x + ram->s16(e), at.y + ram->s16(e + 2), wh & 0xFFFF, wh >> 16, psx(FRAME_COLOUR));
    }
    if (o->picture >= portraits.size()) return;
    gfx::Rect2 dest = r(at.x + 2, at.y + 6, PORTRAIT_W, PORTRAIT_H);
    if (o->facing != 0) {
        // A negative size flips the texture in place.
        dest.size.x = -dest.size.x;
    }
    target->draw_texture_rect_region(portraits[o->picture], dest, gfx::Rect2(0, PORTRAIT_TOP, PORTRAIT_W, PORTRAIT_H));
}

}  // namespace t3
