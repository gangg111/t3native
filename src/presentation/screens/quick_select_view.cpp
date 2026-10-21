// remake/presentation/screens/quick_select_view.gd
#include "presentation/screens/quick_select_view.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "content/fight_content.h"
#include "content/fight_tables.h"
#include "core/ball/tekken_ball.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/game/byte_block.h"
#include "core/game/game_flow.h"
#include "core/game/game_mode.h"
#include "core/game/game_progress.h"
#include "core/math/fx.h"
#include "gd/json.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

using Vector3 = QuickSelectView::Vector3;

// Godot's Basis (rows), for BallView.rot_matrix.
struct Basis {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Basis() = default;
    // Basis(axis, angle): the rotation by `angle` about a unit axis.
    Basis(const Vector3& axis, float angle) {
        float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
        float x = axis.x, y = axis.y, z = axis.z;
        m[0][0] = t * x * x + c;     m[0][1] = t * x * y - s * z; m[0][2] = t * x * z + s * y;
        m[1][0] = t * x * y + s * z; m[1][1] = t * y * y + c;     m[1][2] = t * y * z - s * x;
        m[2][0] = t * x * z - s * y; m[2][1] = t * y * z + s * x; m[2][2] = t * z * z + c;
    }
    Basis operator*(const Basis& o) const {
        Basis out;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) out.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return out;
    }
    Vector3 operator*(const Vector3& v) const {
        return Vector3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                       m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }
};

// Vector2.cross.
float _cross(gfx::Vec2 a, gfx::Vec2 b) { return a.x * b.y - a.y * b.x; }

// BallView's (presentation/ball/ball_view.gd, not translated yet) static helpers and tables that
// this view reads, translated here; replace with the real class once it exists.
namespace BallView {

constexpr gd::Int VERTICES = 0x800B64A8LL;           // 42 SVECTORs
constexpr gd::Int NORMALS = 0x800B65F8LL;            // per vertex: SVECTOR
constexpr gd::Int TRIANGLES = 0x800B6748LL;          // 16 x 4 vertex indices
constexpr gd::Int QUADS = 0x800B6788LL;              // 32 x 4
constexpr gd::Int COLOURS = 0x800B6828LL;            // per type (64 bytes): 8 triangle colours, 8 quad colours
constexpr gd::Int VERTEX_COUNT = 42;
const gd::Arr<gd::Int> FACE_COUNTS{16, 32};          // triangles, quads

// A game SVECTOR (three s16) in RAM, in the game's frame.
Vector3 svector(const gd::Ref<GameRam>& ram, gd::Int at) {
    return Vector3(ram->s16(at), ram->s16(at + 2), ram->s16(at + 4));
}

// Face `i` of a pass (0: the triangles, 1: the quads): its vertex indices, and its colour in a
// ball type (the colours cycle by face index & 7).
gd::PI32 face_corners(const gd::Ref<GameRam>& ram, gd::Int pass_, gd::Int i) {
    gd::Int table = pass_ == 0 ? TRIANGLES : QUADS;
    gd::PI32 idx;
    for (gd::Int k = 0; k < 3 + pass_; ++k) idx.append(ram->u8(table + 4 * i + k));
    return idx;
}

gfx::Color face_colour(const gd::Ref<GameRam>& ram, gd::Int t, gd::Int pass_, gd::Int i) {
    return ScreenCanvas::psx(ram->u32(COLOURS + 64 * t + 0x20 * pass_ + 4 * (i & 7)));
}

// libgte RotMatrix (4096 units): R = Rx . Ry . Rz in the game's y-down frame.
Basis rot_matrix(const gd::PI32& angles) {
    return Basis(Vector3(1, 0, 0), static_cast<float>(WorldSpace::radians(angles[0]))) *
           Basis(Vector3(0, 1, 0), static_cast<float>(WorldSpace::radians(angles[1]))) *
           Basis(Vector3(0, 0, 1), static_cast<float>(WorldSpace::radians(angles[2])));
}

}  // namespace BallView

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

