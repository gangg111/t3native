#pragma once
// remake/presentation/screens/attract_screen.gd
// Draws the attract loop's 2D screens in vector form: the transition screen's
// `NAMCO PRESENTS.` and the title picture with its prompt, logo and copyright
// (FUN_8004FA38, FUN_800DAAD8). Texts use an open font in the original colours, laid out on
// the original 368 x 480 screen scaled into the window's 4:3 area.
#include <string>

#include "content/screen_data.h"
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "gfx/texture.h"

namespace t3 {

class TextLocale;

class AttractScreen {
public:
    static constexpr gd::Int PICTURE_BLACK = 0;
    static constexpr gd::Int PICTURE_TITLE_PROMPT = 2;
    static constexpr gd::Int PICTURE_TITLE_LOGO = 3;
    static constexpr gd::Int PICTURE_TITLE_COPYRIGHT = 4;
    static constexpr gd::Int PICTURE_TITLE = 5;
    static constexpr gd::Int PROMPT_BLINK = 0x30;   // the prompt shows while bit 4 or 5 of the frame counter is set
    static constexpr float TEXT_BOLDNESS = 0.55f;
    static constexpr float OUTLINE_SHARE = 0.16f;  // outline width per font height

    gd::Ref<ScreenData> data;
    gfx::TextureRef title_texture;
    gfx::TextureRef logo_texture;
    gfx::FontRef font;                    // FontVariation.new()
    gd::Int presents_mode = -1;
    gd::Int presents_level = 0;
    gd::Int picture = -1;
    gd::Int frame_count = 0;
    gd::Int prompt = 1;                   // which start prompt: no pad, P1, P2, both
    gd::PI32 _drawn;                      // what the last drawing showed (show_frame redraws on a change)

    bool enbu_variant = false;
    gd::Ref<TextLocale> locale;           // the texts' language (the copyright; the USA title picture)

    // Control: shown or not (game.gd never hides it) and its size (the full window).
    bool visible = true;
    gfx::Vec2 size;

    AttractScreen();

    void setup(const gd::Ref<ScreenData>& screens, bool enbu);
    // The title picture of title.ovl, or the copy enbu.ovl shows after the demonstration.
    void setup_variant(bool enbu);
    // The pictures of the variant in the texts' language (the USA title picture in English), from
    // the texture pack in use.
    void reload_pictures();
    void set_locale(const gd::Ref<TextLocale>& texts);
    void show_frame(gd::Int presents, gd::Int level, gd::Int kind, gd::Int frames);
    void queue_redraw() {}

    // Control._draw at the window's size (PRESET_FULL_RECT).
    void draw(gfx::Canvas& canvas, gfx::Vec2 window);
    void _draw(gfx::Canvas& canvas);
    // Consecutive texts of one original line (different colours), centred as the whole run.
    void _line(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Arr<gd::Ref<ScreenData::Text>>& parts, float alpha);
    gd::Int _size_px(const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t) const;
    // A text centred on `centre_x` (original x), its top at the original y.
    void _text(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t, float centre_x, float alpha);
    // A text in the texts' language (the converted texts show the font's @ as ©; the locale knows
    // the game's strings).
    std::string _shown(const gd::Ref<ScreenData::Text>& t) const;
    // Draws a text from window x `left` at its original y; returns its width (with trailing spaces).
    float _draw_text(gfx::Canvas& canvas, const gfx::Rect2& box, const gd::Ref<ScreenData::Text>& t, float left, float alpha);
};

}  // namespace t3
