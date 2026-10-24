#pragma once
// remake/presentation/screens/screen_canvas.gd: the base of the game's 2D screens: the original
// 368 x 480 screen scaled into the window's 4:3 area, the game's bitmap fonts in their 16 colours
// (FUN_8004D15C, the HUD's glyph sheets) and flat, Gouraud and textured shapes in the original
// coordinates.
//
// The game's semi-transparent primitives blend with what is under them (PSX modes: B+F, B-F); a view
// draws them in passes (`add_pass`): child items in their own blend mode, drawn after the canvas and
// the passes before them, in the order the game's ordering table draws them.
//
// In Godot the canvas is a Control redrawn on demand; here `draw(canvas, size)` draws the whole view
// (its `_draw`, then each pass) into the frame's gfx::Canvas, and `redraw()` / `queue_redraw()` are
// no-ops kept for the views' code.
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "content/hud_data.h"
#include "gfx/canvas.h"
#include "presentation/hud/hud_text.h"

namespace t3 {

class ScreenCanvas {
public:
    static constexpr gd::Int SCREEN_X = 368, SCREEN_Y = 480;
    static constexpr gd::Int NO_FADE = 0x100;
    static constexpr gd::Int MAX_POLYGON_X = 1023, MAX_POLYGON_Y = 511;   // the GPU's largest polygon extent
    static constexpr gd::Int PERCENT = 0x25;   // print_fmt's characters: "%" (and HudText.NEWLINE), the width digits
    static constexpr gd::Int DIGIT_0 = 0x30;
    static constexpr gd::Int DIGIT_8 = 0x38;
    static constexpr gd::Int BCD_LIMIT = 100'000'000;   // FUN_8004CE74: numbers from here on print as 99999999
    static gfx::Vec2 SCREEN() { return gfx::Vec2(static_cast<float>(SCREEN_X), static_cast<float>(SCREEN_Y)); }

    gd::Ref<HudData> hud;
    gfx::Rect2 box;
    gfx::Canvas* target = nullptr;   // the canvas the helpers draw into (set by `draw`)
    gfx::Vec2 size;                  // the Control's size: the window
    bool visible = false;
    // The screen fade the view draws this frame (FUN_8004E2E8's level; 0x100 none): the game adds it
    // to the frame's fades.
    gd::Int fade_level = NO_FADE;

    virtual ~ScreenCanvas() = default;

    void setup_canvas(const gd::Ref<HudData>& hud_data);
    // Draws the view (when visible): `_draw`, then its passes in order.
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    virtual void _draw() {}

    // Call first in `_draw`: the 4:3 area of this frame.
    void begin() { box = layout_box(); }
    // Where the 368 x 480 screen lies in the window: the centred 4:3 box.
    virtual gfx::Rect2 layout_box() const;
    // A drawing pass in a blend mode: `painter` draws it with the helpers, after the canvas's own
    // `_draw` and the passes added before it. `clip` (screen coordinates) limits it to a draw area.
    void add_pass(gfx::Blend blend, std::function<void()> painter, gfx::Rect2 clip = gfx::Rect2());
    // A subtractive pass in which each opaque texel subtracts the vertex colour (a texture drawn
    // through a CLUT of white entries: texel_mask_sub.gdshader).
    void add_texel_mask_pass(std::function<void()> painter);
    // Native: the wide mode (the select screens' backgrounds over the whole window, 21:9 too). On
    // a canvas that takes it (`wide_background`: its `_draw` and the passes added with
    // add_background_pass are background), those draw past the 4:3 box out to the window's edges,
    // and everything else stays clipped to the box (what the original hides past its edges, a
    // title sliding out, a bar sliding in, stays hidden).
    bool wide = false;
    bool wide_background = false;
    void add_background_pass(gfx::Blend blend, std::function<void()> painter);
    // The window's left and right edges in screen x (0 and SCREEN_X but in the wide mode).
    float wide_left() const;
    float wide_right() const;
    // The window's top and bottom edges in screen y (0 and SCREEN_Y but in the wide mode on a
    // window taller than 4:3: a tablet, an unfolded phone).
    float wide_top() const;
    float wide_bottom() const;
    // A picture filling the screen, and in the wide mode its edge rows and columns stretched out to
    // the window's edges (a plain-coloured edge, the options' blue, goes on as it is).
    void image_wide(const gfx::TextureRef& tex);
    bool widened() const { return wide && wide_background; }
    // The wide mode's foreground area in screen coordinates (what ScreenMask leaves open otherwise).
    virtual gfx::Rect2 foreground_area() const { return gfx::Rect2(0, 0, static_cast<float>(SCREEN_X), static_cast<float>(SCREEN_Y)); }
    void redraw() { box = layout_box(); }
    void queue_redraw() {}