// String.split(delimiter) (empty parts kept).
std::vector<std::string> _split(const std::string& s, const std::string& delimiter) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        size_t found = s.find(delimiter, at);
        if (found == std::string::npos) break;
        out.push_back(s.substr(at, found - at));
        at = found + delimiter.size();
    }
    out.push_back(s.substr(at));
    return out;
}

// "%x" of a nonnegative int.
std::string _hex(gd::Int v) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string out;
    do {
        out.insert(out.begin(), DIGITS[v & 0xF]);
        v >>= 4;
    } while (v != 0);
    return out;
}

// "%02d" of a nonnegative int.
std::string _two_digits(gd::Int v) {
    std::string s = gd::str(v);
    return s.size() < 2 ? "0" + s : s;
}

gd::Int _length(const std::string& s) { return static_cast<gd::Int>(s.size()); }

}  // namespace

QuickSelectView::Vector3 QuickSelectView::Vector3::normalized() const {
    float l = std::sqrt(x * x + y * y + z * z);
    if (l == 0.0f) return Vector3();
    return Vector3(x / l, y / l, z / l);
}

void QuickSelectView::setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image) {
    setup_psx(hud_data, game_ram, image);
    add_pass(gfx::Blend::SUB, [this] { _mode_picture(); });
    add_pass(gfx::Blend::MIX, [this] { _stripes(); });
    add_pass(gfx::Blend::ADD, [this] { backdrop(1, ram->u32(BACK_COLOURS + 4 + 8 * _mode())); });
    add_pass(gfx::Blend::MIX, [this] { _front(); });
}

void QuickSelectView::show_state(const gd::Ref<GameFlow>& game) {
    flow = game;
    frame_count = game->fight->vblank;
    fade_level = NO_FADE;
    if (game->sub <= 1) {
        _started = false;
        return;
    }
    if (!_started) {
        _started = true;
        _idle = 0;
        _stripe = 0;
        _banner = 0;
        for (gd::Int p = 0; p < 2; ++p) _bars[p] = ram->s32(_rec(p) + 0x60);
    }
    // The frame's counters, as the drawing advances them.
    _idle = game->held(0) != 0 || game->held(1) != 0 ? 0 : _idle + 1;
    _stripe = gd::posmod(_stripe + 0x17F, STRIPE_PERIOD);
    _banner = (_banner + 0x342) % BANNER_PERIOD;
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int target = ram->s32(_rec(p) + 0x5C);
        gd::Int cur = _bars[p];
        if (cur != target) cur = gd::absi(target - cur) < 0x18 ? target : cur + (target > cur ? 0x18 : -0x18);
        _bars[p] = cur;
    }
    if (ram->u32(CTX + 8) == 1) fade_level = ram->s32(CTX + 0xC);
    redraw();
}

gd::Int QuickSelectView::_mode() {
    return ram->u32(CTX);
}

gd::Int QuickSelectView::_rec(gd::Int p) {
    return CTX + QuickSelect::RECORDS + QuickSelect::RECORD_SIZE * p;
}

gd::Int QuickSelectView::_cell(gd::Int column, gd::Int row) {
    return GRID + 6 * ((column + row * 7) & 0xFFFFFFFFLL);
}

gd::Int QuickSelectView::_key_under_cursor(gd::Int rec) {
    gd::Int key = ram->u8(_cell(ram->u32(rec + 8), ram->u32(rec + 0xC)) + 4);
    gd::Int bit = gd::shl(1, (key >> 2) & 31);
    if ((ram->u32(rec + 0x24) & bit) == 0) key = (ram->u32(GameProgress::BASE) & bit) != 0 ? TAKEN : LOCKED;
    return key;
}

