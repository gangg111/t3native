#pragma once
// remake/presentation/screens/quick_select_view.gd: the resident character select (FUN_80055878's
// drawing, select_sim.quick_*): the gradient backdrop with the scrolling floor stripes and banner, the
// mode's picture, the 21 HUD portraits, each side's picked members, cursor frames and tags, the texts
// (team size, the name on its plate, NO ENTRY / SOLD OUT, the coin prompt, the mode caption), the
// title that gives way to the exit hint after eight idle seconds, and VS's handicap bars and win
// record. The game's ordering table draws the background first, then the portraits, the cursors and
// the texts; the semi-transparent parts are passes in their blend modes.
//
// The drawing's own counters (the idle time, the stripes' and banner's scroll, the handicap bars'
// slide) live here: the game keeps them in the screen's context, which only its drawing reads.
#include <string>

#include "core/game/mode_region.h"
#include "core/game/quick_select.h"
#include "presentation/screens/psx_canvas.h"

namespace t3 {

class GameFlow;
class TekkenBall;

class QuickSelectView : public PsxCanvas {
public:
    // Godot's Vector3 (floats), for the ball preview's constants and maths.
    struct Vector3 {
        float x = 0, y = 0, z = 0;
        Vector3() = default;
        Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
        Vector3 operator-() const { return Vector3(-x, -y, -z); }
        float dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
        Vector3 normalized() const;
    };

    static constexpr gd::Int CTX = QuickSelect::BASE;
    static constexpr gd::Int GRID = 0x800229D4LL;               // 21 x (s16 x, s16 y, u8 key, pad)
    static constexpr gd::Int TITLES = 0x800985A8LL;
    static constexpr gd::Int PROMPT = 0x800985C0LL;
    static constexpr gd::Int BANNER = 0x800985CCLL;
    static inline const gd::Dict<gd::Int, gd::Int> CAPTIONS = [] {
        gd::Dict<gd::Int, gd::Int> d;
        d.set(3, 0x80022BD8LL);
        d.set(4, 0x80022BE8LL);
        d.set(5, 0x80022BF4LL);
        d.set(8, 0x80022C00LL);
        return d;
    }();
    static constexpr gd::Int CAPTION_WAIT = 0x80022C10LL;
    static constexpr gd::Int STR_NO_ENTRY = 0x80022B98LL;
    static constexpr gd::Int STR_SOLD_OUT = 0x80022BA4LL;
    static constexpr gd::Int STR_PLAYER = 0x80022BB0LL;
    static constexpr gd::Int STR_PLAYERS = 0x80022BB8LL;
    static constexpr gd::Int STRIPES = 0x80022A54LL;
    static constexpr gd::Int BACK_COLOURS = 0x80022DB8LL;
    static constexpr gd::Int PLATE_COLOURS = 0x80022198LL;
    static inline const gd::Arr<gd::Int> VS_RECORD_COLOURS{0x800B4EA8LL, 0x800B4EACLL};
    static constexpr gd::Int VS_LIFE = 0x800B0B94LL;
    static constexpr gd::Int VS_PERCENT = 0x800B4EB0LL;
    static constexpr gd::Int VS_BAR = 0x800B0B24LL;
    static constexpr gd::Int MODE_CTX = ModeRegion::BASE + 0x50;
    static constexpr gd::Int LOCKED = QuickSelect::LOCKED;
    static constexpr gd::Int TAKEN = QuickSelect::TAKEN;
    static constexpr gd::Int IDLE_FRAMES = 0x1E0;
    static constexpr gd::Int STRIPE_PERIOD = 0x180;
    static constexpr gd::Int BANNER_PERIOD = 0x344;
    static constexpr gd::Int BALL_NAMES = 0x800B6928LL;         // volley.ovl: per type (level, name) string pointers
    // The preview's projection (FUN_800B5B6C): the view is 0x800AE460, which GsInitGraph sets to
    // the identity at start-up (no display aspect: the ball shows flattened), translated by
    // (-0x40, 0x440, 0x1380); the GTE's H 500 and screen offsets 192, 240.
    static inline const Vector3 BALL_VIEW_T{-0x40, 0x440, 0x1380};
    static constexpr float BALL_H = 500.0f;
    static inline const gfx::Vec2 BALL_OFFSET{192, 240};
    // The light of the preview's scene (FUN_80039BB4(5) and SetBackColor(64, 64, 64)): the back colour
    // is 64 / 128; the lit share and direction stand in for stage 5's light matrix.
    static constexpr float BALL_BACK = 0.5f;
    static constexpr float BALL_LIGHT = 0.75f;
    static inline const Vector3 BALL_LIGHT_DIR{-0.4f, -0.6f, -0.7f};
    static inline const gd::Arr<gd::Arr<gfx::Vec2>> BALL_ARROWS{
        {gfx::Vec2(0xFC, 0x14E), gfx::Vec2(0xFC, 0x15E), gfx::Vec2(0x104, 0x156)},
        {gfx::Vec2(0x74, 0x14E), gfx::Vec2(0x74, 0x15E), gfx::Vec2(0x6C, 0x156)}};