    virtual gfx::Vec2 pt(float x, float y) const;
    gfx::Rect2 r(float x, float y, float w, float h) const;
    void black();
    void fill(float x, float y, float w, float h, const gfx::Color& c);
    // LINE_F2: a one-pixel line, clipped to `clip` (a draw area) when given.
    void line(float x0, float y0, float x1, float y1, const gfx::Color& c, gfx::Rect2 clip = gfx::Rect2());
    // A quad with a colour per corner: top-left, top-right, bottom-left, bottom-right.
    void gradient(float x, float y, float w, float h, const gfx::Color& tl, const gfx::Color& tr, const gfx::Color& bl,
                  const gfx::Color& br);
    // POLY_GT4 over an axis-aligned rectangle: a texture modulated by a colour per corner.
    void textured(const gfx::TextureRef& tex, float x, float y, float w, float h, const gfx::Color& c0, const gfx::Color& c1,
                  const gfx::Color& c2, const gfx::Color& c3);
    // A PSX texture colour as a modulation (0x80 is 1.0).
    static gfx::Color modulation(const gfx::Color& c) { return gfx::Color(c.r * 2.0f, c.g * 2.0f, c.b * 2.0f, 1.0f); }
    // POLY_G4 with its corners anywhere, drawn as the GPU does: triangles (0, 1, 2) and (1, 2, 3).
    void quad(gfx::Vec2 p0, gfx::Vec2 p1, gfx::Vec2 p2, gfx::Vec2 p3, const gfx::Color& c0, const gfx::Color& c1,
              const gfx::Color& c2, const gfx::Color& c3);
    void image(const gfx::TextureRef& tex, float x, float y, float w = -1.0f, float h = -1.0f,
               const gfx::Color& modulate_colour = gfx::Color::white(), bool flip = false);
    // The pixel width of a text in a font (one advance per character).
    gd::Int text_width(const std::string& text, gd::Int font_index) const;
    // FUN_8004D15C: a text in the game's font and colour from (x, y); "\n" starts a new line.
    virtual void text(const std::string& s, gd::Int font_index, gd::Int colour, float x, float y, float alpha = 1.0f);
    // FUN_8004D15C with colour changes: `runs` alternates text and colour, starting with a text.
    using Run = std::variant<std::string, gd::Int>;
    void text_runs(const std::vector<Run>& runs, gd::Int font_index, gd::Int colour, float x, float y);
    // FUN_8004D15C with its format: %c colour, %f font, %H / %V the pen's x / y, %h / %v the same in
    // character cells, %s, %C a character, %d and %x with an optional width, %p, %%.
    using Arg = std::variant<gd::Int, std::string>;
    void print_fmt(const std::string& fmt, const std::vector<Arg>& args, gd::Int font_index = 0, gd::Int colour = 0);
    static std::string _number_text(gd::Int value, char code, gd::Int width, bool zero);
    void centred(const std::string& s, gd::Int font_index, gd::Int colour, float centre_x, float y, float alpha = 1.0f);
    // A GPU colour word (0xBBGGRR).
    static gfx::Color psx(gd::Int c) {
        return gfx::Color::c8(static_cast<int>(c & 0xFF), static_cast<int>((c >> 8) & 0xFF), static_cast<int>((c >> 16) & 0xFF));
    }
    // The game's triangle wave of the frame counter (draw_sim.triangle_wave): 0 ... 0xFF ... 0.
    static gd::Int triangle(gd::Int v) {
        v &= 0x1FF;
        return v >= 0x100 ? 0x1FF - v : v;
    }
    // A packet coordinate as the GPU reads it: 11 bits, signed.
    static gd::Int gpu_coordinate(gd::Int v) { return ((v & 0x7FF) ^ 0x400) - 0x400; }

private:
    float _flush_run(const std::string& run, gd::Int font, gd::Int colour, float x, float y);

    struct Pass {
        gfx::Blend blend;
        std::function<void()> painter;
        gfx::Rect2 clip;
        bool texel_mask;
        bool background = false;
    };
    std::vector<Pass> _passes;
};

}  // namespace t3