void QuickSelectView::_draw() {
    begin();
    black();
    if (flow == nullptr || !_started) return;
    backdrop(0, ram->u32(BACK_COLOURS + 8 * _mode()));
}

// The mode's picture (arcade.ovl 0x800B33CC, 0x800B34A8, 0x800B367C): shading subtracted from
// the backdrop.
void QuickSelectView::_mode_picture() {
    if (!_started) return;
    gd::Int mode = _mode();
    gfx::Color grey = psx(0x808080);
    gfx::Color none = gfx::Color::black();
    if (gd::Arr<gd::Int>{GameMode::ARCADE, GameMode::TIME_ATTACK, GameMode::SURVIVAL, GameMode::PRACTICE, GameMode::DEMO,
                         GameMode::FORCE}
            .has(mode)) {
        gradient(0x50, 0x140, 0x44, 0x70, none, psx(0x282828), none, grey);
        gradient(0xDC, 0x140, 0x44, 0x70, psx(0x282828), none, grey, none);
    } else if (mode == GameMode::VS || mode == GameMode::BALL) {
        gfx::Color c = psx(0x404040);
        fill(0x58, 0x13B, 0xC0, 0x50, c);
        gradient(0x30, 0x13B, 0x28, 0x50, none, c, none, c);
        gradient(0x118, 0x13B, 0x28, 0x50, c, none, c, none);
        fill(0xA8, 0x18B, 0x20, 0x16, c);
        quad(gfx::Vec2(0x9A, 0x18B), gfx::Vec2(0xA8, 0x18B), gfx::Vec2(0xA6, 0x1A1), gfx::Vec2(0xA8, 0x1A1), c, c, c, c);
        quad(gfx::Vec2(0xC8, 0x18B), gfx::Vec2(0xD6, 0x18B), gfx::Vec2(0xC8, 0x1A1), gfx::Vec2(0xCA, 0x1A1), c, c, c, c);
    }
}

// FUN_80055368: the banner (its %p puts it in the background slot) and the floor's stripes
// scrolling left.
void QuickSelectView::_stripes() {
    if (!_started) return;
    if (_mode() == GameMode::TEAM) {
        // Team battle's picture: the member areas at half brightness (blend mode 0 over black).
        for (gd::Int p = 0; p < 2; ++p) {
            gd::Int rec = _rec(p);
            fill(ram->u16(rec + 0x10), ram->s32(rec + 0x14), 0x90, 0x80, gfx::Color(0, 0, 0, 0.5f));
        }
    }
    gd::Int bx = _banner - BANNER_PERIOD;
    while (bx < 0x170) {
        text(ram->string(BANNER), 2, 6, bx, 0xCB);
        bx += BANNER_PERIOD;
    }
    gd::Int x = _stripe - STRIPE_PERIOD;
    for (gd::Int i = 0; i < 7; ++i) {
        gd::Int w = ram->u32(STRIPES + 4 * i);
        gd::Int u = w & 0xFF;
        gd::Int v = (w >> 8) & 0xFF;
        gd::Int xx = x;
        while (xx < 0x170) {
            if (xx > -0x30) ft4(xx, 0x5F, 0x30, 0x50, u, v, 0x18, 0x28, w >> 16, 0x6E);
            xx += STRIPE_PERIOD;
        }
        x += 0x30;
    }
}

// The portraits, cursors and texts over the backdrop.
void QuickSelectView::_front() {
    if (!_started) return;
    _grid();
    _members();
    _cursors();
    gd::Int mode = _mode();
    if (mode == GameMode::VS) {
        _vs_handicaps();
        _vs_record();
    } else if (mode <= GameMode::FORCE && mode != GameMode::BALL) {
        text("VS", 2, 7, 0xA1, 0x15C);
    }
    _texts();
    _title();
    if (mode == GameMode::BALL && flow->region->ctx8(TekkenBall::CHOOSER) == 3 && flow->sim->ball() != nullptr) _ball_types();
}