    gd::Ref<GameFlow> flow;
    gd::Int frame_count = 0;
    gd::Int _idle = 0;
    gd::Int _stripe = 0;
    gd::Int _banner = 0;
    gd::PI32 _bars{0, 0};
    bool _started = false;

    void setup(const gd::Ref<HudData>& hud_data, const gd::Ref<GameRam>& game_ram, const gd::Ref<VramImage>& image);
    void show_state(const gd::Ref<GameFlow>& game);
    gd::Int _mode();
    gd::Int _rec(gd::Int p);
    gd::Int _cell(gd::Int column, gd::Int row);
    gd::Int _key_under_cursor(gd::Int rec);
    void _draw() override;
    // The mode's picture (arcade.ovl 0x800B33CC, 0x800B34A8, 0x800B367C): shading subtracted from
    // the backdrop.
    void _mode_picture();
    // FUN_80055368: the banner (its %p puts it in the background slot) and the floor's stripes
    // scrolling left.
    void _stripes();
    // The portraits, cursors and texts over the backdrop.
    void _front();
    // volley.ovl FUN_800B593C and FUN_800B569C while a side chooses the ball: the ball rolling in
    // from the side it was pushed to (the fight's renderer FUN_800B3198, render kind 10: the type's
    // Gouraud faces, back faces removed), and once it rests its level, name and damage, with the two
    // arrows blinking.
    void _ball_types();
    // The preview's sphere: the 42 vertices turned by the ball's angles (RotMatrix: Rx . Ry . Rz),
    // moved to its position, projected; each front face in its colour (cycling by index & 7) shaded
    // per vertex by its normal.
    void _ball_sphere(const gd::Ref<TekkenBall>& tb, const gd::Ref<GameRam>& volley, gd::Int kind);
    // FUN_80054FE8: the 21 portraits; those under a choosing cursor are lit.
    void _grid();
    // FUN_8005447C: each side's picked members (the one being chosen shows the cursor's character).
    void _members();
    // FUN_80054668: each side's cursor frame on the grid and its tag. A frame is drawn in two halves
    // clipped to draw areas, each side's own half in the higher ordering slot (P1's on the left, P2's
    // on the right on top when both are on a cell); a choosing cursor's slots are above a chosen one's.
    // The tags are over the frames.
    void _cursors();
    // FUN_8004E9EC: the textured plate under a name.
    void _name_plate(gd::Int x, gd::Int y, gd::Int w, gd::Int colour);
    // FUN_8005497C: per side the team size, the name under the cursor, the prompt or the caption.
    void _texts();
    // FUN_80054E30: the title; after 480 idle frames it slides out and the exit hint slides in.
    void _title();
    // arcade.ovl FUN_800B3204: both handicap bars sliding toward their value, LIFE and the percentage.
    void _vs_handicaps();
    // arcade.ovl FUN_800B2EE0: a handicap bar (8 segments, `lit` of them bright) with its end caps.
    void _vs_bar(gd::Int x, gd::Int y, gd::Int right, gd::Int lit, bool pulse);
    // arcade.ovl FUN_800B2D44: the VS win - win record and the draws.
    void _vs_record();
};

}  // namespace t3