// volley.ovl FUN_800B593C and FUN_800B569C while a side chooses the ball: the ball rolling in
// from the side it was pushed to (the fight's renderer FUN_800B3198, render kind 10: the type's
// Gouraud faces, back faces removed), and once it rests its level, name and damage, with the two
// arrows blinking.
void QuickSelectView::_ball_types() {
    gd::Ref<TekkenBall> tb = flow->sim->ball();
    gd::Ref<GameRam> volley = flow->content->mode_ram("volley");
    gd::Int kind = flow->region->ctx8(TekkenBall::BALL_TYPE) % 3;
    _ball_sphere(tb, volley, kind);
    if (gd::absi(tb->b->s32(TekkenBall::B_POS) - tb->b->s32(TekkenBall::B_VEL)) >= 0x40) return;
    std::string level = volley->string(volley->u32(BALL_NAMES + 8 * kind));
    std::string name = volley->string(volley->u32(BALL_NAMES + 8 * kind + 4));
    text(level, 0, 0xB, 0xB8 - ((_length(level) * 9) >> 1), 0x144);
    text(name, 1, 4, 0xB8 - ((_length(name) * 0xD) >> 1), 0x156);
    text("BALL DAMAGE", 0, 5, 0x87, 0x19E);
    print_fmt("%c%f%H%V%3d%f%H%V%%", std::vector<Arg>{gd::Int(6), gd::Int(1), gd::Int(0xA0), gd::Int(0x1B0),
                                                       gd::Int(kind * 0x14 + 0x3C), gd::Int(0), gd::Int(0xC9),
                                                       gd::Int(0x1B6)});
    if ((frame_count & 0x20) != 0) {
        for (const gd::Arr<gfx::Vec2>& arrow : BALL_ARROWS) {
            std::vector<gfx::Vec2> tri;
            for (const gfx::Vec2& v : arrow) tri.push_back(pt(v.x, v.y));
            target->draw_colored_polygon(tri, gfx::Color::c8(0xFF, 0xFF, 0));
        }
    }
}

// The preview's sphere: the 42 vertices turned by the ball's angles (RotMatrix: Rx . Ry . Rz),
// moved to its position, projected; each front face in its colour (cycling by index & 7) shaded
// per vertex by its normal.
void QuickSelectView::_ball_sphere(const gd::Ref<TekkenBall>& tb, const gd::Ref<GameRam>& volley, gd::Int kind) {
    gd::Ref<ByteBlock> b = tb->b;
    gd::PI32 angles;
    angles.append(b->u16(TekkenBall::B_ANGLES));
    angles.append(b->u16(TekkenBall::B_ANGLES + 2));
    angles.append(b->u16(TekkenBall::B_ANGLES + 4));
    Basis r = BallView::rot_matrix(angles);
    Vector3 at = Vector3(b->s32(TekkenBall::B_POS), b->s32(TekkenBall::B_POS + 4), b->s32(TekkenBall::B_POS + 8)) + BALL_VIEW_T;
    Vector3 light = BALL_LIGHT_DIR.normalized();
    std::vector<gfx::Vec2> screen;   // PackedVector2Array
    std::vector<float> shade;        // PackedFloat32Array
    for (gd::Int v = 0; v < BallView::VERTEX_COUNT; ++v) {
        Vector3 p = r * BallView::svector(volley, BallView::VERTICES + 8 * v) + at;
        screen.push_back(pt(BALL_OFFSET.x + BALL_H * p.x / p.z, BALL_OFFSET.y + BALL_H * p.y / p.z));
        Vector3 n = (r * BallView::svector(volley, BallView::NORMALS + 8 * v)).normalized();
        shade.push_back(BALL_BACK + BALL_LIGHT * std::max(0.0f, n.dot(-light)));
    }
    for (gd::Int pass_ = 0; pass_ < 2; ++pass_) {
        gd::Int count = BallView::FACE_COUNTS[pass_];
        for (gd::Int i = 0; i < count; ++i) {
            gfx::Color c = BallView::face_colour(volley, kind, pass_, i);
            gd::PI32 idx = BallView::face_corners(volley, pass_, i);
            gfx::Vec2 p0 = screen[static_cast<size_t>(gd::Int(idx[0]))];
            // NormalClip: only faces turned to the screen.
            if (_cross(screen[static_cast<size_t>(gd::Int(idx[1]))] - p0, screen[static_cast<size_t>(gd::Int(idx[2]))] - p0) < 0)
                continue;
            gd::Arr<gd::PI32> tris{gd::PI32{0, 1, 2}};
            if (pass_ == 1) tris.append(gd::PI32{1, 3, 2});
            for (const gd::PI32& tri : tris) {
                std::vector<gfx::Vec2> points;
                std::vector<gfx::Color> colours;
                for (gd::Int k : tri) {
                    size_t corner = static_cast<size_t>(gd::Int(idx[k]));
                    points.push_back(screen[corner]);
                    float l = std::min(shade[corner], 2.0f);
                    colours.push_back(gfx::Color(c.r * l, c.g * l, c.b * l));
                }
                target->draw_primitive(points, colours, std::vector<gfx::Vec2>());
            }
        }
    }
}

// FUN_80054FE8: the 21 portraits; those under a choosing cursor are lit.
void QuickSelectView::_grid() {
    gd::PI32 under{TAKEN, TAKEN};
    gd::PI32 shown{TAKEN, TAKEN};
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        if (ram->u32(rec + 0x18) == 1) {
            under[p] = ram->u8(_cell(ram->u32(rec + 8), ram->u32(rec + 0xC)) + 4);
            shown[p] = _key_under_cursor(rec);
        }
    }
    if (gd::Int(under[0]) == gd::Int(under[1]) && (gd::Int(shown[0]) != TAKEN || gd::Int(shown[1]) != TAKEN))
        under = gd::PI32{TAKEN, TAKEN};
    gd::Int h0 = gd::Int(shown[0]) == TAKEN ? gd::Int(under[0]) : TAKEN;
    gd::Int h1 = gd::Int(shown[1]) == TAKEN ? gd::Int(under[1]) : TAKEN;
    for (gd::Int i = 0; i < 21; ++i) {
        gd::Int c = GRID + 6 * i;
        gd::Int key = ram->u8(c + 4);
        gd::Int flags = key == h0 || key == h1 ? 0x20 : 0;
        if ((gd::shr(ram->u32(GameProgress::BASE), (key >> 2) & 31) & 1) == 0) key = TAKEN;
        portrait(ram->u16(c), ram->s16(c + 2), key, flags, frame_count);
    }
}

// FUN_8005447C: each side's picked members (the one being chosen shows the cursor's character).
void QuickSelectView::_members() {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        gd::Int x0 = ram->s32(rec + 0x10);
        gd::Int y0 = ram->s32(rec + 0x14);
        gd::Int kind = ram->u32(rec + 0x20);
        gd::Int n = gd::maxi(ram->s32(rec + 0x28), 0);
        for (gd::Int i = 0; i < n; ++i) {
            gd::Int key = ram->s32(rec + 0x38 + 4 * i);
            gd::Int flags = 0;
            if (key > 0x57) key = LOCKED;
            if (kind == 1)
                flags = 0x84;
            else if (kind == 2 && i == ram->u32(rec + 0x2C))
                flags = 0x10;
            else if (kind == 3 && i == ram->u32(rec + 0x2C))
                key = gd::mini(_key_under_cursor(rec), LOCKED);
            portrait(x0 + (i & 3) * 0x24, y0 + (i >> 2) * 0x40, key, flags, frame_count);
        }
    }
}

namespace {

// `halves`' records ([ordering slot, link order reversed, x, y, flags, half]); Array.sort() orders
// the arrays element by element, and (slot, order) is unique, so it decides alone.
struct QuickHalf {
    gd::Int slot;
    gd::Int order;
    gd::Int sx;
    gd::Int sy;
    gd::Int flags;
    gd::Int half;
    bool operator<(const QuickHalf& o) const { return slot != o.slot ? slot < o.slot : order < o.order; }
};

// `tags`' records: [x, y, uv, tint].
struct QuickTag {
    gd::Int x;
    gd::Int y;
    gd::Int uv;
    gfx::Color tint;
};

}  // namespace

// FUN_80054668: each side's cursor frame on the grid and its tag. A frame is drawn in two halves
// clipped to draw areas, each side's own half in the higher ordering slot (P1's on the left, P2's
// on the right on top when both are on a cell); a choosing cursor's slots are above a chosen one's.
// The tags are over the frames.
void QuickSelectView::_cursors() {
    gd::Int level = triangle(frame_count << 4);
    gd::Arr<QuickHalf> halves;
    gd::Arr<QuickTag> tags;
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        gd::Int kind = ram->u32(rec + 0x18);
        if (kind == 0) continue;
        gd::Int cell = _cell(ram->u32(rec + 8), ram->u32(rec + 0xC));
        gd::Int sx = ram->s16(cell);
        gd::Int sy = ram->u16(cell + 2);
        gd::Int flags = 0;
        gfx::Color tint = gfx::Color::white();
        gd::Int layer = 0;
        if (kind == 1 || kind == 3) {
            flags = kind == 1 ? (ram->u32(rec + 0x78) | 0x10) : 0x93;
            tint = gfx::Color(level / 128.0f, level / 128.0f, level / 128.0f);
            layer = 2;
            if (_key_under_cursor(rec) > 0x57) flags = 0x90;
        } else {
            flags = kind == 2 ? ram->u32(rec + 0x78) : 0x83;
        }
        bool chosen = ((kind - 3) & 0xFFFFFFFFLL) < 2;
        gd::Int dx = ram->s16(rec + (chosen ? 0x8A : 0x88));
        gd::Int uv = ram->u32(rec + (chosen ? 0x80 : 0x7C));
        tags.append(QuickTag{sx + dx, sy - 4, uv, tint});
        for (gd::Int i = 0; i < 2; ++i) {
            // [ordering slot, link order reversed, x, y, flags, half]
            halves.append(QuickHalf{layer * 4 + (i == p ? 4 : 0), -(p * 2 + i), sx, sy, flags, i});
        }
    }
    std::stable_sort(halves.begin(), halves.end());
    for (const QuickHalf& h : halves) {
        gd::Int sx = h.sx;
        gd::Int sy = h.sy;
        gd::Int half = h.half;
        portrait_frame(sx, sy, h.flags, frame_count, gfx::Rect2(sx + 0x12 * half, sy, 0x12, 0x40));
    }
    for (const QuickTag& t : tags) sprt(t.x, t.y, 0x14, 0x10, t.uv, 0xE, t.tint);
}

// FUN_8004E9EC: the textured plate under a name.
void QuickSelectView::_name_plate(gd::Int x, gd::Int y, gd::Int w, gd::Int colour) {
    ft4(x, y, w, 10, 0xDC, 0xA8, 2, 10, ram->u16(PLATE_COLOURS + 2 * colour), 0xD);
}

// FUN_8005497C: per side the team size, the name under the cursor, the prompt or the caption.
void QuickSelectView::_texts() {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        gd::Int x = ram->u16(rec + 0x8C);
        gd::Int y = ram->u16(rec + 0x8E);
        gd::Int kind = ram->u32(rec + 0x1C);
        if (kind == 1) {
            gd::Int y1 = ram->s32(rec + 0x14);
            gd::Int xx = ram->s32(rec + 0x10) + 8;
            gd::Int n = ram->u32(rec + 0x28);
            for (gd::Int i = 0; i < 8; ++i) {
                gd::Int colour = 10;
                if (n - 1 == i) {
                    fill(xx - 2, y1 + 0x34, 0x11, 4, psx(0xFF));
                    colour = (frame_count & 2) != 0 ? 1 : 6;
                }
                text(gd::str(i + 1), 1, colour, xx, y1 + 0x18);
                xx += 0x10;
            }
            std::string who = ram->string(n < 2 ? STR_PLAYER : STR_PLAYERS);
            // "%x %s\n ON TEAM" % [n & 0xF, who]
            text(_hex(n & 0xF) + " " + who + "\n ON TEAM", 1, 5, ram->s32(rec + 0x10) + 0xC, ram->s32(rec + 0x14) + 0x48);
        } else if (kind == 2 || kind == 3) {
            gd::Int key = 0;
            if (kind == 2) {
                key = _key_under_cursor(rec);
            } else {
                gd::Int n = ram->u32(rec + 0x28);
                gd::Int done = ram->u32(rec + 0x2C);
                gd::Int i = n <= done ? n - 1 : (Fx::w32(done) > 0 ? done - 1 : done);
                key = ram->s32(rec + 0x38 + 4 * (i & 0xFFFFFFFFLL));
            }
            std::string s = "";
            gd::Int colour = 6;
            bool named = false;
            if (key < 0x58) {
                named = true;
                s = key >= 0 ? flow->fight->tables->character(key & ~3)["name"].str() : "";
            } else if (key == 0x58) {
                s = ram->string(STR_NO_ENTRY);
                colour = 9;
            } else {
                s = ram->string(STR_SOLD_OUT);
                colour = 5;
            }
            gd::Int x0 = x - ((_length(s) * 0xD) >> 1);
            text(s, 1, colour, x0, y);
            if (named) _name_plate(x0 - 4, y + 0x12, _length(s) * 0xD + 10, key & 3);
        } else if (kind == 4) {
            coin(p, x, y + 4, 10, frame_count);
        } else if (kind == 5) {
            std::string s = ram->string(CAPTIONS.get(_mode(), CAPTION_WAIT));
            gd::Int w = _length(s) * 0xD;
            gd::Int x0 = x - (w >> 1);
            gd::Int right = x0 + w;
            if (x0 < 8) {
                x0 = 8;
                right = w + 8;
            }
            if (right > 0x168) x0 = 0x168 - w;
            text(s, 1, 5, x0, y);
        }
    }
}

// FUN_80054E30: the title; after 480 idle frames it slides out and the exit hint slides in.
void QuickSelectView::_title() {
    gd::Int u = _idle > IDLE_FRAMES - 1 ? (_idle - IDLE_FRAMES) & 0xFFFFFFFFLL : 0;
    gd::Int x = (u & 0x100) != 0 ? 0x228 : 0xB8;
    if ((u & 0xE0) == 0xE0) {
        gd::Int d = ((u & 0x1F) * 0x170) >> 5;
        x = (u & 0x100) != 0 ? x - d : x + d;
    }
    std::string title = ram->string(ram->u32(TITLES + 4 * _mode()));
    text(title, 1, 6, x - ((_length(title) * 0xD) >> 1), 0x1C);
    // The hint's width counts its colour codes, as the game's strlen of the format does.
    std::string hint = ram->string(ram->u32(PROMPT));
    std::vector<std::string> parts = _split(hint, "%c");
    std::vector<Run> runs;
    gd::Arr<gd::Int> colours{5, 6, 5, 6, 5};
    gd::Int count = static_cast<gd::Int>(parts.size());
    for (gd::Int i = 0; i < count; ++i) {
        if (i > 0) runs.push_back(colours[i - 1]);
        runs.push_back(parts[static_cast<size_t>(i)]);
    }
    text_runs(runs, 0, 6, x - ((_length(hint) * 9) >> 1) - 0x170, 0x20);
}

// arcade.ovl FUN_800B3204: both handicap bars sliding toward their value, LIFE and the percentage.
void QuickSelectView::_vs_handicaps() {
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        _vs_bar(_bars[p], ram->s16(rec + 0x9E), ram->u16(rec + 0x98), ram->u32(rec + 0x58), ram->u32(rec) == 4);
    }
    for (gd::Int p = 0; p < 2; ++p) {
        gd::Int rec = _rec(p);
        gd::Int cur = _bars[p];
        text(ram->string(VS_LIFE), 0, 5, ram->s16(rec + 0xA0) + cur, ram->s16(rec + 0xA2));
        std::string pct = _replace(ram->string(ram->u32(VS_PERCENT + 4 * ram->u32(rec + 0x58))), "%%", "%");
        text(pct, 0, 6, ram->s16(rec + 0xA4) + cur, ram->s16(rec + 0xA6));
    }
}

// arcade.ovl FUN_800B2EE0: a handicap bar (8 segments, `lit` of them bright) with its end caps.
void QuickSelectView::_vs_bar(gd::Int x, gd::Int y, gd::Int right, gd::Int lit, bool pulse) {
    gd::Int level = triangle(frame_count << 4);
    gfx::Color tint = pulse ? gfx::Color(level / 128.0f, level / 128.0f, level / 128.0f) : gfx::Color::white();
    fill(right != 0 ? (x + 2) : (x - 0x80 + 2), y + 4, 0x7C, 0x10, gfx::Color::black());
    sprt(right != 0 ? x + 0x57 : x - 0x5F, y, 8, 0xA, 0x7E55B6D6, 0xD);
    gd::Int edge = right == 1 ? x + 2 : x - 1;
    for (gd::Int i = 0; i < 8; ++i) {
        gd::Int e = VS_BAR + 6 * i;
        gd::Int w = ram->u8(e);
        gd::Int x0 = right == 1 ? edge : edge - w;
        gd::Int uvt = lit < i ? ram->u16(e + 4) : ram->u16(e + 2);
        ft4(x0, y + 4, w - 1, 0x10, 0xE8, 0xAC, 1, 0x10, uvt, 0xD);
        edge = right == 1 ? w + edge : edge - w;
    }
    gd::Int bx = right != 0 ? x : x - 0x80;
    sprt(bx, y, 8, 0x18, 0x7EDCA8E0, 0xD, tint);
    sprt(bx + 0x78, y, 8, 0x18, 0x7EDCA8E8, 0xD, tint);
    ft4(bx + 8, y, 0x70, 0x18, 0xE6, 0xA8, 4, 0x18, 0x7EDC, 0xD, tint);
}

// arcade.ovl FUN_800B2D44: the VS win - win record and the draws.
void QuickSelectView::_vs_record() {
    // Rows: [mode context offset, x, y, font, colour table].
    gd::Arr<gd::Arr<gd::Int>> rows{gd::Arr<gd::Int>{0x38, 0x78, 0x14E, 2, VS_RECORD_COLOURS[0]},
                                   gd::Arr<gd::Int>{0x3C, 0xCC, 0x14E, 2, VS_RECORD_COLOURS[0]},
                                   gd::Arr<gd::Int>{0x40, 0xAB, 0x181, 1, VS_RECORD_COLOURS[1]}};
    for (const gd::Arr<gd::Int>& row : rows) {
        gd::Int n = ram->u16(MODE_CTX + row[0]);
        gd::Int colours = row[4];
        text(_two_digits(n % 100), row[3], ram->u8(colours + ((n / 100) & 3)), row[1], row[2]);
    }
    text("-", 2, 7, 0xAD, 0x14E);
    text("DRAW", 0, 7, 0xA6, 0x16D);
}

}  // namespace t3
